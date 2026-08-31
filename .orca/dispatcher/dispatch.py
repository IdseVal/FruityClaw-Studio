#!/usr/bin/env python3
"""Dispatcher v3 -- the reconciler that runs the ORCA ADE agent workflow.

THE TWO IDEAS
    1. The dispatcher never trusts an agent to report back. Every tick it OBSERVES
       reality (GitHub issues, PRs, labels; the liveness of the processes it spawned;
       Orca worktrees), COMPUTES what the workflow rules say should be true, and DOES the
       difference. Every action is idempotent: crashes, restarts and double runs are
       harmless.
    2. GitHub is the memory; a run is disposable. An agent run is a headless
       `claude -p` process that starts, does one job, and exits. Nothing waits. A blocked
       PR gets a FRESH run whose brief contains the blocker's comments. A run that needs
       the human says so on the issue/PR, labels it `needs-human`, and ends; the human
       answers in the comments and removes the label, and a fresh run reads them.

WHERE IT RUNS
    On the HOST, because it drives the `orca` CLI (worktrees) and spawns local `claude`
    processes. Orca is a desktop app: GitHub webhooks have nowhere to land, so the
    dispatcher POLLS `gh` (60s tick). Install it as a scheduled task (install-task.ps1)
    so it survives reboots and is independent of any chat session.

WHAT IT DOES EACH TICK
    Issues
      - Gate: no issue is dispatched while `docs/CORE_DOCUMENT.md` on `dev` carries the
        EMPTY marker (the Planner's onboarding PR opens the gate) or the ACHIEVED marker
        (project complete; a revision interview reopens it).
      - An open issue labelled `ready`, with none of `escalated`/`needs-human`, whose
        `Depends on: #a, #b` issues are all closed, with no PR yet and no live run, is
        dispatched (up to `max_active_issues` live runs at a time): an Orca worktree off
        `dev` with NO terminal, a brief file, and a headless `claude -p` process in that
        directory whose stdout goes to `.orca/dispatcher/runs/`. The issue's labels pick
        the role the run loads: `architecture` -> the Architect, `research` -> the
        Planner, otherwise the Implementer.
      - A run past `max_run_minutes` is killed (process tree). A run that exits without a
        PR gets one fresh retry, then the issue is labelled `needs-human`. Every start
        spends a breaker cycle.
      - `proposed` issues are invisible to dispatch: promoting them to `ready` is the
        human's move (or the Planner's, in `auto` mode).
    PRs (base `dev`; the CI pipeline owns testing and review)
      - `state:blocked` -> spend a cycle; under the breaker, spawn a fresh headless fix
        run in the PR's worktree whose brief embeds the blocker's comments; over it,
        label the issue `escalated` and page the human. The fix run pushes and removes
        the label; the push re-triggers CI.
      - `needs-human` on an issue or PR -> flag it once with a GitHub comment (an
        @mention only if configured; the dispatcher itself NEVER e-mails -- the daily
        digest is the mail channel) and touch nothing until the label is gone.
      - Merged -> close the linked issue (merges into `dev` do NOT auto-close; only the
        default branch does), remove the worktree, kill any lingering run.
    Backlog (closing the loop; honours the autonomy dial)
      - Drained (no open issues, no open PRs into `dev`, no live runs, no worktrees),
        gate open, autonomy != manual -> spawn a headless Planner backlog audit. Its
        outcome is read from GitHub: new issues (`proposed` or `ready` per the dial), or
        a PR flipping the core document to ACHIEVED. An audit that produces neither while
        nothing has merged since the last one pages the human instead of looping.
      - ACHIEVED on `dev` -> dispatching stops, the human is paged once;
        `dispatch.py onboard` starts a revision interview that reopens the gate.
    Pause (the human's brake, per-project)
      - `.orca/dispatcher/paused.json` (written by `pause`, removed by `resume`) is read
        every tick. While present, no issue is dispatched and no audit is spawned; fix
        runs, merges, issue closing and cleanup continue, so the fleet DRAINS.

STATE
    `state.json` holds only de-duplication memory (which run/comment/page already
    happened, cycle counts). Truth lives in GitHub and in process liveness; deleting the
    state file costs at most a duplicate comment -- and orphans nothing, because every
    tracked PID is re-checked against the OS, not against memory.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import logging
import os
import re
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
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
PAUSE_FILE = HERE / "paused.json"
LOG_FILE = HERE / "dispatcher.log"
PROMPTS_DIR = HERE / "prompts"
RUNS_DIR = HERE / "runs"

# Roles and skills travel WITH the repository in v0.2 (CI needs them too).
ROLES_DIR = REPO_ROOT / ".orca" / "roles"
ROLE_FILES = ["planner", "architect", "implementer"]
SKILLS_DIR = REPO_ROOT / ".claude" / "skills"
PIPELINE_FILE = REPO_ROOT / ".github" / "workflows" / "agent-pipeline.yml"

LABEL_READY = "ready"
LABEL_PROPOSED = "proposed"
LABEL_ESCALATED = "escalated"
LABEL_NEEDS_HUMAN = "needs-human"
LABEL_TESTED = "state:tested"
LABEL_BLOCKED = "state:blocked"
LABEL_TRIVIAL = "trivial"

# Every label the workflow uses. `doctor --fix` creates the missing ones.
REQUIRED_LABELS: dict[str, tuple[str, str]] = {
    LABEL_READY: ("Approved for dispatch; the dispatcher picks it up", "0E8A16"),
    LABEL_PROPOSED: ("Filed by the Planner, awaiting the owner's promotion to ready", "FBCA04"),
    LABEL_ESCALATED: ("Circuit breaker tripped; a human must resolve", "B60205"),
    LABEL_NEEDS_HUMAN: ("An agent needs the human: answer in the comments, then remove this label", "D93F0B"),
    LABEL_TESTED: ("Verifier passed (or docs-only/trivial); informational", "0E8A16"),
    LABEL_BLOCKED: ("Verifier or Reviewer sent the PR back; a fix run is dispatched", "D93F0B"),
    LABEL_TRIVIAL: ("Skips the Verifier, never the Reviewer", "C5DEF5"),
    "research": ("Issue type: routed to the Planner; facts + evidence as the deliverable", "5319E7"),
    "architecture": ("Issue type: routed to the Architect; contracts and ADRs", "5319E7"),
    "ui": ("Interface work; brief points at the design skills", "0052CC"),
    "seo": ("Search work; brief points at seo-audit", "C5DEF5"),
    "scraper": ("External data acquisition; brief points at just-scrape", "E99695"),
    "bug": ("Defect; brief points at diagnosing-bugs", "D73A4A"),
    "data": ("Data or pipeline; evidence gate applies", "C5DEF5"),
}

SUBPROCESS_TIMEOUT = 180
STILL_ACTIVE = 259
PR_GRACE_MINUTES = 5          # a run whose PR already exists gets this long to finish talking

_ISSUE_RE = re.compile(r"issue-(\d+)")
_DEPENDS_RE = re.compile(r"(?i)\b(?:depends\s+on|blocked\s+by)\b[^\n]*")
_HASH_RE = re.compile(r"#(\d+)")
_CLOSES_RE = re.compile(r"(?i)\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?|refs?)\s+#(\d+)")

log = logging.getLogger("dispatcher")
_stop = False
DRY_RUN = False
_pause_logged: Optional[str] = None

# Popen objects for runs started by THIS dispatcher process; polled to reap children.
# After a dispatcher restart the registry is empty and liveness falls back to the OS.
PROCS: dict[int, subprocess.Popen] = {}


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
    """Run a command to completion; never raise. Returns (ok, stdout, stderr)."""
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    try:
        proc = subprocess.run(
            argv, check=False, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=timeout,
            creationflags=flags,
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


def reap() -> None:
    """Poll every child this process started, so exited runs never linger as zombies."""
    for pid, proc in list(PROCS.items()):
        if proc.poll() is not None:
            PROCS.pop(pid, None)


def run_alive(pid: int) -> bool:
    proc = PROCS.get(pid)
    if proc is not None:
        return proc.poll() is None
    return pid_alive(pid)


def kill_tree(pid: int) -> None:
    """Kill a run and everything it spawned. Runs are started as their own group/tree."""
    if os.name == "nt":
        run(["taskkill", "/PID", str(pid), "/T", "/F"], timeout=30)
    else:
        try:
            os.killpg(pid, signal.SIGTERM)
            time.sleep(2)
            if pid_alive(pid):
                os.killpg(pid, signal.SIGKILL)
        except OSError as exc:
            log.debug("killpg %s: %s", pid, exc)
    PROCS.pop(pid, None)


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


# --------------------------------------------------------------------------- pause


@dataclass
class Pause:
    """A soft pause on this project: start no new work, finish the work in flight.

    Blocked while it exists: dispatching a `ready` issue, spawning a backlog audit --
    the only two places the dispatcher creates NEW work. Untouched: fix runs for blocked
    PRs, merge bookkeeping, issue closing, worktree cleanup and every notification, so
    the fleet drains to empty and stays there.
    """

    mode: str
    reason: str
    since: int

    def describe(self) -> str:
        mins = minutes_since(self.since)
        ago = f"{mins:.0f}m" if mins < 90 else f"{mins / 60:.1f}h"
        return f"PAUSED ({self.mode}) for {ago}" + (f" -- {self.reason}" if self.reason else "")


def read_pause() -> Optional[Pause]:
    """The pause marker, or None. An unreadable marker counts as paused: a corrupt file
    must never be the reason a fleet quietly starts spending quota again."""
    if not PAUSE_FILE.exists():
        return None
    try:
        raw = json.loads(PAUSE_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        log.warning("pause marker unreadable (%s); treating this project as paused", exc)
        return Pause("soft", "unreadable pause marker", now_ms())
    return Pause(str(raw.get("mode") or "soft"), str(raw.get("reason") or ""),
                 int(raw.get("since") or now_ms()))


def write_pause(reason: str) -> Pause:
    pause = Pause("soft", reason.strip(), now_ms())
    PAUSE_FILE.parent.mkdir(parents=True, exist_ok=True)
    tmp = PAUSE_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps({"mode": pause.mode, "reason": pause.reason, "since": pause.since},
                              indent=2), encoding="utf-8")
    tmp.replace(PAUSE_FILE)
    return pause


def clear_pause() -> Optional[Pause]:
    was = read_pause()
    try:
        PAUSE_FILE.unlink(missing_ok=True)
    except OSError as exc:
        log.error("could not remove %s: %s -- the project is still paused", PAUSE_FILE, exc)
        raise
    return was


# --------------------------------------------------------------------------- config & state


AUTONOMY_MODES = ("manual", "propose", "auto")


def load_config() -> dict[str, Any]:
    with CONFIG_FILE.open("r", encoding="utf-8") as fh:
        cfg = yaml.safe_load(fh) or {}
    d = cfg.setdefault("dispatcher", {})
    d.setdefault("poll_interval_seconds", 60)
    d.setdefault("max_active_issues", 3)
    d.setdefault("max_run_minutes", 30)
    d.setdefault("retry_empty_run", True)
    d.setdefault("cleanup_worktrees_on_merge", True)
    d.setdefault("claude_cmd", "claude")
    d.setdefault("permission_args", ["--dangerously-skip-permissions"])
    d.setdefault("extra_args", [])
    d.setdefault("interactive_agent", "claude")
    d.setdefault("notify", {})
    g = cfg.setdefault("gates", {}).setdefault("core_document", {})
    g.setdefault("path", "docs/CORE_DOCUMENT.md")
    g.setdefault("empty_marker", "Status: EMPTY")
    g.setdefault("achieved_marker", "Status: ACHIEVED")
    cfg.setdefault("branches", {}).setdefault("base", "dev")
    cfg.setdefault("circuit_breaker", {}).setdefault("max_cycles", 3)
    cfg.setdefault("labels", {})
    r = cfg.setdefault("roles", {})
    for role, base in (("planner", ["handoff", "grill-with-docs", "domain-modeling"]),
                       ("architect", ["handoff", "improve-codebase-architecture", "codebase-design"]),
                       ("implementer", ["handoff", "tdd"])):
        r.setdefault(role, {}).setdefault("skills", list(base))
    # `autonomy` deliberately has NO default: absence means "ask the owner at onboard".
    return cfg


def autonomy_of(cfg: dict[str, Any]) -> Optional[str]:
    mode = cfg.get("autonomy")
    return mode if mode in AUTONOMY_MODES else None


def write_autonomy(mode: str) -> None:
    """Persist the chosen mode into dispatch.yml with a targeted text edit, so the
    file's comments and layout survive."""
    text = CONFIG_FILE.read_text(encoding="utf-8")
    lines = text.splitlines()
    out: list[str] = []
    replaced = False
    for line in lines:
        if line.strip().startswith("autonomy:") and not line.lstrip().startswith("#"):
            out.append(f"autonomy: {mode}")
            replaced = True
        else:
            out.append(line)
    if not replaced:
        inserted = False
        for i, line in enumerate(out):
            if line.startswith("version:"):
                out.insert(i + 1, f"autonomy: {mode}")
                inserted = True
                break
        if not inserted:
            out.insert(0, f"autonomy: {mode}")
    CONFIG_FILE.write_text("\n".join(out) + "\n", encoding="utf-8")


