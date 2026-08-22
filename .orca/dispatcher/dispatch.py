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

READY_LABEL = "ready"
ESCALATED_LABEL = "escalated"
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
      - ``worktree_name``  — feature/issue-<n>
      - ``worktree_id``    — Orca worktree id, if the create call reported one
      - ``pipeline``       — default | trivial
      - ``escalated``      — true once the breaker trips
    """

    dispatched: dict[str, dict[str, Any]] = field(default_factory=dict)

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
        return cls(dispatched=raw.get("dispatched", {}))

    def save(self) -> None:
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = STATE_FILE.with_suffix(".tmp")
        tmp.write_text(
            json.dumps({"dispatched": self.dispatched}, indent=2),
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


def pipeline_for_labels(labels: set[str], config: dict[str, Any]) -> str:
    """Return ``trivial`` iff the label set opts in AND touches no forbidden label."""

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


def skills_for_labels(labels: set[str], config: dict[str, Any]) -> list[str]:
    """Baseline developer skills plus any earned by the issue's labels."""

    ordered: list[str] = []
    seen: set[str] = set()

    def _extend(items: list[str]) -> None:
        for item in items:
            if item and item not in seen:
                seen.add(item)
                ordered.append(item)

    _extend(config.get("roles", {}).get("developer", {}).get("skills", []) or [])
    label_map = config.get("labels", {}) or {}
    for label in sorted(labels):
        _extend((label_map.get(label) or {}).get("skills", []) or [])
    return ordered


def build_developer_prompt(
    issue: dict[str, Any],
    pipeline: str,
    skills: list[str],
    cycle: int,
    max_cycles: int,
) -> str:
    """Compose the message the Developer agent sees on first turn.

    We inject the Developer system prompt verbatim, then a DISPATCH CONTEXT block, then
    the issue body. The agent has everything it needs without another round trip.
    """

    developer_role = ROLES_DIR / "developer.md"
    if not developer_role.exists():
        raise FileNotFoundError(
            f"Role prompt missing: {developer_role}. Roles live in ~/.orca/roles/ and "
            "are shared across all projects; seed the directory from section 7 of "
            "agent-workflow-setup.md on this host."
        )
    system_prompt = developer_role.read_text(encoding="utf-8")
    labels = ", ".join(sorted(label["name"] for label in issue.get("labels", []))) or "(none)"
    skill_list = ", ".join(skills) if skills else "(baseline only)"
    body = issue.get("body") or "(empty)"

    return f"""{system_prompt}

---
DISPATCH CONTEXT (injected by the Dispatcher process)

Pipeline: {pipeline}
Cycle:    {cycle}/{max_cycles}
Skills:   {skill_list}

Issue #{issue['number']}: {issue['title']}
Labels:   {labels}
URL:      {issue['url']}

Read `docs/CORE_DOCUMENT.md`, the relevant `docs/specs/*.md` and any ADRs before you
begin. Do only what this issue asks. When you are finished:

1. Open a PR from your worktree branch into `dev` with `gh pr create`.
2. Paste evidence into the PR body per `.orca/dispatch.yml` evidence gates
   (counts reconciled, output sampled, cost measured — whichever apply).
3. Post a completion comment on issue #{issue['number']}.

You are STRICTLY FORBIDDEN from merging any branch into `main`. Only the Reviewer merges
into `dev`; the human merges `dev` into `main`.

---
ISSUE BODY

{body}
"""


def dispatch_issue(
    issue: dict[str, Any],
    config: dict[str, Any],
    state: DispatchState,
) -> bool:
    """Create the worktree for one issue. Returns True iff a worktree was created.

    Increments the cycle count first; if the increment would exceed ``max_cycles`` the
    breaker trips and no worktree is created.
    """

    number = int(issue["number"])
    key = str(number)
    existing = state.dispatched.get(key, {})
    max_cycles = int(config.get("circuit_breaker", {}).get("max_cycles", 3))
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

    labels = {label["name"] for label in issue.get("labels", [])}
    pipeline = pipeline_for_labels(labels, config)
    skills = skills_for_labels(labels, config)
    prompt = build_developer_prompt(issue, pipeline, skills, next_cycle, max_cycles)

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
        "worktree_name": worktree_name,
        "worktree_id": worktree_id,
        "pipeline": pipeline,
    }
    state.save()

    gh_comment(
        number,
        f"Dispatched to worktree `{worktree_name}` "
        f"(pipeline: {pipeline}, cycle {next_cycle}/{max_cycles}).",
    )
    print(f"dispatched #{number} -> {worktree_name} (cycle {next_cycle}/{max_cycles})")
    return True


def poll_once(config: dict[str, Any], state: DispatchState) -> int:
    """Poll gh once. Returns the count of issues dispatched on this tick."""

    dispatched_now = 0
    for issue in gh_list_ready_issues():
        labels = {label["name"] for label in issue.get("labels", [])}
        if ESCALATED_LABEL in labels:
            continue
        key = str(int(issue["number"]))
        if key in state.dispatched and not state.dispatched[key].get("escalated"):
            # Already dispatched and not yet failed enough to re-enter the queue.
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
            print(f"!! poll failed: {exc}", file=sys.stderr)
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
