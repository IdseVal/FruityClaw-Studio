#!/usr/bin/env python3
"""Dispatcher v2 -- the reconciler that runs the ORCA ADE agent workflow.

THE ONE IDEA
    The dispatcher never trusts an agent to report back. Every tick it OBSERVES reality
    (GitHub issues, PRs, labels, diffs; Orca worktrees and terminal liveness), COMPUTES
    what the workflow rules say should be true, and DOES the difference. Every action is
    idempotent, so a crash, a restart or a double run is harmless.

    It is deterministic Python, not an LLM: a poll loop is not a judgement call, and an
    agent left to poll would cost more, drift between ticks and forget things -- which is
    exactly the failure mode this version replaces.

WHERE IT RUNS
    On the HOST (not in the dev container), because it drives the `orca` CLI, which talks
    to the Orca desktop runtime. Orca is a desktop app: GitHub webhooks have nowhere to
    land, so the dispatcher POLLS `gh`. Install it as a Windows scheduled task
    (see install-task.ps1) so it survives reboots and is independent of any chat session.

WHAT IT DOES EACH TICK (the contract; `~/.orca/roles/dispatcher.md` is the human-readable
policy for the same thing)
    Issues
      - Gate: no issue is dispatched while `docs/CORE_DOCUMENT.md` on `dev` still carries
        the EMPTY marker. The PO & Analyst's onboarding PR flows through the PR pipeline
        like any other PR; once merged the gate opens by itself.
      - An open issue labelled `ready` with exactly one `role:*` label, none of
        `escalated` / `needs-human`, whose `Depends on: #a, #b` issues are all closed, and
        that has no worktree yet, is dispatched (up to `max_active_issues` at a time):
        an Orca worktree off `dev`, an agent in it, and a one-line prompt pointing at a
        brief file. The brief tells the agent to read its role file, the issue, the core
        document and specs, do the work, and open a PR into `dev`.
      - An issue whose agent session has gone idle without a PR is nudged once, then
        flagged `needs-human`.
    PRs (base `dev`)
      - No `state:*` label: docs-only diff (or issue labelled `trivial`) -> mark
        `state:tested` and say why; otherwise spawn the Tester into the developer's
        worktree.
      - `state:tested` -> spawn the Reviewer into the developer's worktree. The Reviewer
        merges with `gh pr merge --squash --delete-branch` (into `dev`; never `main`).
      - `state:blocked` -> bump the issue's cycle; under the breaker, send the blocker's
        comments back into the developer's session ("fix, push, remove the label"); over
        the breaker, label the issue `escalated` and page the human.
      - `needs-human` (issue or PR) -> bring the Orca tab forward, mention the human on
        GitHub, optional e-mail. Stop nudging until the label is removed.
      - Merged PR -> close the linked issue (merges into `dev` do NOT auto-close issues;
        only the default branch does), mark the worktree completed, remove it.
    Phases
      - Dependencies close by themselves, so "architects first, then developers" is a
        matter of the PO writing `Depends on: #5, #6` in developer issues. No human batch
        marking.

SUBCOMMANDS
    run               daemon loop (default interval 60s)
    once              one tick and exit (scheduled-task friendly)
    status            print the board (issues, PRs, sessions)
    doctor [--fix]    check prerequisites; --fix creates missing labels
    onboard           start the PO & Analyst onboarding worktree
    Flags: --dry-run (decide, change nothing), --interval N, --verbose

STATE
    `.orca/dispatcher/state.json` holds only de-duplication memory (which nudge, comment
    or spawn already happened, cycle counts). Truth lives in GitHub and Orca; deleting the
    state file costs at most a duplicate comment, never a lost issue.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import logging
import os
import re
import signal
import smtplib
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from email.mime.text import MIMEText
from logging.handlers import RotatingFileHandler
from pathlib import Path
from typing import Any, Callable, Optional

import yaml

# --------------------------------------------------------------------------- constants

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
CONFIG_FILE = REPO_ROOT / ".orca" / "dispatch.yml"
STATE_FILE = HERE / "state.json"
LOCK_FILE = HERE / "dispatcher.lock"
LOG_FILE = HERE / "dispatcher.log"
PROMPTS_DIR = HERE / "prompts"

# Role prompts are HOST-LEVEL and shared by every project this workflow drives.
ROLES_DIR = Path.home() / ".orca" / "roles"
ROLE_FILES = [
    "dispatcher", "po-analyst", "architect", "researcher",
    "developer", "tester", "reviewer",
]

LABEL_READY = "ready"
LABEL_ESCALATED = "escalated"
LABEL_NEEDS_HUMAN = "needs-human"
LABEL_TESTED = "state:tested"
LABEL_BLOCKED = "state:blocked"
LABEL_TRIVIAL = "trivial"
ROLE_PREFIX = "role:"

# Every label the workflow uses. `doctor --fix` creates the missing ones.
REQUIRED_LABELS: dict[str, tuple[str, str]] = {
    LABEL_READY: ("Approved for dispatch; the dispatcher picks it up", "0E8A16"),
    LABEL_ESCALATED: ("Circuit breaker tripped; a human must resolve", "B60205"),
    LABEL_NEEDS_HUMAN: ("An agent needs the human (interview, decision, key)", "D93F0B"),
    LABEL_TESTED: ("Tester passed (or docs-only); the Reviewer is next", "0E8A16"),
    LABEL_BLOCKED: ("Tester or Reviewer sent the PR back to the Developer", "D93F0B"),
    LABEL_TRIVIAL: ("Skips the Tester, never the Reviewer", "C5DEF5"),
    "role:developer": ("Implementation; loads ~/.orca/roles/developer.md", "1D76DB"),
    "role:architect": ("Design and contracts; loads ~/.orca/roles/architect.md", "5319E7"),
    "role:researcher": ("Facts about external systems; loads ~/.orca/roles/researcher.md", "5319E7"),
    "ui": ("Interface work; loads the design skills", "0052CC"),
    "seo": ("Search work; loads seo-audit", "C5DEF5"),
    "scraper": ("External data acquisition; loads just-scrape", "E99695"),
    "bug": ("Defect; loads diagnosing-bugs", "D73A4A"),
    "data": ("Data or pipeline; evidence gate applies", "C5DEF5"),
}

SUBPROCESS_TIMEOUT = 180
TUI_READY_TIMEOUT_MS = 90_000
STILL_ACTIVE = 259

_ISSUE_RE = re.compile(r"issue-(\d+)")
_DEPENDS_RE = re.compile(r"(?i)\b(?:depends\s+on|blocked\s+by)\b[^\n]*")
_HASH_RE = re.compile(r"#(\d+)")
_CLOSES_RE = re.compile(r"(?i)\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\s+#(\d+)")

log = logging.getLogger("dispatcher")
_stop = False
DRY_RUN = False


def _on_signal(signum: int, frame: Any) -> None:  # noqa: ARG001
    global _stop
    _stop = True


# --------------------------------------------------------------------------- utilities


def now_ms() -> int:
    return int(time.time() * 1000)


def minutes_since(ms: Optional[int]) -> float:
    if not ms:
        return float("inf")
    return (now_ms() - ms) / 60_000.0


def run(argv: list[str], timeout: int = SUBPROCESS_TIMEOUT) -> tuple[bool, str, str]:
    """Run a command; never raise. Returns (ok, stdout, stderr)."""
    try:
        proc = subprocess.run(
            argv, check=False, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=timeout,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as exc:
        log.warning("%s unavailable: %s", argv[0], exc)
        return False, "", str(exc)
    if proc.returncode != 0:
        log.debug("%s failed (%s): %s", " ".join(argv[:4]), proc.returncode, proc.stderr.strip()[:400])
        return False, proc.stdout, proc.stderr
    return True, proc.stdout, proc.stderr


def gh_json(args: list[str]) -> Any:
    ok, out, _ = run(["gh", *args])
    if not ok or not out.strip():
        return None
    try:
        return json.loads(out)
    except json.JSONDecodeError as exc:
        log.warning("gh returned bad JSON: %s", exc)
        return None


def orca_json(args: list[str]) -> Optional[dict[str, Any]]:
    """Run an orca command with --json; return the `result` dict or None."""
    ok, out, err = run(["orca", *args, "--json"])
    if not out.strip():
        if not ok:
            log.debug("orca %s: %s", " ".join(args[:3]), err.strip()[:200])
        return None
    try:
        payload = json.loads(out)
    except json.JSONDecodeError:
        return None
    if not payload.get("ok", False):
        log.debug("orca %s -> %s", " ".join(args[:3]), payload.get("error"))
        return None
    return payload.get("result") or {}


def act(description: str, fn: Callable[[], Any]) -> Any:
    """Perform a side effect unless --dry-run. Always logs what it did / would do."""
    if DRY_RUN:
        log.info("[dry-run] %s", description)
        return None
    log.info(description)
    return fn()


def pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name == "nt":
        kernel32 = ctypes.windll.kernel32  # type: ignore[attr-defined]
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return bool(ok) and code.value == STILL_ACTIVE
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def acquire_lock() -> bool:
    """One dispatcher per repository. state.json is not safe for concurrent writers."""
    try:
        if LOCK_FILE.exists():
            try:
                old = int(LOCK_FILE.read_text().strip() or 0)
            except ValueError:
                old = 0
            if old and old != os.getpid() and pid_alive(old):
                return False
        LOCK_FILE.write_text(str(os.getpid()))
        return True
    except OSError as exc:
        log.warning("lock: %s", exc)
        return True  # never refuse to run because the lock file is odd


def release_lock() -> None:
    try:
        if LOCK_FILE.exists() and LOCK_FILE.read_text().strip() == str(os.getpid()):
            LOCK_FILE.unlink()
    except OSError:
        pass


# --------------------------------------------------------------------------- config & state


def load_config() -> dict[str, Any]:
    with CONFIG_FILE.open("r", encoding="utf-8") as fh:
        cfg = yaml.safe_load(fh) or {}
    d = cfg.setdefault("dispatcher", {})
    d.setdefault("agent", "claude")
    d.setdefault("poll_interval_seconds", 60)
    d.setdefault("max_active_issues", 4)
    d.setdefault("idle_minutes_before_nudge", 15)
    d.setdefault("idle_minutes_after_nudge", 15)
    d.setdefault("cleanup_worktrees_on_merge", True)
    d.setdefault("docs_only", {"prefixes": ["docs/"], "suffixes": [".md", ".txt"]})
    d.setdefault("notify", {})
    g = cfg.setdefault("gates", {}).setdefault("core_document", {})
    g.setdefault("path", "docs/CORE_DOCUMENT.md")
    g.setdefault("empty_marker", "Status: EMPTY")
    cfg.setdefault("branches", {}).setdefault("base", "dev")
    cfg.setdefault("circuit_breaker", {}).setdefault("max_cycles", 3)
    return cfg


@dataclass
class State:
    issues: dict[str, dict[str, Any]] = field(default_factory=dict)
    prs: dict[str, dict[str, Any]] = field(default_factory=dict)
    notified: dict[str, int] = field(default_factory=dict)   # "issue:5" / "pr:22" -> ms
    closed_issues: list[int] = field(default_factory=list)  # issues we closed (dedupe)

    @classmethod
    def load(cls) -> "State":
        if not STATE_FILE.exists():
            return cls()
        try:
            raw = json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            log.warning("state unreadable (%s); starting fresh", exc)
            return cls()
        st = cls(
            issues=raw.get("issues", {}), prs=raw.get("prs", {}),
            notified=raw.get("notified", {}), closed_issues=raw.get("closed_issues", []),
        )
        # Migrate v1 layout (dispatched{} + prs{stage}) so in-flight work is not redone.
        for num, old in (raw.get("dispatched") or {}).items():
            st.issues.setdefault(num, {})["cycle"] = int(old.get("cycle", 1))
        for num, old in (raw.get("prs") or {}).items():
            stage = old.get("stage")
            if stage in ("tester_dispatched", "reviewer_dispatched", "blocked"):
                entry = st.prs.setdefault(num, {})
                entry.setdefault("round", 0)
                if stage in ("tester_dispatched", "reviewer_dispatched"):
                    entry["tester_round"] = 0
                    entry["tester_terminal"] = old.get("tester_terminal")
                if stage == "reviewer_dispatched":
                    entry["reviewer_round"] = 0
                    entry["reviewer_terminal"] = old.get("reviewer_terminal")
                entry.pop("stage", None)
        return st

    def save(self) -> None:
        if DRY_RUN:
            return
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = STATE_FILE.with_suffix(".tmp")
        tmp.write_text(json.dumps({
            "issues": self.issues, "prs": self.prs,
            "notified": self.notified, "closed_issues": self.closed_issues,
        }, indent=2), encoding="utf-8")
        tmp.replace(STATE_FILE)

    def issue(self, n: int) -> dict[str, Any]:
        return self.issues.setdefault(str(n), {})

    def pr(self, n: int) -> dict[str, Any]:
        return self.prs.setdefault(str(n), {})


# --------------------------------------------------------------------------- observation


@dataclass
class Issue:
    number: int
    title: str
    body: str
    labels: set[str]
    url: str
    state: str  # OPEN / CLOSED

    @property
    def role(self) -> Optional[str]:
        roles = sorted(l[len(ROLE_PREFIX):] for l in self.labels if l.startswith(ROLE_PREFIX))
        return roles[0] if len(roles) == 1 else None

    @property
    def role_problem(self) -> Optional[str]:
        roles = [l for l in self.labels if l.startswith(ROLE_PREFIX)]
        if not roles:
            return "no `role:*` label"
        if len(roles) > 1:
            return f"multiple `role:*` labels: {sorted(roles)}"
        return None

    @property
    def depends_on(self) -> list[int]:
        found: list[int] = []
        for m in _DEPENDS_RE.finditer(self.body or ""):
            found.extend(int(x) for x in _HASH_RE.findall(m.group(0)))
        return sorted(set(found) - {self.number})


@dataclass
class PR:
    number: int
    title: str
    body: str
    labels: set[str]
    url: str
    head: str
    files: list[str]
    is_draft: bool
    state: str
    merged_at: Optional[str] = None

    @property
    def issue_number(self) -> Optional[int]:
        m = _ISSUE_RE.search(self.head)
        if m:
            return int(m.group(1))
        m = _CLOSES_RE.search(self.body or "")
        return int(m.group(1)) if m else None


@dataclass
class Worktree:
    id: str
    path: str
    branch: str       # without refs/heads/
    name: str         # displayName
    linked_issue: Optional[int]
    status: str

    @property
    def issue_number(self) -> Optional[int]:
        if self.linked_issue:
            return self.linked_issue
        m = _ISSUE_RE.search(self.name) or _ISSUE_RE.search(self.branch)
        return int(m.group(1)) if m else None


@dataclass
class Observed:
    issues: dict[int, Issue]
    prs: list[PR]              # open, base dev
    merged: list[PR]           # merged, base dev (recent)
    worktrees: list[Worktree]  # this repo
    gate_open: bool
    gate_reason: str
    repo_id: Optional[str]


def observe(cfg: dict[str, Any]) -> Observed:
    base = cfg["branches"]["base"]

    issues: dict[int, Issue] = {}
    for raw in gh_json(["issue", "list", "--state", "all", "--limit", "500",
                        "--json", "number,title,body,labels,url,state"]) or []:
        issues[int(raw["number"])] = Issue(
            number=int(raw["number"]), title=raw.get("title", ""), body=raw.get("body") or "",
            labels={l["name"] for l in raw.get("labels", [])}, url=raw.get("url", ""),
            state=raw.get("state", "OPEN"),
        )

    def _prs(state: str, limit: int) -> list[PR]:
        out: list[PR] = []
        for raw in gh_json(["pr", "list", "--base", base, "--state", state, "--limit", str(limit),
                            "--json", "number,title,body,labels,url,headRefName,files,isDraft,state,mergedAt"]) or []:
            out.append(PR(
                number=int(raw["number"]), title=raw.get("title", ""), body=raw.get("body") or "",
                labels={l["name"] for l in raw.get("labels", [])}, url=raw.get("url", ""),
                head=raw.get("headRefName", ""), files=[f["path"] for f in raw.get("files", [])],
                is_draft=bool(raw.get("isDraft")), state=raw.get("state", state.upper()),
                merged_at=raw.get("mergedAt"),
            ))
        return out

    prs = _prs("open", 200)
    merged = _prs("merged", 100)

    repo_id: Optional[str] = None
    res = orca_json(["repo", "show", "--repo", f"path:{REPO_ROOT}"])
    if res:
        repo_id = (res.get("repo") or {}).get("id")

    worktrees: list[Worktree] = []
    res = orca_json(["worktree", "list", "--limit", "500"])
    for raw in (res or {}).get("worktrees", []) or []:
        if repo_id and raw.get("repoId") != repo_id:
            continue
        if raw.get("isMainWorktree") or raw.get("isArchived"):
            continue
        worktrees.append(Worktree(
            id=raw.get("id", ""), path=raw.get("path", ""),
            branch=(raw.get("branch") or "").removeprefix("refs/heads/"),
            name=raw.get("displayName") or "", linked_issue=raw.get("linkedIssue"),
            status=raw.get("workspaceStatus") or "",
        ))

    gate_open, gate_reason = core_document_gate(cfg)
    return Observed(issues, prs, merged, worktrees, gate_open, gate_reason, repo_id)


def core_document_gate(cfg: dict[str, Any]) -> tuple[bool, str]:
    g = cfg["gates"]["core_document"]
    base = cfg["branches"]["base"]
    run(["git", "-C", str(REPO_ROOT), "fetch", "-q", "origin", base], timeout=60)
    ok, out, _ = run(["git", "-C", str(REPO_ROOT), "show", f"origin/{base}:{g['path']}"])
    if not ok:
        return False, f"{g['path']} not found on origin/{base}"
    if g["empty_marker"] in out:
        return False, f"{g['path']} on {base} still says '{g['empty_marker']}' -- run the onboarding interview"
    return True, "core document present on " + base


def terminals_for(wt: Worktree) -> list[dict[str, Any]]:
    res = orca_json(["terminal", "list", "--worktree", f"id:{wt.id}", "--limit", "50"])
    return list((res or {}).get("terminals", []) or [])


def last_output_ms(terms: list[dict[str, Any]]) -> Optional[int]:
    vals = [t.get("lastOutputAt") for t in terms if t.get("lastOutputAt")]
    return max(vals) if vals else None


# --------------------------------------------------------------------------- classification


def is_docs_only(pr: PR, cfg: dict[str, Any]) -> bool:
    if not pr.files:
        return False
    d = cfg["dispatcher"]["docs_only"]
    prefixes = tuple(d.get("prefixes", [])) or ()
    suffixes = tuple(d.get("suffixes", [])) or ()
    return all(f.startswith(prefixes) or f.endswith(suffixes) for f in pr.files)


def is_trivial(issue: Optional[Issue], cfg: dict[str, Any]) -> bool:
    if not issue or LABEL_TRIVIAL not in issue.labels:
        return False
    forbid = set((cfg.get("pipelines", {}).get("trivial", {}) or {}).get("forbid_labels", []))
    return not (issue.labels & forbid)


def skills_for(role: str, labels: set[str], cfg: dict[str, Any]) -> list[str]:
    out: list[str] = []
    for s in (cfg.get("roles", {}).get(role, {}) or {}).get("skills", []) or []:
        if s not in out:
            out.append(s)
    label_map = cfg.get("labels", {}) or {}
    for label in sorted(labels):
        for s in (label_map.get(label) or {}).get("skills", []) or []:
            if s not in out:
                out.append(s)
    return out


# --------------------------------------------------------------------------- briefs


def write_brief(name: str, text: str) -> Path:
    PROMPTS_DIR.mkdir(parents=True, exist_ok=True)
    path = PROMPTS_DIR / f"{name}.md"
    if not DRY_RUN:
        path.write_text(text, encoding="utf-8")
    return path


def one_liner(brief: Path) -> str:
    """What the agent actually receives. One line: no newline-submits-early, no argv limits."""
    return f"Read the file {brief.as_posix()} and follow its instructions exactly."


COMMON_RULES = """
Rules that apply to every role:
- You are STRICTLY FORBIDDEN from merging anything into `main`. Only the Reviewer merges, and only into `dev`.
- Role prompts live at `~/.orca/roles/` and are shared by every project; do not copy them into the repo.
- If you need the human (a decision, an interview, a credential): post a comment explaining exactly what you need, add the label `needs-human` (`gh issue edit N --add-label needs-human` or `gh pr edit N --add-label needs-human`), and WAIT in this session. The human will answer here. Remove the label yourself when you have what you need.
- If a skill your role names is missing from `~/.claude/skills/`, say so in a comment and continue with plain tools if you can; otherwise use `needs-human`.
- Never leave work uncommitted when you stop. Never force-push.
"""


def issue_brief(issue: Issue, role: str, cycle: int, max_cycles: int, skills: list[str],
                base: str) -> str:
    return f"""# Dispatch brief -- issue #{issue.number} -- role: {role} -- cycle {cycle}/{max_cycles}