@dataclass
class State:
    issues: dict[str, dict[str, Any]] = field(default_factory=dict)
    prs: dict[str, dict[str, Any]] = field(default_factory=dict)
    notified: dict[str, int] = field(default_factory=dict)
    closed_issues: list[int] = field(default_factory=list)
    backlog: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def load(cls) -> "State":
        if not STATE_FILE.exists():
            return cls()
        try:
            raw = json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            log.warning("state unreadable (%s); starting fresh", exc)
            return cls()
        return cls(
            issues=raw.get("issues", {}), prs=raw.get("prs", {}),
            notified=raw.get("notified", {}), closed_issues=raw.get("closed_issues", []),
            backlog=raw.get("backlog", {}),
        )

    def save(self) -> None:
        if DRY_RUN:
            return
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = STATE_FILE.with_suffix(".tmp")
        tmp.write_text(json.dumps({
            "issues": self.issues, "prs": self.prs,
            "notified": self.notified, "closed_issues": self.closed_issues,
            "backlog": self.backlog,
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
    branch: str
    name: str
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
    achieved: bool
    repo_id: Optional[str]
    pause: Optional[Pause]


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
                            "--json", "number,title,body,labels,url,headRefName,isDraft,state,mergedAt"]) or []:
            out.append(PR(
                number=int(raw["number"]), title=raw.get("title", ""), body=raw.get("body") or "",
                labels={l["name"] for l in raw.get("labels", [])}, url=raw.get("url", ""),
                head=raw.get("headRefName", ""), is_draft=bool(raw.get("isDraft")),
                state=raw.get("state", state.upper()), merged_at=raw.get("mergedAt"),
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

    gate_open, gate_reason, achieved = core_document_gate(cfg)
    return Observed(issues, prs, merged, worktrees, gate_open, gate_reason, achieved, repo_id,
                    read_pause())


def core_document_gate(cfg: dict[str, Any]) -> tuple[bool, str, bool]:
    """(gate_open, reason, achieved). Achieved closes the gate: the project is complete
    until a revision interview sets the status back to AGREED."""
    g = cfg["gates"]["core_document"]
    base = cfg["branches"]["base"]
    run(["git", "-C", str(REPO_ROOT), "fetch", "-q", "origin", base], timeout=60)
    ok, out, _ = run(["git", "-C", str(REPO_ROOT), "show", f"origin/{base}:{g['path']}"])
    if not ok:
        return False, f"{g['path']} not found on origin/{base}", False
    if g["empty_marker"] in out:
        return False, f"{g['path']} on {base} still says '{g['empty_marker']}' -- run the onboarding interview", False
    if g["achieved_marker"] in out:
        return False, (f"{g['path']} on {base} says '{g['achieved_marker']}' -- project complete; "
                       "run `dispatch.py onboard` for a revision interview to start a new round"), True
    return True, "core document present on " + base, False

# --------------------------------------------------------------------------- briefs


def write_brief(name: str, text: str) -> Path:
    PROMPTS_DIR.mkdir(parents=True, exist_ok=True)
    path = PROMPTS_DIR / f"{name}.md"
    if not DRY_RUN:
        path.write_text(text, encoding="utf-8")
    return path


def one_liner(brief: Path) -> str:
    """What the agent actually receives: one line pointing at a file. Long prompts get
    truncated on the CLI path; this never does, and the file stays on disk for triage."""
    return f"Read the file {brief.as_posix()} and follow its instructions exactly."


def role_for(issue: Issue) -> str:
    """The issue's labels choose which role prompt a headless run loads."""
    if "architecture" in issue.labels:
        return "architect"
    if "research" in issue.labels:
        return "planner"
    return "implementer"


def skills_for(role: str, labels: set[str], cfg: dict[str, Any]) -> list[str]:
    out: list[str] = []
    for s in ((cfg.get("roles", {}).get(role) or {}).get("skills", []) or []):
        if s not in out:
            out.append(s)
    label_map = cfg.get("labels", {}) or {}
    for label in sorted(labels):
        for s in (label_map.get(label) or {}).get("skills", []) or []:
            if s not in out:
                out.append(s)
    return out


COMMON_RULES = """
Rules that apply to every run:
- You are STRICTLY FORBIDDEN from merging anything into `main`. Only the CI Reviewer merges, and only into `dev`.
- You run headless. Nothing you print reaches anyone. Anything a human or a later run must know goes on GitHub (`gh issue comment` / `gh pr comment`).
- If you need the human (a decision, a credential, a missing skill): comment on the issue or PR with exactly what you need and what you will do with the answer, add the label `needs-human`, push any work worth keeping, and END YOUR RUN. A fresh run will later read those comments -- write for that reader.
- Skills live in `.claude/skills/` of this repository. If one your brief names is missing, say so in a comment and continue with plain tools if you can; otherwise use `needs-human`.
- Never leave work uncommitted when you end. Never force-push. Never wait for anything.
"""


def issue_brief(issue: Issue, role: str, cycle: int, max_cycles: int, skills: list[str], base: str) -> str:
    types = sorted(issue.labels & {"research", "architecture", "bug"})
    return f"""# Dispatch brief -- issue #{issue.number} -- {role} -- cycle {cycle}/{max_cycles}

You are the **{role}** for issue #{issue.number}: {issue.title}
Worktree: this directory, on its own branch off `{base}`. Issue type labels: {', '.join(types) or '(build)'}

Do these in order:
1. Read your role: `cat .orca/roles/{role}.md`. Follow it exactly.
2. Read the task: `gh issue view {issue.number} --comments`. If the issue does not give
   you enough context to know what is wanted and WHY -- without guessing -- do not build
   your best guess: comment on the issue naming exactly what is missing, add the label
   `needs-human`, and END YOUR RUN.
3. Read `docs/CORE_DOCUMENT.md`, then the relevant `docs/specs/*.md` and `docs/adrs/*.md`.
4. Do ONLY what the issue asks, in this worktree. If your role builds code, its tests
   ship in the same PR and you run everything that exists before publishing; if your
   role produces documents, they land under `docs/` per your role file. Commit in
   small, clear commits.
5. Finish by publishing a pull request into `{base}`:
       git push -u origin HEAD
       gh pr create --base {base} --title "<what you did> (#{issue.number})" --body "<summary, evidence, decisions>. Refs #{issue.number}"
   The CI pipeline verifies and reviews it; the dispatcher closes the issue on merge.
6. Say DONE in one line and END YOUR RUN. Do not wait. Do not merge.

Skills earned by this issue's labels: {', '.join(skills) if skills else '(baseline only)'}
Labels: {', '.join(sorted(issue.labels)) or '(none)'}
Issue URL: {issue.url}
{COMMON_RULES}
"""


def retry_brief(issue: Issue, role: str, base: str) -> str:
    return f"""# Retry -- issue #{issue.number} -- {role}

A previous run for issue #{issue.number} ended WITHOUT publishing a pull request. You are
a fresh run with no memory of it. The branch in this worktree may already contain partial
committed work -- inspect `git log` and `git status` first.

- If the work is essentially done: finish it, `git push -u origin HEAD`,
  `gh pr create --base {base} --title "... (#{issue.number})" --body "... Refs #{issue.number}"`, END YOUR RUN.
- Otherwise: do the task from the top. `cat .orca/roles/{role}.md`,
  `gh issue view {issue.number} --comments`, the core document and specs; do the work
  per your role; push; open the PR into `{base}`; END YOUR RUN.
- If you can see WHY the previous run failed (a missing credential, an impossible
  instruction), do not repeat it: comment on the issue, add `needs-human`, END YOUR RUN.
{COMMON_RULES}
"""


def fix_brief(pr: PR, issue: Optional[Issue], role: str, cycle: int, max_cycles: int, comments: str) -> str:
    n = pr.number
    return f"""# Fix run -- PR #{n} was sent back -- cycle {cycle}/{max_cycles}

PR #{n} ({pr.title}) received `state:blocked` from the CI pipeline. You are a FRESH
{role} run; your memory is the pull request. The blocker's comments:

{comments}

Do this, in this worktree (the PR's branch is checked out here):
1. `gh pr view {n} --comments` and `git diff origin/dev...HEAD` for full context, plus
   `cat .orca/roles/{role}.md`.
2. Address EVERY point. Commit and `git push` on this branch. If the human answered an
   FMEA table or a `needs-human` question in the comments, their answers are
   requirements now.
3. Reply on the PR with what you changed: `gh pr comment {n} --body "..."`.
4. Hand it back: `gh pr edit {n} --remove-label state:blocked`. The push re-triggers the
   CI pipeline automatically.
5. If a point is wrong or impossible, say so on the PR, add `needs-human`, and END YOUR
   RUN -- do not argue in circles. After cycle {max_cycles} the circuit breaker escalates
   to the human.
6. END YOUR RUN.
{COMMON_RULES}
"""


AUDIT_WT_PREFIX = "backlog-audit"


def audit_brief(base: str, achieved_marker: str, autonomy: str) -> str:
    label_rule = ("label every issue you file `ready`"
                  if autonomy == "auto" else
                  "label every issue you file `proposed` -- NEVER `ready`; the human promotes them")
    return f"""# Backlog audit -- Planner (headless)

The pipeline is DRAINED: no open issues, no open PRs into `{base}`, no live runs.
Decide what happens next. Exactly ONE of two outcomes, and it must be visible on GitHub
-- a run that ends with neither stalls the whole project. Autonomy mode: `{autonomy}`.

1. `cat .orca/roles/planner.md` -- you are the Planner.
2. Re-read `docs/CORE_DOCUMENT.md`, every `docs/specs/*.md` and `docs/adrs/*.md`, and the
   closed issues (`gh issue list --state closed --limit 200`). Compare what the core
   document promises against what is actually merged on `{base}`.
3. OUTCOME A -- gaps remain: file the missing GitHub issues, written to the standard in
   your role: self-contained plain-language CONTEXT (what the problem is and why it
   exists), TASK, DONE WHEN (testable), OUT OF SCOPE -- no jargon the core document does
   not define. Type labels (`research`, `architecture`) and skill labels where they
   apply; `trivial` where honest; ordering ONLY via a body line `Depends on: #a, #b`.
   Per the autonomy mode: {label_rule}.
4. OUTCOME B -- everything the core document promises is merged AND the document contains
   no OPEN item: edit its status line to `{achieved_marker} -- <date>`, commit on this
   branch, `git push -u origin HEAD`, and open a PR into `{base}` titled
   "Core document achieved". You may NOT choose B while any OPEN item exists: file one
   issue per OPEN item, label it `needs-human`, and that is OUTCOME A.
5. State in one line which outcome you chose, then END YOUR RUN.
{COMMON_RULES}
"""


def onboarding_brief(base: str, autonomy: str) -> str:
    label_rule = ("label them `ready`" if autonomy == "auto"
                  else "label them `proposed`; the human promotes the ones they want to `ready`")
    return f"""# Onboarding -- Planner (interactive; the human is in this session)

You are the **Planner**. Read `cat .orca/roles/planner.md` and follow it exactly. This is
one of the few INTERACTIVE sessions in this workflow: the human is here, talk to them.
Autonomy mode for this project: `{autonomy}`.

Phase 1 -- the core document:
- `docs/CORE_DOCUMENT.md` is an empty template. Fill it by deep interview, in rounds,
  with the human in this session. Record, read back, ask what is wrong or missing.
  Unknowns are OPEN, never guessed.
- When the human says it is AGREED: replace the `Status: EMPTY` line with
  `Status: AGREED -- <date>`, commit, `git push -u origin HEAD`, and open a PR into `{base}`:
      gh pr create --base {base} --title "Populate core document from owner interview" --body "Agreed with the owner in this session."
  The CI pipeline reviews and merges it; nothing can be dispatched before it is on `{base}`.

Phase 2 -- specs, ADRs, issues (after that PR is merged):
- Derive `docs/specs/*.md` and ADRs as `docs/adrs/ADR-<issue#>-<slug>.md`, freeze the
  contracts feature work will depend on.
- Write the GitHub issues per your role's writing standard: self-contained plain-language
  context (what and why), testable acceptance criteria, type and skill labels,
  `Depends on:` lines for ordering. Per the autonomy mode: {label_rule}.
{COMMON_RULES}
"""


def revision_brief(base: str, achieved_marker: str, autonomy: str) -> str:
    label_rule = ("label them `ready`" if autonomy == "auto"
                  else "label them `proposed`; the human promotes the ones they want to `ready`")
    return f"""# Revision round -- Planner (interactive; the human is in this session)

The project already has an agreed core document on `{base}`. The human wants a NEW ROUND:
new features, changes, fixes, feedback from using the result. Autonomy mode: `{autonomy}`.

1. `cat .orca/roles/planner.md` -- you are the Planner.
2. Read `docs/CORE_DOCUMENT.md` (the `{base}` version), the specs and ADRs, and skim the
   closed issues so you know what already exists.
3. Interview the human IN THIS SESSION, in rounds: what should change, what is new, what
   should go. Record every answer in the core document, read it back, ask what is wrong
   or missing. Unknowns are OPEN items, never guesses.
4. When the human says AGREED: set the status line to `Status: AGREED -- <date>` (replacing
   `{achieved_marker} -- ...` if present), commit, `git push -u origin HEAD`, and open a PR
   into `{base}`. Merging that PR reopens the dispatch gate by itself.
5. After that PR merges: update specs and ADRs and file the new issues per your role's
   writing standard (self-contained plain-language context, testable done-when).
   Per the autonomy mode: {label_rule}.
{COMMON_RULES}
"""


# --------------------------------------------------------------------------- github actions (the CLI kind)


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


def gh_recent_comments(pr_number: int, limit: int = 6) -> str:
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
                 kind: str, number: Optional[int]) -> None:
    """Flag the item on GitHub, once per key. The dispatcher itself NEVER e-mails: the
    daily digest (section 11) is the mail channel, and an @mention is included only when
    notify.github_mention is set."""
    if key in state.notified:
        return
    mention = (cfg["dispatcher"].get("notify") or {}).get("github_mention", "") or ""
    text = ((mention + " ") if mention else "") + f"{subject}\n\n{body}"
    if number is not None:
        act(f"notify human: {subject}", lambda: gh_comment(kind, number, text))
    else:
        log.info("notify human (no GitHub target): %s", subject)
    state.notified[key] = now_ms()
    state.save()


# --------------------------------------------------------------------------- orca + spawning


def orca_create_worktree(name: str, base: str, issue_number: Optional[int],
                         agent: Optional[str] = None, prompt: Optional[str] = None,
                         activate: bool = False) -> Optional[dict[str, Any]]:
    """Create a worktree. WITHOUT `agent`, no terminal is started -- the v0.2 default:
    headless runs are plain subprocesses, Orca only provides the isolated checkout."""
    args = ["worktree", "create", "--repo", f"path:{REPO_ROOT}", "--name", name,
            "--base-branch", base]
    if agent:
        args += ["--agent", agent]
        if prompt:
            args += ["--prompt", prompt]
    if issue_number:
        args += ["--issue", str(issue_number)]
    if activate:
        args += ["--activate"]
    return orca_json(args)


def worktree_path(name: str, res: Optional[dict[str, Any]]) -> Optional[str]:
    """Resolve a worktree's path from the create result, else by listing."""
    if res:
        p = (res.get("worktree") or {}).get("path")
        if p:
            return p
    listing = orca_json(["worktree", "list", "--limit", "500"])
    for raw in (listing or {}).get("worktrees", []) or []:
        if (raw.get("displayName") or "") == name and raw.get("path"):
            return raw["path"]
    return None


def spawn_headless(workdir: str, brief: Path, log_name: str, cfg: dict[str, Any]) -> Optional[int]:
    """Start a run-to-completion `claude -p` process in the worktree. Returns the PID.

    The process is its own group/tree so kill_tree() can take everything with it, and its
    output goes to a per-run log -- the human's window into a headless run."""
    d = cfg["dispatcher"]
    cmd = [d["claude_cmd"], "-p", one_liner(brief), *d["permission_args"], *d["extra_args"]]
    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    log_path = RUNS_DIR / f"{log_name}.log"
    try:
        logf = open(log_path, "ab")
    except OSError as exc:
        log.error("cannot open run log %s: %s", log_path, exc)
        return None
    kwargs: dict[str, Any] = {}
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        kwargs["start_new_session"] = True
    try:
        proc = subprocess.Popen(
            cmd, cwd=workdir, stdin=subprocess.DEVNULL,
            stdout=logf, stderr=subprocess.STDOUT, **kwargs,
        )
    except (FileNotFoundError, OSError) as exc:
        log.error("cannot start %s: %s (set dispatcher.claude_cmd to an absolute path?)", cmd[0], exc)
        logf.close()
        return None
    finally:
        # The child holds its own handle; the parent's copy can close either way.
        try:
            logf.close()
        except OSError:
            pass
    PROCS[proc.pid] = proc
    return proc.pid


# --------------------------------------------------------------------------- reconcile: issues


def reconcile_issues(obs: Observed, cfg: dict[str, Any], state: State) -> None:
    d = cfg["dispatcher"]
    base = cfg["branches"]["base"]
    max_cycles = int(cfg["circuit_breaker"]["max_cycles"])
    max_minutes = float(d["max_run_minutes"])

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
                          if i.state == "OPEN" and i.number not in merged_issues),
                         key=lambda i: i.number)

    def live_runs() -> int:
        n = 0
        for s in state.issues.values():
            r = s.get("run")
            if r and run_alive(int(r.get("pid", 0))):
                n += 1
        return n

    slots = max(0, int(d["max_active_issues"]) - live_runs())

    for issue in open_issues:
        s = state.issue(issue.number)
        r = s.get("run")
        pr = pr_by_issue.get(issue.number)
        wt = wt_by_issue.get(issue.number)

        # needs-human: page once, then leave the item alone until the label is gone.
        if LABEL_NEEDS_HUMAN in issue.labels:
            notify_human(cfg, state, f"issue:{issue.number}:needs-human",
                         f"issue #{issue.number} needs you",
                         f"{issue.title}\nAnswer in the issue comments, then remove the "
                         f"`needs-human` label.\n{issue.url}", "issue", issue.number)
            continue
        state.notified.pop(f"issue:{issue.number}:needs-human", None)

        # --- a PR exists: the CI pipeline owns it; let the run finish talking, then reap.
        if pr is not None:
            if r:
                pid = int(r.get("pid", 0))
                if run_alive(pid):
                    seen = s.setdefault("pr_seen", now_ms())
                    if minutes_since(seen) >= PR_GRACE_MINUTES:
                        act(f"issue #{issue.number}: PR #{pr.number} exists; reaping lingering run {pid}",
                            lambda p=pid: kill_tree(p))
                        s.pop("run", None)
                else:
                    s.pop("run", None)
                s.pop("retried", None)
                state.save()
            continue
        s.pop("pr_seen", None)

        if LABEL_ESCALATED in issue.labels or LABEL_READY not in issue.labels:
            continue

        # --- a run is in flight -----------------------------------------------------------
        if r:
            pid = int(r.get("pid", 0))
            if run_alive(pid):
                if minutes_since(int(r.get("started", 0))) >= max_minutes:
                    act(f"issue #{issue.number}: run {pid} exceeded {max_minutes:.0f} min -> kill",
                        lambda p=pid: kill_tree(p))
                    s.pop("run", None)
                    if int(s.get("cycle", 0)) >= max_cycles:
                        act(f"issue #{issue.number}: breaker after timeout -> escalate",
                            lambda n=issue.number: (gh_label("issue", n, add=[LABEL_ESCALATED]),
                                                    gh_comment("issue", n, f"Circuit breaker tripped: {max_cycles} failed attempts (last run timed out).")))
                        notify_human(cfg, state, f"issue:{issue.number}:escalated",
                                     f"issue #{issue.number} escalated", issue.url, "issue", issue.number)
                    state.save()
                continue
            # run exited without a PR
            s.pop("run", None)
            if bool(d["retry_empty_run"]) and not s.get("retried"):
                cycle = int(s.get("cycle", 0)) + 1
                if cycle > max_cycles:
                    act(f"issue #{issue.number}: breaker -> escalate",
                        lambda n=issue.number: (gh_label("issue", n, add=[LABEL_ESCALATED]),
                                                gh_comment("issue", n, f"Circuit breaker tripped: {max_cycles} failed attempts.")))
                    notify_human(cfg, state, f"issue:{issue.number}:escalated",
                                 f"issue #{issue.number} escalated", issue.url, "issue", issue.number)
                    state.save()
                    continue
                if wt is None or not wt.path:
                    log.warning("issue #%s: retry wanted but no worktree; will re-dispatch fresh", issue.number)
                    s["retried"] = True
                    state.save()
                    continue
                brief = write_brief(f"issue-{issue.number}-retry-c{cycle}",
                                    retry_brief(issue, s.get("role", "implementer"), base))
                def _retry(wt=wt, brief=brief, issue=issue, cycle=cycle):
                    return spawn_headless(wt.path, brief, f"issue-{issue.number}-retry-c{cycle}", cfg)
                pid2 = act(f"issue #{issue.number}: run ended without a PR -> one retry (cycle {cycle}/{max_cycles})", _retry)
                if DRY_RUN or pid2:
                    s["cycle"] = cycle
                    s["retried"] = True
                    if pid2:
                        s["run"] = {"pid": pid2, "started": now_ms(), "kind": "retry"}
                    state.save()
                continue
            act(f"issue #{issue.number}: run ended without a PR (after retry) -> needs-human",
                lambda n=issue.number: (gh_label("issue", n, add=[LABEL_NEEDS_HUMAN]),
                                        gh_comment("issue", n,
                                                   "Two headless runs ended without publishing a PR. See "
                                                   ".orca/dispatcher/runs/ for their logs; answer here and "
                                                   "remove `needs-human` to retry.")))
            state.save()
            continue

        # --- candidate for dispatch -------------------------------------------------------
        if obs.pause:
            log.debug("issue #%s held: %s", issue.number, obs.pause.describe())
            continue
        if not obs.gate_open:
            log.debug("issue #%s held: %s", issue.number, obs.gate_reason)
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
            notify_human(cfg, state, f"issue:{issue.number}:escalated",
                         f"issue #{issue.number} escalated", issue.url, "issue", issue.number)
            continue
        if slots <= 0:
            log.debug("issue #%s waits for a slot", issue.number)
            continue

        if wt is None:
            res = act(f"issue #{issue.number}: create worktree issue-{issue.number}",
                      lambda i=issue: orca_create_worktree(f"issue-{i.number}", base, i.number))
            if DRY_RUN:
                slots -= 1
                continue
            path = worktree_path(f"issue-{issue.number}", res)
        else:
            path = wt.path
        if not path:
            log.warning("issue #%s: no worktree path yet; retrying next tick", issue.number)
            continue

        role = role_for(issue)
        skills = skills_for(role, issue.labels, cfg)
        brief = write_brief(f"issue-{issue.number}-cycle{cycle}",
                            issue_brief(issue, role, cycle, max_cycles, skills, base))
        def _dispatch(path=path, brief=brief, issue=issue, cycle=cycle):
            return spawn_headless(path, brief, f"issue-{issue.number}-cycle{cycle}", cfg)
        pid = act(f"issue #{issue.number}: dispatch {role} (cycle {cycle}/{max_cycles})", _dispatch)
        if DRY_RUN or pid:
            s["cycle"] = cycle
            s["role"] = role
            s.pop("retried", None)
            if pid:
                s["run"] = {"pid": pid, "started": now_ms(), "kind": "implement"}
            state.save()
            act(f"issue #{issue.number}: comment dispatched",
                lambda n=issue.number, c=cycle, r=role: gh_comment("issue", n,
                    f"Dispatched a headless {r} run (cycle {c}/{max_cycles}). "
                    f"Log: .orca/dispatcher/runs/issue-{n}-cycle{c}.log"))
            slots -= 1


