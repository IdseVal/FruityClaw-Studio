# The Dispatcher (v3)

A deterministic reconciler that runs the agent workflow. Not an agent. The full
behavioural contract is the docstring at the top of `dispatch.py`; this file is the
operations crib sheet and the policy record (v0.1.1 kept the policy in a separate
`~/.orca/roles/dispatcher.md`; it lives here now).

**Two ideas:** (1) it never trusts an agent to report back -- every tick it observes
GitHub, the OS process table, and Orca, computes what should be true, and does the
difference, idempotently. (2) GitHub is the memory; a run is disposable -- agents are
headless `claude -p` processes that start, do one job, and exit. Nothing waits.

## Files

| File | What |
| --- | --- |
| `dispatch.py` | the reconciler; `run` / `once` / `status` / `doctor` / `onboard` / `pause` / `resume` |
| `install-task.ps1` | installs it as a Windows scheduled task (at logon, auto-restart) |
| `state.json` | de-dup memory only (cycles, pages, run PIDs). Gitignored. Safe to delete. |
| `prompts/` | the brief files runs are pointed at. Gitignored. Read them to triage. |
| `runs/` | one log per headless run: your window into what an agent did. Gitignored. |
| `dispatcher.log` | rotating log. Gitignored. |
| `dispatcher.lock` | one dispatcher per repo. |
| `paused.json` | the soft-pause marker for THIS project. Gitignored. Present = paused. |
| `dispatcher-pause.cmd` / `-resume.cmd` | the button: double-click, or pin to the taskbar. |

## Commands

```powershell
python .orca\dispatcher\dispatch.py doctor --fix   # prerequisites; creates missing labels
python .orca\dispatcher\dispatch.py onboard        # Planner interview (asks the autonomy question first)
python .orca\dispatcher\dispatch.py status         # the board: issues, runs, PRs
python .orca\dispatcher\dispatch.py once --dry-run # what one tick would do
python .orca\dispatcher\dispatch.py run            # foreground loop (Ctrl+C to stop)
python .orca\dispatcher\dispatch.py pause -m "why" # soft pause: start nothing new
python .orca\dispatcher\dispatch.py resume         # lift it
powershell -ExecutionPolicy Bypass -File .orca\dispatcher\install-task.ps1   # run forever
```

## What it does each tick

Gate on the core document being on `dev`. Dispatch `ready` issues whose `Depends on:`
are closed, up to `max_active_issues` LIVE runs: worktree (no terminal), brief file,
headless `claude -p` in that directory logging to `runs/`. The issue's labels pick the
role the run loads (`research` -> Planner, `architecture` -> Architect, else the
Implementer). Kill any run past
`max_run_minutes` (whole tree); one fresh retry for a run that exits without a PR, then
`needs-human`. PRs are verified, reviewed and merged by the CI pipeline
(`.github/workflows/agent-pipeline.yml`); the dispatcher only reacts: `state:blocked` ->
a fresh headless fix run whose brief embeds the blocker's comments (breaker at 3 total
starts per issue -> `escalated`); `needs-human` -> page once and touch nothing until the
label is gone; merged -> close the issue, remove the worktree, kill any lingering run.
Pipeline fully drained -> per the autonomy dial, a headless Planner audit files
`proposed`/`ready` issues or opens the ACHIEVED PR; an audit that produces nothing while
nothing has merged since pages the human instead of looping.

## Pause

`pause` writes `paused.json`; `resume` deletes it; the running dispatcher reads it every
tick, so neither needs a restart. A pause is **soft and per-project**: it blocks the two
places the dispatcher creates NEW work (dispatching a `ready` issue, spawning an audit).
Fix runs, merges, issue closing and cleanup continue, so the fleet **drains**. It cannot
un-start a headless run that is already going -- but every run has a hard
`max_run_minutes` ceiling, so "already going" is now a bounded promise, not an open one.
A corrupt marker counts as paused: a bad file must never quietly restart the spend.

## Failure modes

| What breaks | What happens |
| --- | --- |
| Orca not open | worktree calls fail; the tick logs it and retries. Nothing is lost. |
| `gh` unauthenticated | the tick sees no issues/PRs and does nothing. `doctor` says so. |
| State file deleted | at most a duplicate comment; PIDs are re-checked against the OS, so nothing orphans. |
| Run exceeds `max_run_minutes` | killed (whole tree); breaker counts it. |
| Run exits without a PR | one fresh retry, then `needs-human` with a pointer at its log. |
| CI blocks a PR | fresh fix run with the comments in its brief; breaker at 3. |
| Agent sets `needs-human` | the item is flagged and appears in the daily digest's "Waiting on you"; nothing moves on it until you remove the label. |
| CI pipeline never ran on a PR | check the repo's Actions tab; usually OWNER STEP 1 or 2 was skipped. |
| Two dispatchers | the lock file refuses the second. |
| Human pauses the project | no new dispatches or audits; in-flight work still lands. |

## Re-running an issue by hand

Remove `escalated`/`needs-human` as appropriate; the dispatcher re-dispatches on the next
tick (cycle continues counting). To reset the cycle count, delete the issue's entry from
`state.json`. To watch a live run: `tail -f .orca/dispatcher/runs/<name>.log` (or just
open the file; PowerShell: `Get-Content -Wait`).