You are the **{role}** for issue #{issue.number}: {issue.title}
Worktree: this directory, on its own branch off `{base}`.

Do these in order:
1. Read your role: `cat ~/.orca/roles/{role}.md`. Follow it exactly.
2. Read the task: `gh issue view {issue.number} --comments`.
3. Read `docs/CORE_DOCUMENT.md`, then the relevant `docs/specs/*.md` and `docs/adrs/*.md`.
4. Do ONLY what the issue asks, in this worktree. Commit in small, clear commits.
5. Finish by publishing a pull request into `{base}`:
       git push -u origin HEAD
       gh pr create --base {base} --title "<what you did> (#{issue.number})" --body "<summary, evidence, decisions>. Refs #{issue.number}"
   The dispatcher closes the issue when the PR merges; you do not close it.
6. After the PR exists you are done. Say so in one line and stop. Do not merge.

Skills earned by this issue's labels: {', '.join(skills) if skills else '(role baseline only)'}
Labels: {', '.join(sorted(issue.labels)) or '(none)'}
Issue URL: {issue.url}
{COMMON_RULES}
"""


def nudge_brief(issue: Issue, base: str) -> str:
    return f"""# Nudge -- issue #{issue.number}

The dispatcher sees no pull request for issue #{issue.number} and your session has been idle.

