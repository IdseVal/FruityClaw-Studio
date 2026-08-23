"""Dispatcher runtime — watches GitHub issues, creates Orca worktrees.

The ORCA ADE setup document names this the "Dispatcher Agent". In practice it is a
running PROCESS on the host, not a chat agent: an LLM in an infinite poll loop is
wasteful and unreliable. The rules in ``~/.orca/roles/dispatcher.md`` (the shared,
host-level roles directory that every project uses) describe the POLICY this script
enforces; the process itself is here.

Runtime shape:

* Poll ``gh issue list --state open --label ready`` at ``--interval`` seconds.
* For each ready issue not yet dispatched, build a Developer prompt (the
  ``developer.md`` system prompt, plus the earned skills, plus the issue body and
  circuit-breaker context) and call ``orca worktree create`` to spawn the worker.
* Record the dispatch in ``.orca/dispatcher/state.json`` (gitignored) so the same
  issue is not re-dispatched on the next tick.
* Circuit breaker: after ``max_cycles`` dispatches without a merged PR into ``dev``
  the poller adds the ``escalated`` label, posts a summary comment, and stops
  touching the issue.
"""

from __future__ import annotations

import argparse
import json
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

REPO_ROOT = Path(__file__).resolve().parents[2]
DISPATCH_CONFIG = REPO_ROOT / ".orca" / "dispatch.yml"

# Role prompts are HOST-LEVEL, shared across every project this user runs through
# the workflow. Never a repo-relative path. See memory feedback-roles-are-host-level
# and section 7 of agent-workflow-setup.md.
ROLES_DIR = Path.home() / ".orca" / "roles"

STATE_FILE = REPO_ROOT / ".orca" / "dispatcher" / "state.json"
PROMPTS_DIR = REPO_ROOT / ".orca" / "dispatcher" / "prompts"

READY_LABEL = "ready"
ESCALATED_LABEL = "escalated"
ROLE_LABEL_PREFIX = "role:"

# PR pipeline signals. Tester adds state:tested on pass; either agent adds state:blocked
# to hand the PR back to the human.
TESTED_LABEL = "state:tested"
BLOCKED_LABEL = "state:blocked"

# Terminal readiness wait for spawned Claude agents. Prompts sent to the Tester and
# Reviewer are also written to `.orca/dispatcher/prompts/pr-<n>-<role>.txt` first so a
# human can inspect exactly what the dispatcher fed each agent when triaging.
TERMINAL_READY_TIMEOUT_MS = 60_000

# Regex-free parse of `feature/issue-<n>` branch names.
FEATURE_BRANCH_PREFIX = "feature/issue-"

DEFAULT_POLL_INTERVAL_SECONDS = 60
SUBPROCESS_TIMEOUT_SECONDS = 120

_STOP = False


def _handle_signal(signum: int, frame: Any) -> None:
    """Flip the loop's stop flag on Ctrl+C / SIGTERM."""

    global _STOP
    _STOP = True


signal.signal(signal.SIGINT, _handle_signal)
if hasattr(signal, "SIGTERM"):
    signal.signal(signal.SIGTERM, _handle_signal)


