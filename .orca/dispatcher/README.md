# The Dispatcher

The Dispatcher is often mistaken for a chat agent because its policy sits next to the
six real agent prompts at `~/.orca/roles/dispatcher.md` (a shared, host-level directory
that all projects use). **It is not an agent.** It is a running process on the host:
this directory.

- `dispatch.py` — the poller. Runs on the host (not inside the dev container),
  because it invokes `orca` (a host desktop app) and `gh`.
- `state.json` — what has been dispatched so far. Gitignored. Delete to reset.
- `~/.orca/roles/dispatcher.md` — the **policy** the poller enforces. When you
  change one, change the other.

## What it does, in order

1. Calls `gh issue list --state open --label ready --json …` every `--interval` seconds.
2. Filters out issues already in `state.json` and issues carrying the `escalated` label.
3. For each remaining issue: computes the pipeline (`trivial` if labelled `trivial` and
   free of `ui`/`scraper`/`data`; else `default`), computes the skills earned by
   labels per `../dispatch.yml`, and builds a Developer prompt from
   `~/.orca/roles/developer.md` + issue body + dispatch context.
4. `orca worktree create --repo path:<repo> --name feature/issue-<n> --issue <n>
   --base-branch dev --agent claude --prompt "<built prompt>"`.
5. Records the dispatch in `state.json` and comments on the issue with the worktree name
   and cycle count.
6. If an issue is re-labelled `ready` after a failed round (its `state.json` entry is
   removed by hand, or its cycle count is bumped), the poller dispatches it again — up
   to `circuit_breaker.max_cycles` (3). The next attempt adds the `escalated` label,
   posts `Circuit breaker tripped: 3 failed attempts.`, and stops touching the issue.

## Prerequisites on the host

- Python 3.9+ and PyYAML (`pip install pyyaml`).
- `gh` authenticated for this repo (`gh auth status`).
- `orca` on PATH, and Orca open (`orca status` reports `runtimeReachable: true`).
- Labels on the GitHub repo: `ready`, `escalated`, plus the routing labels from
  section 8 of `agent-workflow-setup.md` (`ui`, `seo`, `scraper`, `bug`, `data`,
  `trivial`).

## Running it

**Foreground, one terminal (simplest):**

```bash
python .orca/dispatcher/dispatch.py
```

Ctrl+C to stop. Poll interval defaults to 60s; override with `--interval 30`.

**Cron / Orca automation (`--once`):**

```bash
python .orca/dispatcher/dispatch.py --once
```

Runs one poll pass and exits. Wire this into an Orca automation (`orca automations
create`) or a Task Scheduler entry to poll at whatever cadence you want. `state.json`
survives across runs.

**Windows Task Scheduler**: create a Basic Task that runs
`python C:\path\to\repo\.orca\dispatcher\dispatch.py --once` every 5 minutes.

## Failure modes and what happens

| What breaks | What the poller does |
| --- | --- |
| `gh` not installed / unauthenticated | Prints the error to stderr, treats the tick as "no ready issues". Retries on the next tick. |
| Orca not open | `orca worktree create` fails; the issue stays undispatched and is retried on the next tick. No cycle is spent. |
| Config missing (`.orca/dispatch.yml`) | The poller raises on startup — a bad config must not silently produce a no-op dispatcher. |
| `state.json` corrupted | Warned to stderr; the poller starts with an empty state. Every open ready issue is re-dispatched — which is intentional: better to duplicate than to silently drop. |
| Ctrl+C mid-tick | The 1-second sleep slices catch the signal; the current subprocess call is allowed to finish, then the loop exits. |

## Re-dispatching a failed issue

An issue whose worker did not open a merged PR is still in `state.json`. To retry it:

1. Fix whatever the worker got stuck on (spec, prompt, environment).
2. Bump its cycle count manually by editing `state.json`, OR delete its entry outright
   (which resets the cycle to 0 — use this when the previous failure was the
   Dispatcher's fault, not the worker's).
3. Ensure the issue still carries the `ready` label and not `escalated`.

## The circuit breaker

`max_cycles: 3` in `.orca/dispatch.yml`. Do not raise it to force something through: an
issue that has failed three times is telling you the specification is wrong. The fourth
attempt at the wrong thing costs more than asking.