- If the work is done: commit, `git push -u origin HEAD`, `gh pr create --base {base} --title "... (#{issue.number})" --body "... Refs #{issue.number}"`, then stop.
- If you are stuck or need a decision: comment on the issue with exactly what you need and add the `needs-human` label, then wait here.
- If you have not started: read `~/.orca/roles/<your role>.md` and `gh issue view {issue.number}` and start.
{COMMON_RULES}
"""


def tester_brief(pr: PR, issue: Optional[Issue], round_no: int) -> str:
    n = pr.number
    return f"""# Dispatch brief -- TESTER -- PR #{n} (round {round_no + 1})

You are the **tester** for PR #{n}: {pr.title}
Linked issue: {('#' + str(issue.number) + ' ' + issue.title) if issue else '(none found)'}
Branch checked out in this worktree: `{pr.head}`.

1. `cat ~/.orca/roles/tester.md` and follow it (hybrid strategy; FMEA interview for large components via `needs-human`).
2. `gh pr view {n} --comments` and `git diff dev...HEAD --stat`.
3. Run what exists: the project's test suite, linters, builds. For new behaviour with no tests, write them per your role, commit them on this branch and push (`git push`).
4. Record the outcome on the PR -- this is what the dispatcher reads:
   - PASS:  `gh pr edit {n} --add-label state:tested`  then `gh pr comment {n} --body "<what ran, counts, what you added>"`
   - FAIL:  `gh pr edit {n} --add-label state:blocked` then `gh pr comment {n} --body "<exactly what failed and what the developer must change>"`
   Exactly one of the two labels, always. A session that ends without a label stalls the pipeline.
