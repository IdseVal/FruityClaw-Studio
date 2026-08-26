"""The dispatcher must refuse to act on an observation it could not make (issue #56).

Everything the dispatcher learns about the world arrives through `dispatch.run()`, so a
stub of that one function is enough to drive a whole tick: `FakeShell` answers `gh`, `git`
and `orca` from a repository state the test declares, and records every command attempted.
A test then asserts on what the tick did, not on what it logged.

Run directly (`python tests/test_dispatcher.py`) or through ctest as `dispatcher_tests`.
Exits 77 -- ctest's skip code -- when PyYAML is absent, since `dispatch.py` cannot be
imported without it and a build machine need not carry the dispatcher's dependencies.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
DISPATCHER_DIR = REPO_ROOT / ".orca" / "dispatcher"

try:
    import yaml  # noqa: F401
except ModuleNotFoundError:  # pragma: no cover - environment, not behaviour
    print("SKIP: PyYAML is not installed; dispatch.py cannot be imported")
    raise SystemExit(77)

sys.path.insert(0, str(DISPATCHER_DIR))
import dispatch  # noqa: E402

CORE_DOCUMENT = "# Core document\n\n> **Status: AGREED - 2026-08-22**\n"


def issue(number: int, labels: list[str], state: str = "OPEN") -> dict[str, Any]:
    """One row as `gh issue list --json` returns it."""
    return {"number": number, "title": f"issue {number}", "body": "",
            "labels": [{"name": name} for name in labels], "url": "", "state": state}


def worktree(name: str, branch: Optional[str] = None) -> dict[str, Any]:
    """One row as `orca worktree list --json` returns it."""
    return {"id": name, "path": "", "branch": "refs/heads/" + (branch or name),
            "displayName": name, "linkedIssue": None, "workspaceStatus": "",
            "repoId": "REPO", "isMainWorktree": False, "isArchived": False}


class FakeShell:
    """Stands in for `dispatch.run()`: answers by command shape, records every call.

    `fail` holds substrings of the joined command line that must come back as a failed run
    -- the stub-on-PATH from the issue's acceptance criteria, without a stub on PATH.
    """

    def __init__(self, issues: list[dict[str, Any]] = (), open_prs: list[dict[str, Any]] = (),
                 merged_prs: list[dict[str, Any]] = (),
                 worktrees: list[dict[str, Any]] = ()) -> None:
        self.issues = list(issues)
        self.open_prs = list(open_prs)
        self.merged_prs = list(merged_prs)
        self.worktrees = list(worktrees)
        self.fail: set[str] = set()
        self.calls: list[list[str]] = []
        # Set to a worktree name to make `orca worktree create` time out while Orca goes
        # ahead and creates the worktree anyway -- the issue's duplicate-audit trigger.
        self.create_times_out_but_succeeds: Optional[str] = None

    def commands(self, *prefix: str) -> list[list[str]]:
        """Every recorded call starting with `prefix`."""
        n = len(prefix)
        return [c for c in self.calls if tuple(c[:n]) == prefix]

    def _json(self, payload: Any) -> tuple[bool, str, str]:
        return True, json.dumps(payload), ""

    def _orca(self, result: Any) -> tuple[bool, str, str]:
        return True, json.dumps({"ok": True, "result": result}), ""

    def __call__(self, argv: list[str], timeout: int = 0) -> tuple[bool, str, str]:
        self.calls.append(list(argv))
        joined = " ".join(argv)
        if any(marker in joined for marker in self.fail):
            return False, "", "stubbed failure"

        if argv[0] == "git":
            return (True, CORE_DOCUMENT, "") if "show" in argv else (True, "", "")

        if argv[0] == "gh":
            if argv[1:3] == ["issue", "list"]:
                return self._json(self.issues)
            if argv[1:3] == ["pr", "list"]:
                state = argv[argv.index("--state") + 1]
                return self._json(self.open_prs if state == "open" else self.merged_prs)
            return True, "", ""

        if argv[0] == "orca":
            if argv[1:3] == ["repo", "show"]:
                return self._orca({"repo": {"id": "REPO"}})
            if argv[1:3] == ["worktree", "list"]:
                return self._orca({"worktrees": self.worktrees})
            if argv[1:3] == ["worktree", "create"]:
                name = argv[argv.index("--name") + 1]
                self.worktrees.append(worktree(name))
                if self.create_times_out_but_succeeds == name:
                    return False, "", "timed out after 180 seconds"
                return self._orca({"worktree": {"id": name}})
            if argv[1:3] == ["terminal", "list"]:
                return self._orca({"terminals": []})
            return self._orca({})

        return True, "", ""


class DispatcherTestCase(unittest.TestCase):
    """Each test gets its own state file, prompts directory, role files and shell stub."""

    def setUp(self) -> None:
        tmp = Path(tempfile.mkdtemp())
        self._patch(dispatch, "STATE_FILE", tmp / "state.json")
        self._patch(dispatch, "PROMPTS_DIR", tmp / "prompts")
        self._patch(dispatch, "ROLES_DIR", tmp / "roles")
        (tmp / "roles").mkdir()
        for role in dispatch.ROLE_FILES:
            (tmp / "roles" / f"{role}.md").write_text("role", encoding="utf-8")
        self.cfg = dispatch.load_config()
        self.state = dispatch.State()

    def _patch(self, module: Any, name: str, value: Any) -> None:
        old = getattr(module, name)
        setattr(module, name, value)
        self.addCleanup(setattr, module, name, old)

    def tick(self, shell: FakeShell) -> None:
        self._patch(dispatch, "run", shell)
        dispatch.tick(self.cfg, self.state)

    def saved_state(self) -> dict[str, Any]:
        if not dispatch.STATE_FILE.exists():
            return {}
        return json.loads(dispatch.STATE_FILE.read_text(encoding="utf-8"))


class DegradedObservation(DispatcherTestCase):

    def test_failed_gh_read_is_not_an_empty_repository(self) -> None:
        """The repository genuinely holds an open ready issue and `gh` cannot be read. The
        tick must spawn nothing, nudge nothing, escalate nothing and close nothing."""
        shell = FakeShell(issues=[issue(1, ["ready", "role:developer"])])
        shell.fail = {"gh issue list"}
        self.tick(shell)

        self.assertEqual(shell.commands("orca", "worktree", "create"), [])
        self.assertEqual(shell.commands("gh", "issue", "close"), [])
        self.assertEqual(shell.commands("gh", "issue", "edit"), [])
        self.assertEqual(shell.commands("gh", "issue", "comment"), [])
        self.assertEqual(self.saved_state().get("backlog", {}), {})
        self.assertEqual(self.state.observation["degraded_reads"], ["issues"])

    def test_failed_worktree_read_is_not_an_empty_orca(self) -> None:
        """Orca is the read that failed, and it is the read that would otherwise have
        hidden an audit worktree that already exists."""
        shell = FakeShell(worktrees=[worktree("backlog-audit-0")])
        shell.fail = {"orca worktree list"}
        self.tick(shell)
        self.assertEqual(shell.commands("orca", "worktree", "create"), [])

    def test_failed_fetch_leaves_the_gate_unobserved(self) -> None:
        """`git show` reads the local mirror and keeps succeeding after a failed fetch, so
        the gate must not be called open on the strength of it."""
        shell = FakeShell(issues=[issue(1, ["ready", "role:developer"])])
        shell.fail = {"fetch"}
        self.tick(shell)
        self.assertIn("core document", self.state.observation["degraded_reads"])
        self.assertEqual(shell.commands("orca", "worktree", "create"), [])

    def test_a_run_of_degraded_ticks_pages_once(self) -> None:
        shell = FakeShell(issues=[issue(1, ["ready", "role:developer"])])
        shell.fail = {"gh issue list"}
        for _ in range(6):
            self.tick(shell)
        self.assertEqual(self.state.observation["degraded_ticks"], 6)
        self.assertEqual(len(self.state.notified), 1)
        self.assertIn("observe:degraded", self.state.notified)

    def test_paging_waits_for_the_configured_run_length(self) -> None:
        shell = FakeShell()
        shell.fail = {"gh issue list"}
        self.cfg["dispatcher"]["degraded_ticks_before_page"] = 3
        self.tick(shell)
        self.assertNotIn("observe:degraded", self.state.notified)
        self.tick(shell)
        self.assertNotIn("observe:degraded", self.state.notified)
        self.tick(shell)
        self.assertIn("observe:degraded", self.state.notified)

    def test_recovery_clears_the_run(self) -> None:
        shell = FakeShell()
        shell.fail = {"gh issue list"}
        self.tick(shell)
        self.assertTrue(self.state.observation)
        shell.fail = set()
        self.tick(shell)
        self.assertEqual(self.state.observation, {})
        self.assertNotIn("observe:degraded", self.state.notified)


class BacklogAudit(DispatcherTestCase):

    def test_a_genuinely_drained_pipeline_still_spawns_an_audit(self) -> None:
        """The guard must not close the loop it exists to run."""
        shell = FakeShell()
        self.tick(shell)
        created = shell.commands("orca", "worktree", "create")
        self.assertEqual(len(created), 1)
        self.assertIn("backlog-audit-0", created[0])
        self.assertEqual(self.saved_state()["backlog"]["audit_epoch"], 0)

    def test_a_timed_out_spawn_does_not_produce_a_second_audit(self) -> None:
        """`orca worktree create` gives up at 180 s while Orca creates the worktree anyway.
        The next tick must adopt what is there rather than create a duplicate."""
        shell = FakeShell()
        shell.create_times_out_but_succeeds = "backlog-audit-0"
        self.tick(shell)
        self.assertEqual(len(shell.commands("orca", "worktree", "create")), 1)
        self.assertEqual(self.saved_state()["backlog"]["audit_epoch"], 0)

        self.tick(shell)
        self.assertEqual(len(shell.commands("orca", "worktree", "create")), 1)

    def test_a_spawn_that_really_failed_is_not_recorded(self) -> None:
        """Orca is up enough to answer `worktree list` and says the worktree is not there:
        nothing was spawned, so the attempt must not stand in the state file."""
        shell = FakeShell()
        shell.fail = {"orca worktree create"}
        self.tick(shell)
        self.assertEqual(self.saved_state().get("backlog", {}), {})

    def test_open_issues_prevent_an_audit(self) -> None:
        shell = FakeShell(issues=[issue(1, [])])
        self.tick(shell)
        self.assertEqual(shell.commands("orca", "worktree", "create"), [])


class ReadFailure(unittest.TestCase):
    """`FAILED` is falsy on purpose: callers that only ever wanted "nothing usable" are
    unchanged, and the callers that care can ask."""

    def test_failed_is_falsy_and_distinct_from_none(self) -> None:
        self.assertFalse(dispatch.FAILED)
        self.assertIsNot(dispatch.FAILED, None)
        self.assertEqual(dispatch.FAILED or [], [])

    def test_gh_json_separates_failure_from_emptiness(self) -> None:
        def shell(argv: list[str], timeout: int = 0) -> tuple[bool, str, str]:
            return (False, "", "boom") if "fail" in argv else (True, "[]", "")

        old = dispatch.run
        dispatch.run = shell
        try:
            self.assertIs(dispatch.gh_json(["fail"]), dispatch.FAILED)
            self.assertEqual(dispatch.gh_json(["issue", "list"]), [])
        finally:
            dispatch.run = old


if __name__ == "__main__":
    unittest.main(verbosity=2)