# --------------------------------------------------------------------------- reconcile: PRs


def reconcile_prs(obs: Observed, cfg: dict[str, Any], state: State) -> None:
    d = cfg["dispatcher"]
    max_cycles = int(cfg["circuit_breaker"]["max_cycles"])
    max_minutes = float(d["max_run_minutes"])
    wt_by_branch = {wt.branch: wt for wt in obs.worktrees}

    for pr in obs.prs:
        if pr.is_draft:
            continue
        s = state.pr(pr.number)
        issue = obs.issues.get(pr.issue_number) if pr.issue_number else None

        if LABEL_NEEDS_HUMAN in pr.labels:
            notify_human(cfg, state, f"pr:{pr.number}:needs-human", f"PR #{pr.number} needs you",
                         f"{pr.title}\nAnswer in the PR comments, then remove the "
                         f"`needs-human` label.\n{pr.url}", "pr", pr.number)
            continue  # a fix run is NOT dispatched while the human is being waited on
        state.notified.pop(f"pr:{pr.number}:needs-human", None)

        if LABEL_BLOCKED in pr.labels:
            fix = s.get("fix")
            if not s.get("blocked_handled"):
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
                            gh_comment("issue", issue.number,
                                       f"Circuit breaker tripped on PR #{pr.number}: {max_cycles} failed attempts.")
                    act(f"PR #{pr.number}: blocked beyond breaker -> escalate", _esc)
                    notify_human(cfg, state, f"pr:{pr.number}:escalated", f"PR #{pr.number} escalated",
                                 pr.url, "pr", pr.number)
                else:
                    wt = wt_by_branch.get(pr.head)
                    if wt is None or not wt.path:
                        act(f"PR #{pr.number}: blocked but no worktree for {pr.head} -> needs-human",
                            lambda n=pr.number: (gh_label("pr", n, add=[LABEL_NEEDS_HUMAN]),
                                                 gh_comment("pr", n,
                                                            "Blocked, but the local worktree for this branch is gone. "
                                                            "Recreate it in Orca (or fix by hand), then remove `needs-human`.")))
                    else:
                        role = role_for(issue) if issue else "implementer"
                        comments = gh_recent_comments(pr.number)
                        brief = write_brief(f"pr-{pr.number}-fix-c{cycle}",
                                            fix_brief(pr, issue, role, cycle, max_cycles, comments))
                        def _fix(wt=wt, brief=brief, pr=pr, cycle=cycle):
                            return spawn_headless(wt.path, brief, f"pr-{pr.number}-fix-c{cycle}", cfg)
                        pid = act(f"PR #{pr.number}: blocked -> dispatch fix run (cycle {cycle}/{max_cycles})", _fix)
                        if pid:
                            s["fix"] = {"pid": pid, "started": now_ms()}
                        act(f"PR #{pr.number}: comment fix dispatched",
                            lambda n=pr.number, c=cycle: gh_comment("pr", n,
                                f"Sent back; a fresh fix run is on it (cycle {c}/{max_cycles})."))
                s["blocked_handled"] = True
                state.save()
                continue
            # blocked_handled: watch the fix run.
            if fix:
                pid = int(fix.get("pid", 0))
                if run_alive(pid):
                    if minutes_since(int(fix.get("started", 0))) >= max_minutes:
                        act(f"PR #{pr.number}: fix run {pid} exceeded {max_minutes:.0f} min -> kill + needs-human",
                            lambda p=pid, n=pr.number: (kill_tree(p),
                                                        gh_label("pr", n, add=[LABEL_NEEDS_HUMAN]),
                                                        gh_comment("pr", n, "Fix run timed out. See .orca/dispatcher/runs/.")))
                        s.pop("fix", None)
                        state.save()
                else:
                    act(f"PR #{pr.number}: fix run ended but the PR is still blocked -> needs-human",
                        lambda n=pr.number: (gh_label("pr", n, add=[LABEL_NEEDS_HUMAN]),
                                             gh_comment("pr", n,
                                                        "A fix run ended without clearing `state:blocked`. See "
                                                        ".orca/dispatcher/runs/ for its log; answer here and remove "
                                                        "`needs-human` to retry.")))
                    s.pop("fix", None)
                    state.save()
            continue

        # not blocked (any more): tidy the bookkeeping; reap a lingering fix run.
        if s.get("blocked_handled") or s.get("fix"):
            fix = s.get("fix")
            if fix and run_alive(int(fix.get("pid", 0))):
                seen = s.setdefault("fix_done_seen", now_ms())
                if minutes_since(seen) >= PR_GRACE_MINUTES:
                    act(f"PR #{pr.number}: unblocked; reaping lingering fix run",
                        lambda p=int(fix.get("pid", 0)): kill_tree(p))
                    s.pop("fix", None)
                    s.pop("fix_done_seen", None)
                    s.pop("blocked_handled", None)
            else:
                s.pop("fix", None)
                s.pop("fix_done_seen", None)
                s.pop("blocked_handled", None)
            state.save()