@dataclass
class DispatchState:
    """On-disk record of what has been dispatched.

    ``dispatched[str(issue_number)]`` holds a dict with:
      - ``cycle``          — count of dispatches so far
      - ``role``           — the role loaded from ~/.orca/roles/<role>.md
      - ``worktree_name``  — feature/issue-<n>
      - ``worktree_id``    — Orca worktree id, if the create call reported one
      - ``pipeline``       — default | trivial | <role name>
      - ``escalated``      — true once the breaker trips
      - ``role_missing``   — true if the issue lacked a usable role:* label

    ``prs[str(pr_number)]`` holds a dict with:
      - ``stage``          — tester_dispatched | reviewer_dispatched | blocked
      - ``worktree_id``    — where the tester/reviewer terminals live
      - ``issue_number``   — the originating issue, if parseable from the branch
      - ``tester_terminal``   — terminal handle for the Tester session, if any
      - ``reviewer_terminal`` — terminal handle for the Reviewer session, if any
    """

    dispatched: dict[str, dict[str, Any]] = field(default_factory=dict)
    prs: dict[str, dict[str, Any]] = field(default_factory=dict)

    @classmethod
    def load(cls) -> "DispatchState":
        if not STATE_FILE.exists():
            return cls()
        try:
            raw = json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(
                f"!! state file unreadable ({exc}); starting fresh",
                file=sys.stderr,
            )
            return cls()
        return cls(
            dispatched=raw.get("dispatched", {}),
            prs=raw.get("prs", {}),
        )

    def save(self) -> None:
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = STATE_FILE.with_suffix(".tmp")
        tmp.write_text(
            json.dumps(
                {"dispatched": self.dispatched, "prs": self.prs},
                indent=2,
            ),
            encoding="utf-8",
        )
        tmp.replace(STATE_FILE)