5. Stop. Do not merge, do not touch `main`.
{COMMON_RULES}
"""


def reviewer_brief(pr: PR, issue: Optional[Issue], round_no: int, docs_only: bool) -> str:
    n = pr.number
    extra = ("This PR is docs-only (ADRs/specs/research): check file names follow `docs/adrs/ADR-<issue#>-<slug>.md` "
             "and `docs/specs/<topic>.md`, links resolve, numbering does not collide, and it does not contradict "
             "`docs/CORE_DOCUMENT.md`.") if docs_only else (
             "Verify the change fulfils the spec, meets the code standard (readable, minimalist, no premature "
             "abstraction), tests pass, and evidence gates in `.orca/dispatch.yml` are satisfied for labels data/ui/pipeline.")
    return f"""# Dispatch brief -- REVIEWER -- PR #{n} (round {round_no + 1})

You are the **reviewer** for PR #{n}: {pr.title}
Linked issue: {('#' + str(issue.number) + ' ' + issue.title) if issue else '(none found)'}

1. `cat ~/.orca/roles/reviewer.md` and follow it.
2. `gh pr view {n} --comments`, `gh pr diff {n}`, and read the linked issue and the relevant spec.
3. {extra}
4. Decide -- this is what the dispatcher reads:
   - APPROVE:  `gh pr merge {n} --squash --delete-branch` (this merges into `dev`), then `gh pr comment {n} --body "<one-paragraph review>"`.
     If the merge fails because of conflicts with `dev`, treat it as CHANGES: the developer rebases.
   - CHANGES:  `gh pr edit {n} --add-label state:blocked --remove-label state:tested` then `gh pr comment {n} --body "<numbered list of required changes>"`.
5. Stop. You may merge into `dev` and only `dev`; never `main`. You never close issues; the dispatcher does.
{COMMON_RULES}
"""


def blocked_brief(pr: PR, issue: Optional[Issue], cycle: int, max_cycles: int, comments: str) -> str:
    n = pr.number
    return f"""# PR #{n} was sent back -- cycle {cycle}/{max_cycles}

Your PR #{n} ({pr.title}) received `state:blocked`. The blocker's comments:

{comments}

Do this:
1. Address every point. Commit and `git push` on this branch.
2. Reply on the PR with what you changed: `gh pr comment {n} --body "..."`.
3. Hand it back: `gh pr edit {n} --remove-label state:blocked --remove-label state:tested`. The tester runs again automatically.
4. If a point is wrong or impossible, say so on the PR and add `needs-human`; do not argue in circles.
After cycle {max_cycles} the circuit breaker escalates to the human.
{COMMON_RULES}
"""


def onboarding_brief(base: str) -> str:
    return f"""# Onboarding -- PO & Analyst

You are the **PO & Analyst**. Read `cat ~/.orca/roles/po-analyst.md` and follow it exactly.

Phase 1 -- the core document (the human is in the loop):
- `docs/CORE_DOCUMENT.md` is an empty template. Fill it by deep interview, in rounds, with the human in this session. Record, read back, ask what is wrong or missing. Unknowns are OPEN, never guessed.
- When the human says the document is AGREED: replace the `Status: EMPTY` line with `Status: AGREED -- <date>`, commit, `git push -u origin HEAD`, and open a PR into `{base}`:
      gh pr create --base {base} --title "Populate core document from owner interview" --body "Agreed with the owner in this session."
  The pipeline reviews and merges it; nothing else can start until it is on `{base}`.