# --------------------------------------------------------------------------- reconcile: merged


def reconcile_merged(obs: Observed, cfg: dict[str, Any], state: State) -> None:
    d = cfg["dispatcher"]
    wt_by_branch = {wt.branch: wt for wt in obs.worktrees}
    for pr in obs.merged:
        n = pr.issue_number
        issue = obs.issues.get(n) if n else None
        if n:
            si = state.issues.get(str(n)) or {}
            r = si.get("run")
            if r and run_alive(int(r.get("pid", 0))):
                act(f"PR #{pr.number} merged -> kill lingering run for issue #{n}",
                    lambda p=int(r.get("pid", 0)): kill_tree(p))
        if issue and issue.state == "OPEN" and n not in state.closed_issues:
            act(f"PR #{pr.number} merged -> close issue #{n}",
                lambda n=n, p=pr: run(["gh", "issue", "close", str(n), "--reason", "completed",
                                       "--comment", f"Merged into {cfg['branches']['base']} via PR #{p.number}."]))
            state.closed_issues.append(n)
            state.save()
        wt = wt_by_branch.get(pr.head)
        if wt and d.get("cleanup_worktrees_on_merge", True):
            act(f"PR #{pr.number} merged -> remove worktree {wt.name}",
                lambda wt=wt: orca_json(["worktree", "rm", "--worktree", f"id:{wt.id}", "--force"]))
        if n and str(n) in state.issues:
            state.issues.pop(str(n), None)
            state.save()
        if str(pr.number) in state.prs:
            state.prs.pop(str(pr.number), None)
            state.save()