def load_config() -> dict[str, Any]:
    """Read ``.orca/dispatch.yml``. Errors surface — a bad config must not silently
    become an empty dispatcher."""

    with DISPATCH_CONFIG.open("r", encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def _run(argv: list[str], *, timeout: int = SUBPROCESS_TIMEOUT_SECONDS) -> tuple[bool, str]:
    """Run a command; return ``(ok, stdout)``. Never raises."""

    try:
        result = subprocess.run(
            argv,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as exc:
        print(f"!! {argv[0]} unavailable: {exc}", file=sys.stderr)
        return False, ""
    if result.returncode != 0:
        err = (result.stderr or "").strip()
        if err:
            print(
                f"!! {' '.join(argv[:3])} failed: {err}",
                file=sys.stderr,
            )
        return False, ""
    return True, result.stdout


def gh_list_ready_issues() -> list[dict[str, Any]]:
    """Ask gh for every open, ready issue. Empty on any failure."""

    ok, out = _run(
        [
            "gh",
            "issue",
            "list",
            "--state",
            "open",
            "--label",
            READY_LABEL,
            "--limit",
            "200",
            "--json",
            "number,title,body,labels,url,assignees",
        ]
    )
    if not ok or not out.strip():
        return []
    try:
        return json.loads(out)
    except json.JSONDecodeError as exc:
        print(f"!! gh returned bad JSON: {exc}", file=sys.stderr)
        return []


def gh_add_label(issue_number: int, label: str) -> None:
    _run(["gh", "issue", "edit", str(issue_number), "--add-label", label])


def gh_comment(issue_number: int, body: str) -> None:
    _run(["gh", "issue", "comment", str(issue_number), "--body", body])


def gh_list_open_prs_to_dev() -> list[dict[str, Any]]:
    """Every open PR whose base is ``dev``. Empty on any failure."""

    ok, out = _run(
        [
            "gh",
            "pr",
            "list",
            "--state",
            "open",
            "--base",
            "dev",
            "--limit",
            "200",
            "--json",
            "number,title,body,labels,url,headRefName",
        ]
    )
    if not ok or not out.strip():
        return []
    try:
        return json.loads(out)
    except json.JSONDecodeError as exc:
        print(f"!! gh returned bad JSON: {exc}", file=sys.stderr)
        return []


def gh_pr_comment(pr_number: int, body: str) -> None:
    _run(["gh", "pr", "comment", str(pr_number), "--body", body])


def gh_pr_add_label(pr_number: int, label: str) -> None:
    _run(["gh", "pr", "edit", str(pr_number), "--add-label", label])


import re

# Orca rewrites `--name feature/issue-<n>` into a branch named
# `<gitusername>/feature-issue-<n>` (flattens the slash, prefixes the git username).
# Accept both my ideal form and Orca's actual form, with or without ref prefix.
_FEATURE_BRANCH_RE = re.compile(r"(?:^|/)feature[-/]issue-(\d+)")


def _issue_number_from_branch(branch: str) -> int | None:
    """Parse an issue number out of a dispatcher-created branch name."""

    stripped = branch.removeprefix("refs/heads/")
    match = _FEATURE_BRANCH_RE.search(stripped)
    if not match:
        return None
    return int(match.group(1))


def find_worktree_selector_for_branch(branch: str) -> str | None:
    """Return an ``orca`` worktree selector for the worktree checked out on ``branch``.

    Looks the worktree up via ``orca worktree list --json`` and returns a
    ``branch:<name>`` selector when the branch matches. Returns None if Orca reports
    no such worktree — the caller should defer and try again next tick.
    """

    ok, out = _run(["orca", "worktree", "list", "--json"])
    if not ok or not out.strip():
        return None
    try:
        payload = json.loads(out)
    except json.JSONDecodeError:
        return None
    worktrees = ((payload.get("result") or {}).get("worktrees")) or []
    stripped = branch.removeprefix("refs/heads/")
    for entry in worktrees:
        raw = entry.get("branch") or ""
        entry_branch = raw.removeprefix("refs/heads/")
        if entry_branch == stripped:
            return f"branch:{entry_branch}"
    return None


def _extract_terminal_handle(payload: dict[str, Any]) -> str | None:
    """Extract a terminal handle from an ``orca terminal create --json`` payload,
    tolerating minor schema differences across Orca versions."""

    result = payload.get("result") or {}
    terminal = result.get("terminal") or {}
    for key in ("handle", "terminalHandle", "id"):
        value = terminal.get(key)
        if isinstance(value, str) and value:
            return value
    for key in ("terminalHandle", "handle"):
        value = result.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def spawn_role_terminal(worktree_selector: str, prompt: str) -> str | None:
    """Start ``claude`` in a fresh terminal inside the given worktree, wait for it to
    become ready, and feed it ``prompt`` as the initial user message.

    Returns the terminal handle on success, or None on any failure — the caller should
    defer and try again next tick rather than losing the PR.
    """

    ok, out = _run(
        [
            "orca",
            "terminal",
            "create",
            "--worktree",
            worktree_selector,
            "--command",
            "claude",
            "--json",
        ]
    )
    if not ok or not out.strip():
        return None
    try:
        payload = json.loads(out)
    except json.JSONDecodeError:
        return None
    handle = _extract_terminal_handle(payload)
    if not handle:
        print("!! orca terminal create returned no handle", file=sys.stderr)
        return None

    # claude has a TUI startup lag; sending the prompt too early drops characters.
    wait_ok, _ = _run(
        [
            "orca",
            "terminal",
            "wait",
            "--terminal",
            handle,
            "--for",
            "tui-idle",
            "--timeout-ms",
            str(TERMINAL_READY_TIMEOUT_MS),
        ]
    )
    if not wait_ok:
        print(
            f"!! orca terminal wait timed out for {handle}; sending anyway",
            file=sys.stderr,
        )

    send_ok, _ = _run(
        [
            "orca",
            "terminal",
            "send",
            "--terminal",
            handle,
            "--text",
            prompt,
            "--enter",
        ]
    )
    if not send_ok:
        return None
    return handle


def _write_prompt_to_disk(pr_number: int, role: str, prompt: str) -> None:
    """Persist the exact prompt the dispatcher fed the agent, for later triage."""

    try:
        PROMPTS_DIR.mkdir(parents=True, exist_ok=True)
        target = PROMPTS_DIR / f"pr-{pr_number}-{role}.txt"
        target.write_text(prompt, encoding="utf-8")
    except OSError as exc:
        print(f"!! could not persist prompt for pr #{pr_number}: {exc}", file=sys.stderr)


def build_pr_role_prompt(
    role: str,
    pr: dict[str, Any],
    issue_number: int | None,
) -> str | None:
    """Compose a SMALL first-turn prompt (~900 chars) for a Tester or Reviewer
    spawned onto an open PR. The agent fetches its role and the PR itself."""

    role_file = ROLES_DIR / f"{role}.md"
    if not role_file.exists():
        return None
    pr_number = int(pr["number"])
    issue_note = (
        f"originating issue #{issue_number}"
        if issue_number is not None
        else "no originating issue (branch did not match feature-issue-<n>)"
    )

    if role == "tester":
        actions = (
            f"1. Run the project's test suite (per docs or the project's conventions). "
            f"Reproduce any failing test and capture what it did.\n"
            f"2. If ALL tests pass:  "
            f"`gh pr edit {pr_number} --add-label {TESTED_LABEL}`, then post a "
            f"comment on the PR summarising the run.\n"
            f"3. If ANY test fails, or you cannot run the suite:  "
            f"`gh pr edit {pr_number} --add-label {BLOCKED_LABEL}`, then post a "
            f"comment explaining what failed.\n"
            f"\n"
            f"Do NOT push commits, do NOT merge."
        )
    elif role == "reviewer":
        issue_ref = str(issue_number) if issue_number is not None else "?"
        actions = (
            f"1. Read the diff (`git diff dev...HEAD`) and the linked issue "
            f"#{issue_ref}. Verify the code fulfils the spec and meets the code "
            f"standard (easily readable, minimalist, no premature abstraction).\n"
            f"2. If you approve:  "
            f"`gh pr merge {pr_number} --squash --delete-branch`, then post an "
            f"approval comment on the PR. This merges into `dev`.\n"
            f"3. If you want changes:  "
            f"`gh pr edit {pr_number} --add-label {BLOCKED_LABEL}`, post a review "
            f"comment listing the required changes."
        )
    else:
        return None

    return (
        f"You are being dispatched as the **{role}** for PR #{pr_number} "
        f"({issue_note}).\n"
        "\n"
        "Load your context:\n"
        f"  cat ~/.orca/roles/{role}.md\n"
        f"  gh pr view {pr_number}\n"
        f"  git log --oneline dev..HEAD\n"
        "\n"
        f"Then:\n{actions}\n"
        "\n"
        f"You may merge into `dev` and only `dev`. STRICTLY FORBIDDEN from merging "
        f"anything into `main`.\n"
    )


def role_from_labels(labels: set[str]) -> tuple[str | None, str]:
    """Return ``(role, reason)``.

    ``role`` is the role name from the sole ``role:*`` label, or None when no
    unambiguous role can be chosen. ``reason`` is a human-readable explanation used in
    the skip comment.
    """

    role_labels = sorted(
        label[len(ROLE_LABEL_PREFIX):]
        for label in labels
        if label.startswith(ROLE_LABEL_PREFIX)
    )
    if not role_labels:
        return None, "no `role:*` label found"
    if len(role_labels) > 1:
        return None, f"multiple `role:*` labels found: {role_labels}"
    return role_labels[0], "ok"


def pipeline_for(role: str, labels: set[str], config: dict[str, Any]) -> str:
    """Only the ``developer`` role opts into default/trivial pipelines. Every other
    role is a single-agent dispatch named after the role itself."""

    if role != "developer":
        return role
    if "trivial" not in labels:
        return "default"
    forbidden = set(
        config.get("pipelines", {})
        .get("trivial", {})
        .get("forbid_labels", [])
    )
    if labels & forbidden:
        return "default"
    return "trivial"


def skills_for(role: str, labels: set[str], config: dict[str, Any]) -> list[str]:
    """Baseline skills for the chosen role plus any earned by the issue's labels."""

    ordered: list[str] = []
    seen: set[str] = set()

    def _extend(items: list[str]) -> None:
        for item in items:
            if item and item not in seen:
                seen.add(item)
                ordered.append(item)

    _extend(config.get("roles", {}).get(role, {}).get("skills", []) or [])
    label_map = config.get("labels", {}) or {}
    for label in sorted(labels):
        _extend((label_map.get(label) or {}).get("skills", []) or [])
    return ordered


def build_role_prompt(
    role: str,
    issue: dict[str, Any],
    pipeline: str,
    skills: list[str],
    cycle: int,
    max_cycles: int,
) -> str | None:
    """Compose a SMALL first-turn prompt (~800 chars).

    The agent fetches its full role prompt and the issue body itself with local
    commands. This keeps the initial prompt well under any argv or terminal-input
    limit, and keeps the responsibility for reading the role in one place (the
    role file on disk) rather than duplicating it into every dispatch.

    Returns None if the role prompt file is missing on this host — the caller
    comments and skips.
    """

    role_file = ROLES_DIR / f"{role}.md"
    if not role_file.exists():
        return None
    skill_list = ", ".join(skills) if skills else "(baseline only)"
    label_list = (
        ", ".join(sorted(label["name"] for label in issue.get("labels", [])))
        or "(none)"
    )
    issue_number = int(issue["number"])

    return (
        f"You are being dispatched as the **{role}** for issue #{issue_number} in "
        f"this project.\n"
        "\n"
        "Load your context before doing anything:\n"
        f"  1. `cat ~/.orca/roles/{role}.md`  -- your role instructions\n"
        f"  2. `gh issue view {issue_number}`  -- the task\n"
        "  3. `cat docs/CORE_DOCUMENT.md`  -- project single source of truth\n"
        "  4. Read the relevant `docs/specs/*.md` and any ADRs.\n"
        "\n"
        "Then follow your role instructions. When you are done, open a PR into "
        "`dev`:\n"
        "  `gh pr create --base dev --fill`\n"
        "\n"
        "Dispatcher context:\n"
        f"- role={role}, pipeline={pipeline}, cycle={cycle}/{max_cycles}\n"
        f"- skills={skill_list}\n"
        f"- labels={label_list}\n"
        "\n"
        f"Rules:\n"
        f"- STRICTLY FORBIDDEN from merging any branch into `main`.\n"
        f"- If your role requires a skill you don't have (check `~/.claude/skills`), "
        f"add a comment to issue #{issue_number} saying which skill is missing and "
        f"stop. A human resolves.\n"
    )


def dispatch_issue(
    issue: dict[str, Any],
    config: dict[str, Any],
    state: DispatchState,
) -> bool:
    """Create the worktree for one issue. Returns True iff a worktree was created.

    Increments the cycle count first; if the increment would exceed ``max_cycles`` the
    breaker trips and no worktree is created. If no unambiguous ``role:*`` label is
    present the poller comments once and records ``role_missing`` in state so the same
    comment is not re-posted on every tick.
    """

    number = int(issue["number"])
    key = str(number)
    existing = state.dispatched.get(key, {})
    max_cycles = int(config.get("circuit_breaker", {}).get("max_cycles", 3))
    labels = {label["name"] for label in issue.get("labels", [])}

    role, role_reason = role_from_labels(labels)
    if role is None:
        prev_labels = set(existing.get("labels", []))
        if existing.get("role_missing") and prev_labels == labels:
            # Same broken label set as before; do not re-comment.
            return False
        msg = (
            f"Cannot dispatch: {role_reason}. Add exactly one `role:*` label "
            "(e.g. `role:developer`, `role:architect`, `role:researcher`) and remove "
            "the `escalated` label if it is present."
        )
        gh_comment(number, msg)
        state.dispatched[key] = {
            **existing,
            "role_missing": True,
            "labels": sorted(labels),
        }
        state.save()
        print(f"skipped #{number}: {role_reason}", file=sys.stderr)
        return False

    role_file = ROLES_DIR / f"{role}.md"
    if not role_file.exists():
        prev_labels = set(existing.get("labels", []))
        if existing.get("role_missing") and prev_labels == labels:
            return False
        msg = (
            f"Cannot dispatch: role prompt `~/.orca/roles/{role}.md` is missing on "
            "the dispatcher host. Seed the host roles directory from section 7 of "
            "agent-workflow-setup.md."
        )
        gh_comment(number, msg)
        state.dispatched[key] = {
            **existing,
            "role_missing": True,
            "labels": sorted(labels),
        }
        state.save()
        print(f"skipped #{number}: role prompt missing for {role!r}", file=sys.stderr)
        return False

    next_cycle = int(existing.get("cycle", 0)) + 1

    if next_cycle > max_cycles:
        # We have already spent all allowed cycles; escalate instead of dispatching.
        summary = f"Circuit breaker tripped: {max_cycles} failed attempts."
        gh_comment(number, summary)
        gh_add_label(number, ESCALATED_LABEL)
        state.dispatched[key] = {**existing, "escalated": True}
        state.save()
        print(f"escalated #{number}: {summary}", file=sys.stderr)
        return False

    pipeline = pipeline_for(role, labels, config)
    skills = skills_for(role, labels, config)
    prompt = build_role_prompt(role, issue, pipeline, skills, next_cycle, max_cycles)
    assert prompt is not None  # guaranteed by the role_file.exists() check above.

    worktree_name = f"feature/issue-{number}"
    ok, out = _run(
        [
            "orca",
            "worktree",
            "create",
            "--repo",
            f"path:{REPO_ROOT}",
            "--name",
            worktree_name,
            "--issue",
            str(number),
            "--base-branch",
            config.get("branches", {}).get("base", "dev"),
            "--agent",
            "claude",
            "--prompt",
            prompt,
            "--json",
        ]
    )
    if not ok:
        # A create failure is transient (Orca not open, git conflict, gh rate limit);
        # do not count it against the cycle budget.
        return False

    worktree_id: str | None = None
    try:
        payload = json.loads(out)
        worktree_id = (
            ((payload.get("result") or {}).get("worktree") or {}).get("id")
        )
    except json.JSONDecodeError:
        pass

    state.dispatched[key] = {
        "cycle": next_cycle,
        "role": role,
        "worktree_name": worktree_name,
        "worktree_id": worktree_id,
        "pipeline": pipeline,
    }
    state.save()

    gh_comment(
        number,
        f"Dispatched to worktree `{worktree_name}` as `{role}` "
        f"(pipeline: {pipeline}, cycle {next_cycle}/{max_cycles}).",
    )
    print(
        f"dispatched #{number} -> {worktree_name} as {role} "
        f"(cycle {next_cycle}/{max_cycles})"
    )
    return True


def dispatch_pr_stage(
    pr: dict[str, Any],
    role: str,
    state: DispatchState,
) -> str | None:
    """Spawn ``role`` (tester or reviewer) as a fresh Claude terminal inside the
    developer's existing worktree. Records handles in state and comments on the PR.
    Returns the terminal handle on success, None on any failure."""

    pr_number = int(pr["number"])
    branch = pr.get("headRefName") or ""
    issue_number = _issue_number_from_branch(branch)

    worktree_selector = find_worktree_selector_for_branch(branch)
    if worktree_selector is None:
        print(
            f"!! pr #{pr_number}: no Orca worktree found for branch {branch!r}; "
            "deferring — a human may need to open one",
            file=sys.stderr,
        )
        return None

    prompt = build_pr_role_prompt(role, pr, issue_number)
    if prompt is None:
        print(
            f"!! pr #{pr_number}: role prompt missing for {role!r} at "
            f"~/.orca/roles/{role}.md",
            file=sys.stderr,
        )
        return None
    _write_prompt_to_disk(pr_number, role, prompt)

    handle = spawn_role_terminal(worktree_selector, prompt)
    if handle is None:
        return None

    key = str(pr_number)
    existing = state.prs.get(key, {})
    stage = f"{role}_dispatched"
    updated = {
        **existing,
        "stage": stage,
        "worktree_selector": worktree_selector,
        "issue_number": issue_number,
        f"{role}_terminal": handle,
    }
    state.prs[key] = updated
    state.save()

    gh_pr_comment(
        pr_number,
        f"Dispatched `{role}` agent into worktree `{worktree_selector}` "
        f"(terminal `{handle}`). Waiting for the agent to add "
        f"`{TESTED_LABEL}` or `{BLOCKED_LABEL}`.",
    )
    print(f"dispatched pr #{pr_number} -> {role} in {worktree_selector}")
    return handle


def poll_prs(state: DispatchState) -> int:
    """One pass over open PRs to ``dev``. Advances the pipeline for each PR according
    to labels and stored state. Returns the count of new agent spawns this tick."""

    prs = gh_list_open_prs_to_dev()
    open_numbers = {int(pr["number"]) for pr in prs}

    # Drop state entries for PRs that are no longer open (merged, closed, or renamed).
    closed = [key for key in list(state.prs) if int(key) not in open_numbers]
    for key in closed:
        prev = state.prs.pop(key)
        prev_stage = prev.get("stage")
        print(f"pr #{key} closed; previous stage was {prev_stage!r}")
    if closed:
        state.save()

    spawns = 0
    for pr in prs:
        pr_number = int(pr["number"])
        key = str(pr_number)
        labels = {label["name"] for label in pr.get("labels", [])}
        entry = state.prs.get(key, {})
        stage = entry.get("stage")

        if BLOCKED_LABEL in labels:
            if stage != "blocked":
                state.prs[key] = {**entry, "stage": "blocked"}
                state.save()
                print(f"pr #{pr_number} blocked (label {BLOCKED_LABEL})", file=sys.stderr)
            continue

        if stage is None:
            # New PR — dispatch Tester.
            if dispatch_pr_stage(pr, "tester", state):
                spawns += 1
            continue

        if stage == "tester_dispatched" and TESTED_LABEL in labels:
            if dispatch_pr_stage(pr, "reviewer", state):
                spawns += 1
            continue

        # tester_dispatched without state:tested → still waiting for the Tester.
        # reviewer_dispatched → still waiting for the Reviewer to merge or block.

    return spawns


def poll_once(config: dict[str, Any], state: DispatchState) -> int:
    """Poll gh once. Returns the count of issues dispatched on this tick."""

    dispatched_now = 0
    for issue in gh_list_ready_issues():
        labels = {label["name"] for label in issue.get("labels", [])}
        if ESCALATED_LABEL in labels:
            continue
        key = str(int(issue["number"]))
        existing = state.dispatched.get(key)
        if existing:
            if existing.get("escalated"):
                continue
            if existing.get("role_missing"):
                # dispatch_issue re-evaluates when labels have changed; call it and
                # let it decide whether to comment/skip or fall through to dispatch.
                if dispatch_issue(issue, config, state):
                    dispatched_now += 1
                continue
            if existing.get("cycle", 0) > 0:
                # Already dispatched at least once; skip until the state entry is
                # cleared (documented in .orca/dispatcher/README.md).
                continue
        if dispatch_issue(issue, config, state):
            dispatched_now += 1
    return dispatched_now


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Dispatcher process — watches GitHub issues, creates Orca worktrees.",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Poll once and exit (for cron / Orca automation use).",
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=DEFAULT_POLL_INTERVAL_SECONDS,
        help="Seconds between polls in daemon mode (default 60).",
    )
    args = parser.parse_args()

    config = load_config()
    state = DispatchState.load()

    if args.once:
        poll_once(config, state)
        poll_prs(state)
        return 0

    print(
        f"dispatcher running (interval {args.interval}s); ctrl+c to stop",
        flush=True,
    )
    while not _STOP:
        try:
            dispatched = poll_once(config, state)
            if dispatched:
                print(f"tick: dispatched {dispatched} issue(s)", flush=True)
        except Exception as exc:  # noqa: BLE001 — the loop must never die.
            print(f"!! issue poll failed: {exc}", file=sys.stderr)
        try:
            spawns = poll_prs(state)
            if spawns:
                print(f"tick: spawned {spawns} PR-pipeline agent(s)", flush=True)
        except Exception as exc:  # noqa: BLE001 — the loop must never die.
            print(f"!! pr poll failed: {exc}", file=sys.stderr)
        # Sleep in 1-second slices so Ctrl+C is responsive.
        for _ in range(max(1, args.interval)):
            if _STOP:
                break
            time.sleep(1)
    print("dispatcher stopped")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