Phase 2 -- specs, ADRs, issues (after the PR is merged):
- Derive `docs/specs/*.md` and ADRs as `docs/adrs/ADR-<issue#>-<slug>.md` where the number is the GitHub issue that produced them.
- Write GitHub issues. Each issue: what is wanted, how anyone will know it worked, what is out of scope, and a line `Depends on: #a, #b` when it must wait for other issues. Labels: exactly one `role:*` (`role:researcher`, `role:architect`, `role:developer`), skill labels (`ui`, `seo`, `scraper`, `bug`, `data`), `trivial` where a tester has nothing to do, and `ready` when it may start.
- Order of work is expressed ONLY through `Depends on:`; the dispatcher holds an issue until its dependencies are closed. Typical: research first, architecture depends on research, implementation depends on architecture.
{COMMON_RULES}
"""


# --------------------------------------------------------------------------- orca actions


def orca_create_worktree(name: str, base: str, issue_number: Optional[int], agent: str,
                         prompt: str, activate: bool = False) -> Optional[dict[str, Any]]:
    args = ["worktree", "create", "--repo", f"path:{REPO_ROOT}", "--name", name,
            "--base-branch", base, "--agent", agent, "--prompt", prompt]
    if issue_number:
        args += ["--issue", str(issue_number)]
    if activate:
        args += ["--activate"]
    return orca_json(args)


def orca_spawn_terminal(wt: Worktree, title: str, command: str, text: str) -> Optional[str]:
    """Start an agent in a fresh terminal inside a worktree and feed it one line."""
    res = orca_json(["terminal", "create", "--worktree", f"id:{wt.id}", "--title", title, "--command", command])
    term = (res or {}).get("terminal") or {}
    handle = term.get("handle") or (res or {}).get("handle")
    if not handle:
        log.warning("terminal create returned no handle for %s", wt.name)
        return None
    run(["orca", "terminal", "wait", "--terminal", handle, "--for", "tui-idle",
         "--timeout-ms", str(TUI_READY_TIMEOUT_MS)], timeout=TUI_READY_TIMEOUT_MS // 1000 + 30)
    ok, _, _ = run(["orca", "terminal", "send", "--terminal", handle, "--text", text, "--enter"])
    return handle if ok else None


def orca_send(handle: str, text: str) -> bool:
    ok, _, _ = run(["orca", "terminal", "send", "--terminal", handle, "--text", text, "--enter"])
    return ok


def orca_deliver(wt: Worktree, title: str, command: str, text: str,
                 preferred_handle: Optional[str] = None) -> Optional[str]:
    """Deliver a one-liner to a live session in the worktree, or start one."""
    terms = terminals_for(wt)
    handles = [t.get("handle") for t in terms if t.get("handle") and t.get("writable", True)]
    if preferred_handle and preferred_handle in handles and orca_send(preferred_handle, text):
        return preferred_handle
    if handles:
        newest = max(terms, key=lambda t: t.get("lastOutputAt") or 0)
        h = newest.get("handle")
        if h and orca_send(h, text):
            return h
    return orca_spawn_terminal(wt, title, command, text)


def orca_note(wt: Worktree, comment: str, status: Optional[str] = None) -> None:
    args = ["worktree", "set", "--worktree", f"id:{wt.id}", "--comment", comment]
    if status:
        args += ["--workspace-status", status]
    orca_json(args)


def orca_bring_forward(wt: Worktree) -> None:
    terms = terminals_for(wt)
    if terms:
        newest = max(terms, key=lambda t: t.get("lastOutputAt") or 0)
        if newest.get("handle"):
            orca_json(["terminal", "switch", "--terminal", newest["handle"]])


# --------------------------------------------------------------------------- github actions


def gh_comment(kind: str, number: int, body: str) -> None:
    run(["gh", kind, "comment", str(number), "--body", body])


def gh_label(kind: str, number: int, add: list[str] = (), remove: list[str] = ()) -> None:
    args = ["gh", kind, "edit", str(number)]
    for l in add:
        args += ["--add-label", l]
    for l in remove:
        args += ["--remove-label", l]
    if add or remove:
        run(args)


def gh_recent_comments(pr_number: int, limit: int = 4) -> str:
    data = gh_json(["pr", "view", str(pr_number), "--json", "comments,reviews"]) or {}
    items = []
    for c in data.get("comments", []) or []:
        items.append((c.get("createdAt", ""), c.get("author", {}).get("login", "?"), c.get("body", "")))
    for r in data.get("reviews", []) or []:
        if r.get("body"):
            items.append((r.get("submittedAt", ""), r.get("author", {}).get("login", "?"), r.get("body", "")))
    items.sort()
    out = []
    for _, who, body in items[-limit:]:
        out.append(f"--- {who} ---\n{body.strip()[:3000]}")
    return "\n\n".join(out) or "(no comments found; read the PR conversation)"


def notify_human(cfg: dict[str, Any], state: State, key: str, subject: str, body: str,
                 kind: str, number: int, wt: Optional[Worktree]) -> None:
    """Mention on GitHub (GitHub e-mails mentions), bring the Orca tab forward, optional SMTP."""
    if key in state.notified:
        return
    mention = (cfg["dispatcher"].get("notify") or {}).get("github_mention", "")
    text = f"{mention} {subject}\n\n{body}".strip()
    act(f"notify human: {subject}", lambda: gh_comment(kind, number, text))
    if wt:
        act(f"orca: bring {wt.name} forward", lambda: (orca_note(wt, f"NEEDS HUMAN: {subject}"), orca_bring_forward(wt)))
    smtp = {k: os.environ.get(k, "") for k in ("SMTP_SENDER_EMAIL", "SMTP_RECEIVER_EMAIL", "SMTP_PASSWORD", "SMTP_SERVER", "SMTP_PORT")}
    if all(smtp.values()) and not DRY_RUN:
        try:
            msg = MIMEText(f"{subject}\n\n{body}", "plain", "utf-8")
            msg["Subject"] = f"[dispatcher] {subject}"
            msg["From"], msg["To"] = smtp["SMTP_SENDER_EMAIL"], smtp["SMTP_RECEIVER_EMAIL"]
            with smtplib.SMTP_SSL(smtp["SMTP_SERVER"], int(smtp["SMTP_PORT"]), timeout=30) as s:
                s.login(smtp["SMTP_SENDER_EMAIL"], smtp["SMTP_PASSWORD"])
                s.sendmail(msg["From"], [msg["To"]], msg.as_string())
        except Exception as exc:  # noqa: BLE001
            log.warning("smtp notify failed: %s", exc)
    state.notified[key] = now_ms()
    state.save()


# --------------------------------------------------------------------------- reconcile: issues


def reconcile_issues(obs: Observed, cfg: dict[str, Any], state: State) -> None:
    d = cfg["dispatcher"]
    base = cfg["branches"]["base"]
    max_cycles = int(cfg["circuit_breaker"]["max_cycles"])
    agent = d["agent"]

    wt_by_issue: dict[int, Worktree] = {}
    for wt in obs.worktrees:
        n = wt.issue_number
        if n and n not in wt_by_issue:
            wt_by_issue[n] = wt
    pr_by_issue: dict[int, PR] = {}
    for pr in obs.prs:
        n = pr.issue_number
        if n and n not in pr_by_issue:
            pr_by_issue[n] = pr

    merged_issues = {pr.issue_number for pr in obs.merged if pr.issue_number}
    open_issues = sorted((i for i in obs.issues.values()
                          if i.state == "OPEN" and i.number not in merged_issues), key=lambda i: i.number)
    active = [n for n in wt_by_issue if n in obs.issues and obs.issues[n].state == "OPEN" and n not in merged_issues]
    slots = max(0, int(d["max_active_issues"]) - len(active))

    for issue in open_issues:
        s = state.issue(issue.number)
        wt = wt_by_issue.get(issue.number)
        pr = pr_by_issue.get(issue.number)

        # needs-human: page once, then leave alone.
        if LABEL_NEEDS_HUMAN in issue.labels:
            notify_human(cfg, state, f"issue:{issue.number}:needs-human",
                         f"issue #{issue.number} needs you", f"{issue.title}\n{issue.url}", "issue", issue.number, wt)
            continue
        state.notified.pop(f"issue:{issue.number}:needs-human", None)

        if LABEL_ESCALATED in issue.labels or LABEL_READY not in issue.labels:
            continue

        # --- in flight -----------------------------------------------------------------
        if wt is not None:
            if pr is not None:
                s.pop("nudged_at", None)
                continue  # the PR pipeline owns it now
            terms = terminals_for(wt)
            idle_min = minutes_since(last_output_ms(terms)) if terms else float("inf")
            nudged_at = s.get("nudged_at")
            if nudged_at is None:
                if idle_min >= float(d["idle_minutes_before_nudge"]):
                    brief = write_brief(f"issue-{issue.number}-nudge", nudge_brief(issue, base))
                    def _nudge(wt=wt, brief=brief, issue=issue):
                        return orca_deliver(wt, f"{issue.role or 'agent'} #{issue.number}", agent, one_liner(brief))
                    act(f"issue #{issue.number}: idle {idle_min:.0f} min without PR -> nudge", _nudge)
                    s["nudged_at"] = now_ms()
                    state.save()
            elif minutes_since(nudged_at) >= float(d["idle_minutes_after_nudge"]) and \
                    idle_min >= float(d["idle_minutes_after_nudge"]):
                act(f"issue #{issue.number}: still no PR after nudge -> needs-human",
                    lambda n=issue.number: gh_label("issue", n, add=[LABEL_NEEDS_HUMAN]))
                s.pop("nudged_at", None)
                state.save()
            continue

        # --- candidate for dispatch -------------------------------------------------------
        if not obs.gate_open:
            log.debug("issue #%s held: %s", issue.number, obs.gate_reason)
            continue
        problem = issue.role_problem
        if problem:
            key = f"role-problem:{sorted(issue.labels)}"
            if s.get("role_problem_key") != key:
                act(f"issue #{issue.number}: {problem} -> comment",
                    lambda n=issue.number, p=problem: gh_comment("issue", n,
                        f"Cannot dispatch: {p}. Add exactly one `role:*` label (`role:developer`, `role:architect`, `role:researcher`)."))
                s["role_problem_key"] = key
                state.save()
            continue
        role = issue.role or "developer"
        if not (ROLES_DIR / f"{role}.md").exists():
            log.warning("issue #%s: role file missing: %s", issue.number, ROLES_DIR / f"{role}.md")
            continue
        unmet = [n for n in issue.depends_on if n not in obs.issues or obs.issues[n].state != "CLOSED"]
        if unmet:
            log.debug("issue #%s waits on %s", issue.number, unmet)
            continue
        cycle = int(s.get("cycle", 0)) + 1
        if cycle > max_cycles:
            act(f"issue #{issue.number}: cycle {cycle} > {max_cycles} -> escalate",
                lambda n=issue.number: (gh_label("issue", n, add=[LABEL_ESCALATED]),
                                        gh_comment("issue", n, f"Circuit breaker tripped: {max_cycles} failed attempts.")))
            notify_human(cfg, state, f"issue:{issue.number}:escalated", f"issue #{issue.number} escalated",
                         issue.url, "issue", issue.number, None)
            continue
        if slots <= 0:
            log.debug("issue #%s waits for a slot (%s active)", issue.number, len(active))
            continue

        skills = skills_for(role, issue.labels, cfg)
        brief = write_brief(f"issue-{issue.number}-cycle{cycle}",
                            issue_brief(issue, role, cycle, max_cycles, skills, base))
        name = f"issue-{issue.number}"

        def _dispatch(name=name, issue=issue, brief=brief):
            return orca_create_worktree(name, base, issue.number, agent, one_liner(brief))
        res = act(f"issue #{issue.number}: dispatch as {role} (cycle {cycle}/{max_cycles})", _dispatch)
        if DRY_RUN or res:
            s["cycle"] = cycle
            s["role"] = role
            s.pop("nudged_at", None)
            state.save()
            act(f"issue #{issue.number}: comment dispatched",
                lambda n=issue.number, r=role, c=cycle: gh_comment("issue", n,
                    f"Dispatched as `{r}` to worktree `issue-{n}` (cycle {c}/{max_cycles})."))
            slots -= 1
            active.append(issue.number)
        else:
            log.warning("issue #%s: orca worktree create failed; will retry next tick", issue.number)


# --------------------------------------------------------------------------- reconcile: PRs


def reconcile_prs(obs: Observed, cfg: dict[str, Any], state: State) -> None:
    d = cfg["dispatcher"]
    base = cfg["branches"]["base"]
    agent = d["agent"]
    max_cycles = int(cfg["circuit_breaker"]["max_cycles"])
    wt_by_branch = {wt.branch: wt for wt in obs.worktrees}

    for pr in obs.prs:
        if pr.is_draft:
            continue
        s = state.pr(pr.number)
        s.setdefault("round", 0)
        issue = obs.issues.get(pr.issue_number) if pr.issue_number else None
        wt = wt_by_branch.get(pr.head)

        if LABEL_NEEDS_HUMAN in pr.labels:
            notify_human(cfg, state, f"pr:{pr.number}:needs-human", f"PR #{pr.number} needs you",
                         f"{pr.title}\n{pr.url}", "pr", pr.number, wt)
            continue
        state.notified.pop(f"pr:{pr.number}:needs-human", None)

        # --- blocked: send back to the developer, under the breaker ------------------------
        if LABEL_BLOCKED in pr.labels:
            if s.get("blocked_handled"):
                continue
            cycle = 1
            if issue:
                si = state.issue(issue.number)
                cycle = int(si.get("cycle", 1)) + 1
                si["cycle"] = cycle
            if cycle > max_cycles:
                def _esc(pr=pr, issue=issue):
                    gh_comment("pr", pr.number, f"Circuit breaker tripped: {max_cycles} failed attempts.")
                    if issue:
                        gh_label("issue", issue.number, add=[LABEL_ESCALATED])
                        gh_comment("issue", issue.number, f"Circuit breaker tripped on PR #{pr.number}: {max_cycles} failed attempts.")
                act(f"PR #{pr.number}: blocked beyond breaker -> escalate", _esc)
                notify_human(cfg, state, f"pr:{pr.number}:escalated", f"PR #{pr.number} escalated", pr.url, "pr", pr.number, wt)
            elif wt is None:
                act(f"PR #{pr.number}: blocked but no worktree for {pr.head} -> needs-human",
                    lambda n=pr.number: gh_label("pr", n, add=[LABEL_NEEDS_HUMAN]))
            else:
                comments = gh_recent_comments(pr.number)
                brief = write_brief(f"pr-{pr.number}-blocked-cycle{cycle}",
                                    blocked_brief(pr, issue, cycle, max_cycles, comments))
                def _send(wt=wt, brief=brief, pr=pr):
                    return orca_deliver(wt, f"developer PR#{pr.number}", agent, one_liner(brief), s.get("developer_terminal"))
                act(f"PR #{pr.number}: blocked -> back to developer (cycle {cycle}/{max_cycles})", _send)
                act(f"PR #{pr.number}: comment sent-back",
                    lambda n=pr.number, c=cycle: gh_comment("pr", n, f"Sent back to the developer (cycle {c}/{max_cycles})."))
            s["blocked_handled"] = True
            s["round"] = int(s.get("round", 0)) + 1
            state.save()
            continue
        if s.pop("blocked_handled", None) is not None:
            state.save()

        rnd = int(s.get("round", 0))

        # --- tested: reviewer ----------------------------------------------------------------
        if LABEL_TESTED in pr.labels:
            if s.get("reviewer_round") == rnd:
                _watch_session(pr, s, "reviewer", wt, d, cfg, state, agent)
                continue
            if wt is None:
                log.warning("PR #%s: no worktree for branch %s; cannot spawn reviewer", pr.number, pr.head)
                notify_human(cfg, state, f"pr:{pr.number}:no-worktree", f"PR #{pr.number}: no Orca worktree for {pr.head}",
                             "Open a worktree on that branch or review by hand.", "pr", pr.number, None)
                continue
            brief = write_brief(f"pr-{pr.number}-reviewer-r{rnd}", reviewer_brief(pr, issue, rnd, is_docs_only(pr, cfg)))
            def _rev(wt=wt, brief=brief, pr=pr):
                return orca_spawn_terminal(wt, f"reviewer PR#{pr.number}", agent, one_liner(brief))
            handle = act(f"PR #{pr.number}: spawn reviewer (round {rnd + 1})", _rev)
            if DRY_RUN or handle:
                s["reviewer_round"], s["reviewer_terminal"], s["reviewer_started"] = rnd, handle, now_ms()
                s.pop("reviewer_nudged", None)
                state.save()
                act(f"PR #{pr.number}: comment reviewer spawned",
                    lambda n=pr.number: gh_comment("pr", n, "Reviewer dispatched."))
            continue

        # --- no state label: tester (or auto-tested) -----------------------------------------
        if is_docs_only(pr, cfg) or is_trivial(issue, cfg):
            why = "docs-only change" if is_docs_only(pr, cfg) else "issue labelled `trivial`"
            if s.get("auto_tested_round") != rnd:
                act(f"PR #{pr.number}: {why} -> state:tested",
                    lambda n=pr.number, w=why: (gh_label("pr", n, add=[LABEL_TESTED]),
                                                gh_comment("pr", n, f"Tester skipped ({w}); the Reviewer still reads every line.")))
                s["auto_tested_round"] = rnd
                state.save()
            continue
        if s.get("tester_round") == rnd:
            _watch_session(pr, s, "tester", wt, d, cfg, state, agent)
            continue
        if wt is None:
            notify_human(cfg, state, f"pr:{pr.number}:no-worktree", f"PR #{pr.number}: no Orca worktree for {pr.head}",
                         "Open a worktree on that branch or test by hand.", "pr", pr.number, None)
            continue
        brief = write_brief(f"pr-{pr.number}-tester-r{rnd}", tester_brief(pr, issue, rnd))
        def _test(wt=wt, brief=brief, pr=pr):
            return orca_spawn_terminal(wt, f"tester PR#{pr.number}", agent, one_liner(brief))
        handle = act(f"PR #{pr.number}: spawn tester (round {rnd + 1})", _test)
        if DRY_RUN or handle:
            s["tester_round"], s["tester_terminal"], s["tester_started"] = rnd, handle, now_ms()
            s.pop("tester_nudged", None)
            # remember the developer's session so blocked feedback goes to the right tab
            if wt and not s.get("developer_terminal"):
                terms = terminals_for(wt)
                others = [t for t in terms if t.get("handle") != handle]
                if others:
                    s["developer_terminal"] = max(others, key=lambda t: t.get("lastOutputAt") or 0).get("handle")
            state.save()
            act(f"PR #{pr.number}: comment tester spawned",
                lambda n=pr.number: gh_comment("pr", n, "Tester dispatched."))


def _watch_session(pr: PR, s: dict[str, Any], role: str, wt: Optional[Worktree], d: dict[str, Any],
                   cfg: dict[str, Any], state: State, agent: str) -> None:
    """A tester/reviewer was spawned and the PR has not moved. Idle -> nudge once -> needs-human."""
    if wt is None:
        return
    terms = terminals_for(wt)
    handle = s.get(f"{role}_terminal")
    mine = [t for t in terms if t.get("handle") == handle]
    last = last_output_ms(mine) if mine else (s.get(f"{role}_started") if not terms else last_output_ms(terms))
    idle = minutes_since(last)
    if not s.get(f"{role}_nudged"):
        if idle >= float(d["idle_minutes_before_nudge"]):
            rnd = int(s.get("round", 0))
            issue = None
            brief_text = (tester_brief(pr, issue, rnd) if role == "tester"
                          else reviewer_brief(pr, issue, rnd, is_docs_only(pr, cfg)))
            brief = write_brief(f"pr-{pr.number}-{role}-r{rnd}-nudge", brief_text)
            text = (f"The dispatcher sees no outcome on PR #{pr.number} yet. {one_liner(brief)} "
                    f"Finish with the label/merge step; if you need the human add `needs-human` and wait.")
            act(f"PR #{pr.number}: {role} idle {idle:.0f} min -> nudge",
                lambda: orca_deliver(wt, f"{role} PR#{pr.number}", agent, text, handle))
            s[f"{role}_nudged"] = now_ms()
            state.save()
    elif minutes_since(s[f"{role}_nudged"]) >= float(d["idle_minutes_after_nudge"]) and \
            idle >= float(d["idle_minutes_after_nudge"]):
        act(f"PR #{pr.number}: {role} still silent after nudge -> needs-human",
            lambda n=pr.number: gh_label("pr", n, add=[LABEL_NEEDS_HUMAN]))
        s.pop(f"{role}_nudged", None)
        state.save()


# --------------------------------------------------------------------------- reconcile: merged


def reconcile_merged(obs: Observed, cfg: dict[str, Any], state: State) -> None:
    d = cfg["dispatcher"]
    wt_by_branch = {wt.branch: wt for wt in obs.worktrees}
    for pr in obs.merged:
        n = pr.issue_number
        issue = obs.issues.get(n) if n else None
        if issue and issue.state == "OPEN" and n not in state.closed_issues:
            act(f"PR #{pr.number} merged -> close issue #{n}",
                lambda n=n, p=pr: run(["gh", "issue", "close", str(n), "--reason", "completed",
                                       "--comment", f"Merged into {cfg['branches']['base']} via PR #{p.number}."]))
            state.closed_issues.append(n)
            state.save()
        wt = wt_by_branch.get(pr.head)
        if wt and d.get("cleanup_worktrees_on_merge", True):
            def _rm(wt=wt):
                orca_note(wt, "merged", "completed")
                return orca_json(["worktree", "rm", "--worktree", f"id:{wt.id}", "--force"])
            act(f"PR #{pr.number} merged -> remove worktree {wt.name}", _rm)
        if str(pr.number) in state.prs:
            state.prs.pop(str(pr.number), None)
            state.save()


# --------------------------------------------------------------------------- tick & loop


def tick(cfg: dict[str, Any], state: State) -> None:
    obs = observe(cfg)
    log.debug("observed: %s issues, %s open PRs, %s merged PRs, %s worktrees; gate: %s",
              len(obs.issues), len(obs.prs), len(obs.merged), len(obs.worktrees), obs.gate_reason)
    reconcile_merged(obs, cfg, state)
    reconcile_prs(obs, cfg, state)
    reconcile_issues(obs, cfg, state)


def cmd_run(cfg: dict[str, Any], state: State, interval: int) -> int:
    if not acquire_lock():
        log.error("another dispatcher holds %s; refusing to start", LOCK_FILE)
        return 2
    log.info("dispatcher running (interval %ss, dry-run=%s)", interval, DRY_RUN)
    try:
        while not _stop:
            started = time.time()
            try:
                tick(cfg, state)
            except Exception:  # noqa: BLE001 -- the loop must never die
                log.exception("tick failed")
            remaining = max(1.0, interval - (time.time() - started))
            while remaining > 0 and not _stop:
                time.sleep(min(1.0, remaining))
                remaining -= 1
    finally:
        release_lock()
    log.info("dispatcher stopped")
    return 0


def cmd_once(cfg: dict[str, Any], state: State) -> int:
    if not acquire_lock():
        log.error("another dispatcher is running; skipping this tick")
        return 2
    try:
        tick(cfg, state)
    finally:
        release_lock()
    return 0


def cmd_status(cfg: dict[str, Any], state: State) -> int:
    obs = observe(cfg)
    print(f"gate: {'OPEN' if obs.gate_open else 'CLOSED'} -- {obs.gate_reason}")
    wt_by_issue = {wt.issue_number: wt for wt in obs.worktrees if wt.issue_number}
    pr_by_issue = {pr.issue_number: pr for pr in obs.prs if pr.issue_number}
    print("\nISSUES")
    for issue in sorted(obs.issues.values(), key=lambda i: i.number):
        if issue.state != "OPEN":
            continue
        flags = []
        if LABEL_READY in issue.labels: flags.append("ready")
        if LABEL_NEEDS_HUMAN in issue.labels: flags.append("NEEDS-HUMAN")
        if LABEL_ESCALATED in issue.labels: flags.append("ESCALATED")
        deps = issue.depends_on
        unmet = [n for n in deps if n not in obs.issues or obs.issues[n].state != "CLOSED"]
        where = "in PR #%s" % pr_by_issue[issue.number].number if issue.number in pr_by_issue else \
                ("worktree %s" % wt_by_issue[issue.number].name if issue.number in wt_by_issue else "queued")
        cyc = state.issues.get(str(issue.number), {}).get("cycle", 0)
        print(f"  #{issue.number:<4} {issue.role or '?':<10} {where:<22} cycle={cyc} "
              f"{'waits on ' + str(unmet) if unmet else ''} {' '.join(flags)}  {issue.title[:50]}")
    print("\nPULL REQUESTS (base %s)" % cfg["branches"]["base"])
    for pr in sorted(obs.prs, key=lambda p: p.number):
        s = state.prs.get(str(pr.number), {})
        stage = "blocked" if LABEL_BLOCKED in pr.labels else ("tested->reviewer" if LABEL_TESTED in pr.labels else "awaiting tester")
        if LABEL_NEEDS_HUMAN in pr.labels: stage = "NEEDS-HUMAN"
        print(f"  #{pr.number:<4} {stage:<18} round={s.get('round', 0)} {pr.head:<32} {pr.title[:45]}")
    print("\nWORKTREES")
    for wt in obs.worktrees:
        terms = terminals_for(wt)
        idle = minutes_since(last_output_ms(terms)) if terms else float('inf')
        print(f"  {wt.name:<24} {wt.branch:<32} sessions={len(terms)} idle={'-' if idle == float('inf') else f'{idle:.0f}m'}")
    return 0


def cmd_doctor(cfg: dict[str, Any], fix: bool) -> int:
    problems = 0

    def ok(msg: str) -> None:
        print(f"  OK   {msg}")

    def bad(msg: str) -> None:
        nonlocal problems
        problems += 1
        print(f"  FAIL {msg}")

    print("python / yaml");  ok(f"python {sys.version.split()[0]}, pyyaml {yaml.__version__}")
    print("gh")
    okk, out, err = run(["gh", "auth", "status"])
    ok("gh authenticated") if okk else bad("gh not authenticated: " + err.strip()[:200])
    repo = gh_json(["repo", "view", "--json", "nameWithOwner,defaultBranchRef"])
    if repo:
        ok(f"repo {repo['nameWithOwner']} (default branch {repo['defaultBranchRef']['name']})")
    else:
        bad("gh cannot resolve this repository (run inside the clone, with a GitHub remote)")
    base = cfg["branches"]["base"]
    okk, _, _ = run(["git", "-C", str(REPO_ROOT), "ls-remote", "--exit-code", "--heads", "origin", base])
    ok(f"branch {base} exists on origin") if okk else bad(f"branch {base} missing on origin: git push -u origin {base}")
    print("labels")
    have = {l["name"] for l in gh_json(["label", "list", "--limit", "200", "--json", "name"]) or []}
    for name, (desc, color) in REQUIRED_LABELS.items():
        if name in have:
            continue
        if fix:
            okk, _, err = run(["gh", "label", "create", name, "--description", desc, "--color", color])
            ok(f"created label {name}") if okk else bad(f"could not create label {name}: {err.strip()[:120]}")
        else:
            bad(f"label missing: {name}  (run: doctor --fix)")
    if all(n in have for n in REQUIRED_LABELS):
        ok("all workflow labels present")
    print("orca")
    st = orca_json(["status"])
    if st and (st.get("runtime") or {}).get("reachable"):
        ok("orca runtime reachable")
    else:
        bad("orca runtime not reachable: open Orca (or `orca open`)")
    rep = orca_json(["repo", "show", "--repo", f"path:{REPO_ROOT}"])
    if rep:
        ref = (rep.get("repo") or {}).get("worktreeBaseRef")
        ok(f"repo registered in Orca (base ref {ref})") if ref == base else \
            bad(f"Orca base ref is {ref!r}; run: orca repo set-base-ref --repo path:{REPO_ROOT} --ref {base}")
    else:
        bad(f"repo not registered in Orca: orca repo add --path {REPO_ROOT}")
    print("roles (host-level, ~/.orca/roles)")
    for r in ROLE_FILES:
        p = ROLES_DIR / f"{r}.md"
        ok(p.as_posix()) if p.exists() else bad(f"missing {p.as_posix()} -- seed from section 7 of agent-workflow-setup.md")
    print("skills (~/.claude/skills)")
    wanted: set[str] = set()
    for r in (cfg.get("roles") or {}).values():
        wanted.update(r.get("skills", []) or [])
    for l in (cfg.get("labels") or {}).values():
        wanted.update(l.get("skills", []) or [])
    missing = sorted(s for s in wanted if not (Path.home() / ".claude" / "skills" / s).exists())
    ok(f"{len(wanted) - len(missing)}/{len(wanted)} skills installed") if not missing else \
        bad(f"skills missing: {missing}  (run: bash .orca/setup_skills.sh)")
    print("gate")
    g_ok, g_why = core_document_gate(cfg)
    (ok if g_ok else bad)(g_why)
    print("state")
    try:
        HERE.mkdir(parents=True, exist_ok=True); (HERE / ".write-test").write_text("x"); (HERE / ".write-test").unlink()
        ok(f"{HERE.as_posix()} writable")
    except OSError as exc:
        bad(f"{HERE} not writable: {exc}")
    print(f"\n{problems} problem(s)")
    return 1 if problems else 0


def cmd_onboard(cfg: dict[str, Any]) -> int:
    base = cfg["branches"]["base"]
    brief = write_brief("onboarding", onboarding_brief(base))
    res = act("create onboarding worktree with the PO & Analyst",
              lambda: orca_create_worktree("onboarding", base, None, cfg["dispatcher"]["agent"], one_liner(brief), activate=True))
    if DRY_RUN:
        return 0
    if not res:
        print("failed: is Orca open and the repo registered? run: dispatch.py doctor")
        return 1
    print("onboarding worktree created; switch to Orca and talk to the PO & Analyst.")
    return 0


def setup_logging(verbose: bool) -> None:
    log.setLevel(logging.DEBUG if verbose else logging.INFO)
    fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s")
    HERE.mkdir(parents=True, exist_ok=True)
    fh = RotatingFileHandler(LOG_FILE, maxBytes=2_000_000, backupCount=3, encoding="utf-8")
    fh.setFormatter(fmt)
    log.addHandler(fh)
    if sys.stderr is not None:  # pythonw has no console
        sh = logging.StreamHandler(sys.stderr)
        sh.setFormatter(fmt)
        log.addHandler(sh)


def main(argv: Optional[list[str]] = None) -> int:
    global DRY_RUN
    p = argparse.ArgumentParser(description="ORCA ADE dispatcher (reconciler).")
    p.add_argument("command", nargs="?", default="run", choices=["run", "once", "status", "doctor", "onboard"])
    p.add_argument("--once", action="store_true", help="alias for the `once` command")
    p.add_argument("--interval", type=int, default=None)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--fix", action="store_true", help="doctor: create missing labels")
    p.add_argument("--verbose", "-v", action="store_true")
    a = p.parse_args(argv)
    DRY_RUN = a.dry_run
    setup_logging(a.verbose)
    signal.signal(signal.SIGINT, _on_signal)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, _on_signal)
    cfg = load_config()
    cmd = "once" if a.once else a.command
    if cmd == "doctor":
        return cmd_doctor(cfg, a.fix)
    if cmd == "onboard":
        return cmd_onboard(cfg)
    state = State.load()
    if cmd == "status":
        return cmd_status(cfg, state)
    if cmd == "once":
        return cmd_once(cfg, state)
    return cmd_run(cfg, state, a.interval or int(cfg["dispatcher"]["poll_interval_seconds"]))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