# --------------------------------------------------------------------------- reconcile: backlog


_autonomy_warned = False


def reconcile_backlog(obs: Observed, cfg: dict[str, Any], state: State) -> None:
    """Close the loop: drained pipeline -> headless Planner audit -> new issues or
    ACHIEVED. Honours the autonomy dial; every transition is triggered by observed
    state, never by what an agent said."""
    global _autonomy_warned
    d = cfg["dispatcher"]
    base = cfg["branches"]["base"]
    g = cfg["gates"]["core_document"]
    b = state.backlog

    # Project complete: page the human once, then hold until a revision reopens the gate.
    if obs.achieved:
        if "achieved_notified" not in b:
            body = (f"Every issue is closed and `{g['path']}` on `{base}` is marked ACHIEVED.\n"
                    f"Review the result. When satisfied, open the {base} -> main PR yourself.\n"
                    f"For a new round: run `python .orca/dispatcher/dispatch.py onboard` -- a "
                    f"revision interview that reopens the gate.")
            target = next((p for p in obs.merged if g["path"] in (p.title + p.body)), None)
            notify_human(cfg, state, "project:achieved", "project complete -- core document ACHIEVED",
                         body, "pr", target.number if target else None)
            b["achieved_notified"] = now_ms()
            state.save()
        return
    if b.pop("achieved_notified", None) is not None:
        state.notified.pop("project:achieved", None)
        state.save()

    if not obs.gate_open:
        return

    mode = autonomy_of(cfg)
    if mode is None:
        if not _autonomy_warned:
            log.warning("autonomy not set in dispatch.yml; treating as 'propose'. "
                        "Run `dispatch.py onboard` or set the line to silence this.")
            _autonomy_warned = True
        mode = "propose"
    if mode == "manual":
        return

    open_issues = any(i.state == "OPEN" for i in obs.issues.values())
    audit = b.get("audit")  # {pid, started, epoch, name}

    if audit:
        name = audit.get("name", "")
        wt = next((w for w in obs.worktrees if w.name == name), None)
        audit_pr = next((p for p in obs.prs if wt and p.head == wt.branch), None)
        pid = int(audit.get("pid", 0))
        if open_issues or audit_pr:
            # An outcome is visible on GitHub. Let the run finish talking, then clean up.
            if run_alive(pid):
                seen = b.setdefault("audit_done_seen", now_ms())
                if minutes_since(seen) >= PR_GRACE_MINUTES:
                    act("backlog audit produced an outcome; reaping lingering run",
                        lambda p=pid: kill_tree(p))
                else:
                    return
            if wt and not audit_pr:
                act(f"backlog audit filed issues -> remove worktree {wt.name}",
                    lambda wt=wt: orca_json(["worktree", "rm", "--worktree", f"id:{wt.id}", "--force"]))
            b.pop("audit", None)
            b.pop("audit_done_seen", None)
            state.save()
            return
        if run_alive(pid):
            if minutes_since(int(audit.get("started", 0))) >= float(d["max_run_minutes"]):
                act("backlog audit exceeded max_run_minutes -> kill", lambda p=pid: kill_tree(p))
                b.pop("audit", None)
                state.save()
                notify_human(cfg, state, f"backlog:audit-stalled:{audit.get('epoch')}",
                             "backlog audit timed out",
                             "The Planner audit ran out of time without filing issues or declaring "
                             "completion. See .orca/dispatcher/runs/. File issues yourself, or wait: "
                             "a merge will allow a fresh audit.", "issue", None)
            return
        # run ended with no visible outcome
        b.pop("audit", None)
        state.save()
        notify_human(cfg, state, f"backlog:empty-audit:{audit.get('epoch')}",
                     "backlog audit produced nothing",
                     "A Planner audit ended without filing issues or declaring the core document "
                     "achieved, and nothing has merged since. See .orca/dispatcher/runs/. File "
                     "issues yourself or run `dispatch.py onboard` for a revision interview.",
                     "issue", None)
        return

    # No audit running: spawn one when the pipeline is fully drained.
    if open_issues or obs.prs or obs.worktrees:
        return
    if any(r.get("run") and run_alive(int(r["run"].get("pid", 0))) for r in state.issues.values()):
        return
    if obs.pause:
        log.debug("backlog audit held: %s", obs.pause.describe())
        return
    epoch = max((p.number for p in obs.merged), default=0)  # merged-PR high-water mark
    if b.get("audit_epoch") == epoch:
        notify_human(cfg, state, f"backlog:empty-audit:{epoch}", "backlog audit produced nothing",
                     "The previous backlog audit ended without filing issues or declaring the core "
                     "document achieved, and nothing has merged since. File issues yourself or run "
                     "`dispatch.py onboard` for a revision interview.", "issue", None)
        return
    name = f"{AUDIT_WT_PREFIX}-{epoch}"
    res = act(f"pipeline drained -> create audit worktree {name}",
              lambda: orca_create_worktree(name, base, None))
    if DRY_RUN:
        b["audit_epoch"] = epoch
        return
    path = worktree_path(name, res)
    if not path:
        log.warning("backlog audit: no worktree path; will retry next tick")
        return
    brief = write_brief(name, audit_brief(base, g["achieved_marker"], mode))
    pid = act(f"spawn headless Planner backlog audit (epoch {epoch}, autonomy {mode})",
              lambda: spawn_headless(path, brief, name, cfg))
    if pid:
        b["audit"] = {"pid": pid, "started": now_ms(), "epoch": epoch, "name": name}
        b["audit_epoch"] = epoch
        b.pop("audit_done_seen", None)
        state.save()


