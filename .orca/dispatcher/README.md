# The Dispatcher

A deterministic reconciler that runs the agent workflow. Not an agent. Its policy (the
human-readable version of the same rules) is `~/.orca/roles/dispatcher.md`; the full
behavioural contract is the docstring at the top of `dispatch.py`.

**One idea:** it never trusts an agent to report back. Every tick it observes GitHub
(issues, PRs, labels, diffs) and Orca (worktrees, terminal liveness), computes what should
be true, and does the difference. Every action is idempotent.

## Files

| File | What |
| --- | --- |
| `dispatch.py` | the reconciler; `run` / `once` / `status` / `doctor` / `onboard` |
| `install-task.ps1` | installs it as a Windows scheduled task (at logon, auto-restart) |
| `state.json` | de-dup memory only (cycles, nudges, spawns). Gitignored. Safe to delete. |
| `prompts/` | the brief files agents are pointed at. Gitignored. Useful for triage. |
| `dispatcher.log` | rotating log. Gitignored. |
| `dispatcher.lock` | one dispatcher per repo. |

## Commands

```powershell
python .orca\dispatcher\dispatch.py doctor --fix   # prerequisites; creates missing labels
python .orca\dispatcher\dispatch.py onboard        # PO & Analyst: first interview, or a revision round
python .orca\dispatcher\dispatch.py status         # the board: issues, PRs, sessions
python .orca\dispatcher\dispatch.py once --dry-run # what one tick would do
python .orca\dispatcher\dispatch.py run            # foreground loop (Ctrl+C to stop)
powershell -ExecutionPolicy Bypass -File .orca\dispatcher\install-task.ps1   # run forever
```

## What it does each tick

See the docstring in `dispatch.py` and `~/.orca/roles/dispatcher.md`. In one breath:
gate on the core document being on `dev`; dispatch `ready` issues whose `Depends on:` are
closed (one role per `role:*` label, up to `max_active_issues`); nudge idle sessions once,
then `needs-human`; PR with no state label -> docs-only gets `state:tested` automatically,
otherwise spawn the Tester; `state:tested` -> spawn the Reviewer, who merges into `dev`;
`state:blocked` -> back to the Developer's session under the 3-cycle breaker, else
`escalated`; merged -> close the issue, remove the worktree; `needs-human` -> page the
human and wait; pipeline drained -> spawn a PO & Analyst backlog audit, whose outcome
(new issues, or a PR marking the core document `Status: ACHIEVED`) is read from GitHub,
never from the agent; `ACHIEVED` on `dev` -> stop dispatching and page the human once
(`onboard` then starts a revision interview that reopens the gate).

## Tuning

`.orca/dispatch.yml` -> `dispatcher:` block (interval, concurrency, idle thresholds,
docs-only rules, `github_mention`). `circuit_breaker.max_cycles` (do not raise it).

## Failure modes

| What breaks | What happens |
| --- | --- |
| Orca not open | worktree/terminal calls fail; the tick logs it and retries next tick. Nothing is lost. |
| `gh` unauthenticated | the tick sees no issues/PRs and does nothing. `doctor` says so. |
| State file deleted | at most a duplicate comment or a second nudge; truth is in GitHub/Orca. |
| Agent ends without label/PR | nudge after `idle_minutes_before_nudge`, `needs-human` after `idle_minutes_after_nudge`. |
| Agent sets `needs-human` | human is @mentioned on GitHub (+ e-mail if SMTP set), Orca tab brought forward; dispatcher waits. |
| Two dispatchers | the lock file refuses the second. |
| Backlog audit ends with no outcome | nudge -> page the human; never re-audited until something merges. |

## Re-running an issue by hand

Remove `escalated`/`needs-human` as appropriate; if its worktree is gone the dispatcher
re-dispatches on the next tick (cycle continues counting). To reset the cycle count,
delete the issue's entry from `state.json`.