# --------------------------------------------------------------------------- tick & commands


def tick(cfg: dict[str, Any], state: State) -> None:
    global _pause_logged
    reap()
    obs = observe(cfg)
    log.debug("observed: %s issues, %s open PRs, %s merged PRs, %s worktrees; gate: %s",
              len(obs.issues), len(obs.prs), len(obs.merged), len(obs.worktrees), obs.gate_reason)
    pause_key = str(obs.pause.since) if obs.pause else None
    if pause_key != _pause_logged:
        if obs.pause:
            log.info("%s: no dispatch, no audits; fixes and merges still reconcile", obs.pause.describe())
        else:
            log.info("resumed: dispatching again")
        _pause_logged = pause_key
    reconcile_merged(obs, cfg, state)
    reconcile_prs(obs, cfg, state)
    reconcile_issues(obs, cfg, state)
    reconcile_backlog(obs, cfg, state)


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


def cmd_pause(reason: str) -> int:
    already = read_pause()
    if DRY_RUN:
        print(f"[dry-run] would pause this project ({PAUSE_FILE.name})")
        return 0
    pause = write_pause(reason)
    log.info("paused by hand: %s", pause.reason or "(no reason given)")
    if already:
        print(f"already paused for {minutes_since(already.since):.0f}m; reason and clock reset")
    print(pause.describe())
    print("  stopped:  dispatching ready issues, spawning backlog audits")
    print("  running:  fix runs, merge bookkeeping, issue closing, worktree cleanup, pages")
    print("  headless runs already started keep going; their PRs still land in CI.")
    print("  resume with: dispatch.py resume")
    return 0


def cmd_resume() -> int:
    if DRY_RUN:
        print(f"[dry-run] would resume this project ({PAUSE_FILE.name})")
        return 0
    was = clear_pause()
    if not was:
        print("not paused -- nothing to do")
        return 0
    log.info("resumed by hand after %.0f min", minutes_since(was.since))
    print(f"resumed after {minutes_since(was.since):.0f}m -- issues dispatch again on the next tick")
    return 0


def cmd_status(cfg: dict[str, Any], state: State) -> int:
    obs = observe(cfg)
    mode = autonomy_of(cfg) or "(unset -> propose)"
    print(f"autonomy: {mode}")
    if obs.pause:
        print(obs.pause.describe())
        print("  nothing new starts; fixes and merges still land.  resume with: dispatch.py resume")
    print(f"gate: {'OPEN' if obs.gate_open else 'CLOSED'} -- {obs.gate_reason}")
    pr_by_issue = {pr.issue_number: pr for pr in obs.prs if pr.issue_number}
    print("\nISSUES")
    for issue in sorted(obs.issues.values(), key=lambda i: i.number):
        if issue.state != "OPEN":
            continue
        s = state.issues.get(str(issue.number), {})
        flags = []
        if LABEL_PROPOSED in issue.labels: flags.append("proposed")
        if LABEL_READY in issue.labels: flags.append("ready")
        if LABEL_NEEDS_HUMAN in issue.labels: flags.append("NEEDS-HUMAN")
        if LABEL_ESCALATED in issue.labels: flags.append("ESCALATED")
        deps = issue.depends_on
        unmet = [n for n in deps if n not in obs.issues or obs.issues[n].state != "CLOSED"]
        r = s.get("run")
        if issue.number in pr_by_issue:
            where = "in PR #%s" % pr_by_issue[issue.number].number
        elif r and run_alive(int(r.get("pid", 0))):
            where = f"RUNNING pid={r['pid']} {minutes_since(int(r.get('started', 0))):.0f}m"
        else:
            where = "queued"
        print(f"  #{issue.number:<4} {where:<28} cycle={s.get('cycle', 0)} "
              f"{'waits on ' + str(unmet) if unmet else ''} {' '.join(flags)}  {issue.title[:50]}")
    print("\nPULL REQUESTS (base %s; verified and reviewed by CI)" % cfg["branches"]["base"])
    for pr in sorted(obs.prs, key=lambda p: p.number):
        s = state.prs.get(str(pr.number), {})
        if LABEL_NEEDS_HUMAN in pr.labels:
            stage = "NEEDS-HUMAN"
        elif LABEL_BLOCKED in pr.labels:
            fix = s.get("fix")
            stage = "blocked (fix run live)" if fix and run_alive(int(fix.get("pid", 0))) else "blocked"
        elif LABEL_TESTED in pr.labels:
            stage = "tested -> review"
        else:
            stage = "in CI"
        print(f"  #{pr.number:<4} {stage:<24} {pr.head:<32} {pr.title[:45]}")
    print("\nWORKTREES")
    for wt in obs.worktrees:
        print(f"  {wt.name:<24} {wt.branch:<40} {wt.path}")
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
    print("claude CLI")
    okk, out, err = run([cfg["dispatcher"]["claude_cmd"], "--version"], timeout=60)
    ok(f"{cfg['dispatcher']['claude_cmd']} -> {out.strip()[:60]}") if okk else \
        bad(f"cannot run `{cfg['dispatcher']['claude_cmd']} --version`: set dispatcher.claude_cmd "
            f"to an ABSOLUTE path (scheduled tasks often have a minimal PATH)")
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
    print("roles (in-repo, .orca/roles)")
    for r in ROLE_FILES:
        p = ROLES_DIR / f"{r}.md"
        ok(p.as_posix()) if p.exists() else bad(f"missing {p.as_posix()} -- seed from section 7 of agent-workflow-setup.md")
    print("skills (in-repo, .claude/skills -- must be COMMITTED for CI)")
    if SKILLS_DIR.exists() and any(SKILLS_DIR.iterdir()):
        ok(f"{sum(1 for _ in SKILLS_DIR.iterdir())} entries in {SKILLS_DIR.as_posix()}")
        okk, out, _ = run(["git", "-C", str(REPO_ROOT), "ls-files", str(SKILLS_DIR)])
        if okk and out.strip():
            ok("skills are tracked by git")
        else:
            bad("skills exist but are NOT committed -- CI agents will lack them: git add .claude/skills")
    else:
        bad(f"no skills at {SKILLS_DIR.as_posix()} -- run: bash .orca/setup_skills.sh, then commit")
    print("pipeline")
    ok(PIPELINE_FILE.as_posix()) if PIPELINE_FILE.exists() else \
        bad(f"missing {PIPELINE_FILE.as_posix()} -- seed from section 8 (and do OWNER STEPS 1-2)")
    print("autonomy")
    mode = autonomy_of(cfg)
    ok(f"autonomy: {mode}") if mode else \
        ok("autonomy unset -- `dispatch.py onboard` will ask the owner (defaults to propose meanwhile)")
    print("gate")
    g_ok, g_why, g_achieved = core_document_gate(cfg)
    if g_achieved:
        ok(g_why)
    else:
        (ok if g_ok else bad)(g_why)
    paused = read_pause()
    if paused:
        print("pause")
        ok(paused.describe() + "  (deliberate -- lift it with: dispatch.py resume)")
    print("state")
    try:
        HERE.mkdir(parents=True, exist_ok=True); (HERE / ".write-test").write_text("x"); (HERE / ".write-test").unlink()
        ok(f"{HERE.as_posix()} writable")
    except OSError as exc:
        bad(f"{HERE} not writable: {exc}")
    print(f"\n{problems} problem(s)")
    return 1 if problems else 0


AUTONOMY_QUESTION = """
How autonomous should this project be? (You can change this at any time by editing the
`autonomy:` line in .orca/dispatch.yml -- it is a project setting, not a GitHub setting.)

  1) manual  - YOU write the GitHub issues. The AI Planner only files suggestions
               (labelled `proposed`) when you explicitly start an interview, and never
               invents work on its own.
  2) propose - When all work is done, the AI Planner compares the result against the
               core document and files SUGGESTED issues (labelled `proposed`). Nothing
               runs until you promote a suggestion by giving it the `ready` label.
  3) auto    - The AI Planner files issues marked `ready` itself; the loop runs until
               the core document is achieved. You are only contacted for questions,
               escalations, and completion.
"""


def ensure_autonomy(cfg: dict[str, Any]) -> str:
    mode = autonomy_of(cfg)
    if mode:
        return mode
    if sys.stdin is not None and sys.stdin.isatty():
        print(AUTONOMY_QUESTION)
        choices = {"1": "manual", "2": "propose", "3": "auto",
                   "manual": "manual", "propose": "propose", "auto": "auto"}
        while True:
            answer = input("Choose 1, 2 or 3 [2]: ").strip().lower() or "2"
            if answer in choices:
                mode = choices[answer]
                break
            print("Please answer 1, 2 or 3.")
        if not DRY_RUN:
            write_autonomy(mode)
            print(f"written to {CONFIG_FILE.as_posix()}: autonomy: {mode}")
        cfg["autonomy"] = mode
        return mode
    print("autonomy not set and no terminal to ask in; using 'propose' for this session. "
          "Set the `autonomy:` line in .orca/dispatch.yml to make it permanent.")
    cfg["autonomy"] = "propose"
    return "propose"


def cmd_onboard(cfg: dict[str, Any]) -> int:
    mode = ensure_autonomy(cfg)
    base = cfg["branches"]["base"]
    g = cfg["gates"]["core_document"]
    run(["git", "-C", str(REPO_ROOT), "fetch", "-q", "origin", base], timeout=60)
    ok, doc, _ = run(["git", "-C", str(REPO_ROOT), "show", f"origin/{base}:{g['path']}"])
    populated = ok and g["empty_marker"] not in doc
    if populated:
        name = "revision-" + datetime.now().strftime("%Y%m%d-%H%M")
        brief = write_brief(name, revision_brief(base, g["achieved_marker"], mode))
        what = "revision"
    else:
        name = "onboarding"
        brief = write_brief("onboarding", onboarding_brief(base, mode))
        what = "onboarding"
    agent = cfg["dispatcher"]["interactive_agent"]
    res = act(f"create {what} worktree with the Planner (interactive)",
              lambda: orca_create_worktree(name, base, None, agent=agent,
                                           prompt=one_liner(brief), activate=True))
    if DRY_RUN:
        return 0
    if not res:
        print("failed: is Orca open and the repo registered? run: dispatch.py doctor")
        return 1
    print(f"{what} worktree created; switch to Orca and talk to the Planner.")
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
    p = argparse.ArgumentParser(description="ORCA ADE dispatcher v3 (reconciler, headless workers).")
    p.add_argument("command", nargs="?", default="run",
                   choices=["run", "once", "status", "doctor", "onboard", "pause", "resume"])
    p.add_argument("--once", action="store_true", help="alias for the `once` command")
    p.add_argument("--interval", type=int, default=None)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--fix", action="store_true", help="doctor: create missing labels")
    p.add_argument("-m", "--reason", default="", help="pause: why, shown in status and the log")
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
    if cmd == "pause":
        return cmd_pause(a.reason)
    if cmd == "resume":
        return cmd_resume()
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
