# The ORCA ADE agent workflow: complete setup (v2)

How to build this environment from an empty repository. **Every file is listed in full**, so
the whole workflow is constructable from this document alone -- including the dispatcher
runtime, the role prompts, and the final instructions for the human owner.

Project-agnostic. Two things must be substituted wherever they appear; nothing else is
tied to a particular project:

| Where | Replace |
| --- | --- |
| `.orca/dispatch.yml` -> `notify.github_mention` (embedded below as `"@IdseVal"`) | the new owner's GitHub handle |
| `PROJECT_DATA_HOST_DIR` (dev container mount, section 5) | the project's large-data path, or delete the mount |

**If you are a Claude instance performing this setup:** work top to bottom. Sections 3-8
create files; section 9 is the bring-up order with verification after each step; section 17
is what you hand the human at the end. Do not improvise around a failing check -- every
check exists because the naive path silently breaks the workflow. The version of this
document you are reading was produced *after* running the workflow on a real project; the
pitfalls in section 15 all actually happened.

---

## Contents

1.  [What you are building](#1-what-you-are-building)
2.  [Prerequisites](#2-prerequisites)
3.  [Repository layout](#3-repository-layout)
4.  [Step 1 -- the core document](#4-step-1----the-core-document)
5.  [Step 2 -- the dev container (optional)](#5-step-2----the-dev-container-optional)
6.  [Step 3 -- skills, installed on the host](#6-step-3----skills-installed-on-the-host)
7.  [Step 4 -- the roles, installed on the host](#7-step-4----the-roles-installed-on-the-host)
8.  [Step 5 -- the dispatcher](#8-step-5----the-dispatcher)
9.  [Step 6 -- bring-up, in order](#9-step-6----bring-up-in-order)
10. [The circuit breaker](#10-the-circuit-breaker)
11. [The FMEA protocol](#11-the-fmea-protocol)
12. [Evidence before merge](#12-evidence-before-merge)
13. [The daily digest](#13-the-daily-digest)
14. [Branch protection](#14-branch-protection)
15. [Known pitfalls -- all of these actually happened](#15-known-pitfalls----all-of-these-actually-happened)
16. [Checklist](#16-checklist)
17. [The owner's runbook](#17-the-owners-runbook)

---

## 1. What you are building

```
                    +---------------------------+
                    |      HUMAN OWNER          |
                    |  PO interview - FMEA      |
                    |  interviews - needs-human |
                    |  pings - dev -> main PR   |
                    +------------+--------------+
                                 |
                                 v
    +--------------------------------------------------------+
    |            DISPATCHER  (deterministic process)         |
    |  a reconciler: observes GitHub + Orca every 60s,       |
    |  computes what should be true, does the difference     |
    |  runs as a Windows scheduled task -- no chat session   |
    +--+----------+-----------+-----------+----------+-------+
       |          |           |           |          |
       v          v           v           v          v
  +--------+ +---------+ +---------+ +--------+ +--------+
  |  PO &  | |Architect| |Research | |Develop | | Tester |
  | Analyst| |         | |   er    | |   er   | |        |
  +--------+ +---------+ +---------+ +--------+ +---+----+
   issues on    ADRs +      facts +     PRs to      |
   the board    specs       evidence    dev     +---v----+
                                                |Reviewer|
                                                | merges |
                                                | to dev |
                                                +--------+
```

**The one idea that makes it work.** The dispatcher never trusts an agent to report back.
Agents end their sessions without saying so; prompts get truncated; testers meet PRs with
nothing to test and quietly stop. Every failure mode of the first version of this workflow
traced to trusting agents to move the pipeline. So the dispatcher is a *reconciler*, like a
thermostat: every tick it observes reality (GitHub issues, PRs, labels, diffs; Orca
worktrees and terminal liveness), computes what the rules say should be true, and does the
difference. Every action is idempotent -- crashes, restarts and double runs are harmless.

**Signals, not memory.** The pipeline state lives in GitHub labels, not in anyone's recall:

| Signal | Meaning |
| --- | --- |
| issue: `role:developer` / `role:architect` / `role:researcher` | which role prompt the issue loads (exactly one) |
| issue: `ready` | may be dispatched once its `Depends on: #n` issues are closed |
| issue body: `Depends on: #a, #b` | phase ordering; the dispatcher holds the issue until those are closed |
| issue: `trivial` | tester skipped (reviewer never is) |
| PR: `state:tested` | tester passed (set automatically for docs-only PRs) -> reviewer |
| PR: `state:blocked` | back to the developer, cycle +1, breaker at 3 |
| either: `needs-human` | the human is paged and the dispatcher waits |
| issue: `escalated` | circuit breaker tripped; human resolves |

**Where each part runs:**

```
HOST                                          CONTAINER (optional)
  Orca (desktop app)  -- dispatches agents      repo + pinned toolchain
  dispatcher (scheduled task) -- the loop       reproducible tests/builds
  ~/.orca/roles/      -- role prompts, shared
  ~/.claude/skills/   -- skills, shared
```

Orca is a desktop application on the host; it cannot live inside a container and it is not
on npm. GitHub webhooks have nowhere to land on a desktop machine, so the dispatcher POLLS
`gh` (60s tick). Roles and skills are **host-level and shared across every project** this
workflow drives -- never copied per repository. That is a deliberate, repeatedly confirmed
convention: per-project copies drift.

**The human's touchpoints, in total:** the PO interview; FMEA interviews; `needs-human`
pings; opening the `dev -> main` pull request. Everything else runs itself.

### Isolation: decide which one you want

| You want | Do this | Cost |
| --- | --- | --- |
| **Reproducibility** -- same toolchain everywhere | the dev container of section 5. No `docker.sock`. | none |
| **Containment** -- agents cannot reach the host | rootless Docker, sysbox, or a VM | real setup work |

Mounting `/var/run/docker.sock` gives the container root-equivalent control of the host.
It is not isolation. This workflow runs its agents on the HOST via Orca; the container is
for reproducible builds and tests, not a security boundary.

---

## 2. Prerequisites

All on the host. Verify each before starting; `dispatch.py doctor` (section 8) re-checks
most of them at any time.

| Tool | Why | Check |
| --- | --- | --- |
| Orca (desktop app) | dispatches agents, owns worktrees | `orca status` -> `runtimeReachable: true` |
| GitHub CLI, authenticated with push rights | the dispatcher's eyes and hands | `gh auth status` |
| Python >= 3.9 + PyYAML | the dispatcher runtime | `python -c "import yaml"` |
| `pythonw.exe` on PATH (Windows) | the scheduled task runs without a console | `where pythonw` |
| Node >= 22.20 | the `skills` CLI floor | `node --version` |
| Claude account signed into Orca | the agents | `orca account list` |
| Docker Desktop + VS Code Dev Containers | only if you keep section 5 | `docker info` |

A GitHub repository with a remote must exist, with two branches: `main` (default, protected,
human-only) and `dev` (integration; the Reviewer merges here). Create `dev` from `main`
before anything else and push it.

---

## 3. Repository layout

```
.orca/
  dispatch.yml            config: roles->skills, labels, gates, breaker, dispatcher tuning
  setup_skills.sh         installs every skill GLOBALLY on the host
  dispatcher/
    dispatch.py           the reconciler (full source in section 8)
    install-task.ps1      registers it as a Windows scheduled task
    README.md             operations crib sheet
    state.json            (gitignored) de-dup memory; safe to delete
    prompts/              (gitignored) the brief files agents are pointed at
    dispatcher.log        (gitignored)
docs/
  CORE_DOCUMENT.md        single source of truth; everything derives from it
  specs/                  docs/specs/<topic>.md
  adrs/                   docs/adrs/ADR-<issue#>-<slug>.md   (numbered by ISSUE, never a counter)
  research/               docs/research/issue-<issue#>-<slug>.md
mail/
  daily_digest.py         digest derived from git + merged PRs
.github/workflows/
  daily-digest.yml
.devcontainer/            optional; section 5
.gitattributes            *.sh pinned to LF
.gitignore
```

The seven role prompts are **not** in the repo. They live at `~/.orca/roles/` on the host,
shared by every project (section 7). Skills likewise live at `~/.claude/skills/` (section 6).

Create the tree:

```bash
mkdir -p .orca/dispatcher docs/adrs docs/specs docs/research mail .github/workflows .devcontainer
```

### `.gitignore`

```
mail/__pycache__/
.orca/dispatcher/__pycache__/
.orca/dispatcher/state.json
.orca/dispatcher/state.tmp
.orca/dispatcher/prompts/
.orca/dispatcher/dispatcher.lock
.orca/dispatcher/dispatcher.log*
```

### `.gitattributes`

```gitattributes
* text=auto eol=lf

# Executed inside the Linux dev container, which bind-mounts the working tree rather than a
# fresh checkout. A CRLF here is invisible on Windows and fails in the container with
# `set: pipefail: invalid option name`, which points nowhere near the real cause.
*.sh text eol=lf
```

---

## 4. Step 1 -- the core document

**Every project starts from a core document, and nothing else exists until it does.** It is
the single source of truth; every later artefact -- specs, ADRs, issues, tests -- derives
from it. The PO & Analyst populates it by **deep interview with the human**: never
inferred, never defaulted.

The dispatcher enforces this mechanically: **no issue is dispatched while the copy on
`dev` still contains `Status: EMPTY`** (the `gates.core_document` block in
`dispatch.yml`). The PO's onboarding PR opens the gate by merging.

Create the template:

```markdown
# Core document

> Populated by deep interview with the project owner. Nothing here is inferred.
> Status: EMPTY -- run the onboarding interview.

## 1. Purpose and success criteria
## 2. Target users
## 3. Scope
## 4. Explicit NON-scope
## 5. Domain model and vocabulary
## 6. Data sources and their constraints
## 7. External systems
## 8. Legal, privacy and compliance limits
## 9. What must never happen
## 10. Open questions
```

Save as `docs/CORE_DOCUMENT.md`, commit to `dev`, push. Two rules govern the interview
(the full protocol is in the PO & Analyst role, section 7):

- **Interview in rounds.** Ask, record, read back, ask what is now wrong or missing.
- **An unknown is recorded as OPEN, never guessed.** A guess written down is a defect
  shipped, and every later artefact inherits it.

> If the project already has a core document, **port it rather than re-interviewing**, then
> change the Status line and open the PR to `dev` the same way.

---

## 5. Step 2 -- the dev container (optional)

Reproducible toolchain for tests and builds. The workflow itself does not depend on it --
agents run on the host via Orca -- so a project with a simple toolchain may skip this
section entirely. Keep it when "a test that passes here passes everywhere" matters.

### `.devcontainer/Dockerfile`

```dockerfile
# The reproducible toolchain every agent runs inside.
#
# It pins the toolchain so a test that passes here passes on any machine, and it is the shape
# the project deploys in later. It is NOT a sandbox: the container is not a security boundary.
#
# Orca is not installed. It is a desktop application on the host that dispatches agents and
# owns the worktrees; it cannot live inside the environment it dispatches into, and it is not
# published to npm at all.

FROM mcr.microsoft.com/devcontainers/typescript-node:1-22-bookworm

# git and gh: the daily digest is derived from commit and pull-request history, so the
# container reads both. jq keeps shell-side JSON handling out of Python.
RUN apt-get update && apt-get install -y --no-install-recommends \
        git \
        gh \
        jq \
        curl \
        python3 \
        python3-pip \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

# The `1-22` tag ships Node 22.16, BELOW the 22.20.0 floor the `skills` CLI declares. npm runs
# it anyway with an EBADENGINE warning; a toolchain that works by warning breaks the day npm
# enforces it. `n` pins the interpreter above the floor whatever the base image moves to.
#
# Nothing is chained onto this step. `n` replaces /usr/local/bin/node underneath the running
# shell, so a subsequent npm in the same RUN resolves to the binary just swapped out.
RUN npm install -g n && n 22.20.0

# Debian marks the system interpreter externally managed, so a virtualenv is the supported way
# to install into it. Put it OUTSIDE the workspace: a bind-mounted workspace shared with a
# Windows or macOS host would otherwise have the container's Linux venv overwrite the host's,
# leaving neither working.
ENV VIRTUAL_ENV=/opt/venv
RUN python3 -m venv "$VIRTUAL_ENV"
ENV PATH="$VIRTUAL_ENV/bin:$PATH"

WORKDIR /workspace
```

### `.devcontainer/devcontainer.json`

```jsonc
{
  "name": "Agent workspace",

  "build": { "dockerfile": "Dockerfile" },
  "workspaceFolder": "/workspace",

  // Large datasets are mounted, not copied, and mounted READ-ONLY: a container that can
  // delete the data can destroy something expensive or impossible to rebuild. The host path
  // is read from the environment so no machine-specific path is committed. Leave the variable
  // unset and the container still opens; only the commands needing that data are unavailable.
  "mounts": [
    "source=${localEnv:PROJECT_DATA_HOST_DIR},target=/data,type=bind,readonly"
  ],

  // No docker.sock. See "Isolation" above.

  "remoteEnv": {
    "PROJECT_DATA_DIR": "/data"
  },

  "postCreateCommand": "bash .devcontainer/post-create.sh",

  "customizations": {
    "vscode": {
      "extensions": [
        "ms-python.python",
        "charliermarsh.ruff",
        "dbaeumer.vscode-eslint",
        "esbenp.prettier-vscode"
      ],
      "settings": {
        "python.defaultInterpreterPath": "/opt/venv/bin/python"
      }
    }
  }
}
```

### `.devcontainer/post-create.sh`

Adapt the two dependency installs to the project's layout.

```bash
#!/usr/bin/env bash
#
# Everything the container needs that the image cannot bake in: project dependencies, which
# change with the lockfiles, and the agent skills, which are fetched from GitHub.
#
# Deliberately NOT `set -e` around the whole file. A failed skill fetch is a network problem
# and must not leave a developer with no container at all; a failed dependency install must.
set -uo pipefail

cd /workspace

echo "==> Python dependencies"
set -e
python -m pip install --quiet --upgrade pip
python -m pip install --quiet -e "backend[dev]" -c backend/constraints.txt
set +e

echo "==> Node dependencies"
if [ -f frontend/package-lock.json ]; then
  (cd frontend && npm ci --no-audit --no-fund) || {
    echo "!! npm ci failed. The container is usable; run it by hand." >&2
  }
fi

echo "==> Agent skills"
if bash .orca/setup_skills.sh; then
  echo "    skills installed"
else
  echo "!! Skill installation failed — usually network or a GitHub rate limit." >&2
  echo "   Re-run: bash .orca/setup_skills.sh" >&2
fi
```

---

## 6. Step 3 -- skills, installed on the host

Skills are installed **globally** (`~/.claude/skills/`) so every Claude session on the
host has them, in every project -- the same shape as the roles. The first version of this
workflow installed them only inside the dev container; since agents run on the host, no
agent ever had a skill. Do not repeat that.

The CLI is [`skills`](https://github.com/vercel-labs/skills). Three facts that differ from
most written instructions: the verb is **`add`** (not install); scope is `--global` (there
is no `--target`); the agent id is **`claude-code`** (plain `claude` is rejected).

### The skill matrix

| Role / label | Skills | Repository |
| --- | --- | --- |
| every role | `handoff` | `mattpocock/skills` |
| po-analyst | `grill-with-docs`, `domain-modeling` | `mattpocock/skills` |
| architect | `improve-codebase-architecture`, `codebase-design` | `mattpocock/skills` |
| researcher | `just-scrape` | `scrapegraphai/just-scrape` |
| researcher, label `bug` | `diagnosing-bugs` | `mattpocock/skills` |
| label `ui` | `frontend-design` | `anthropics/skills` |
| label `ui` | `web-design-guidelines` | `vercel-labs/agent-skills` |
| label `ui` | `high-end-visual-design` | `leonxlnx/taste-skill` |
| label `seo` | `seo-audit` | `coreyhaines31/marketingskills` |
| label `scraper` | `just-scrape` | `scrapegraphai/just-scrape` |
| tester | `tdd` | `mattpocock/skills` |
| tester | `webapp-testing` | `anthropics/skills` |
| reviewer | `code-review` | `mattpocock/skills` |

> `just-scrape` needs a ScrapeGraph API key to actually run; without one it hangs on an
> interactive prompt. The researcher role therefore says: use it when configured, otherwise
> fetch directly. Tell the owner (section 17) it is optional.

### `.orca/setup_skills.sh`

```bash
#!/usr/bin/env bash
#
# Fetch every agent skill this workspace dispatches, into the project scope.
#
# The CLI verb is `add`, not install. There is no --target. The agent id is `claude-code`.
# Node >= 22.20.0 is required; the Dockerfile pins it upward with `n`.
#
# Every skill name here was read back from the repository with `--list` rather than copied
# from a plan. A name that is wrong fails the container build for everyone.
set -uo pipefail

SKILLS_CLI="${SKILLS_CLI:-npx -y skills@latest}"
AGENT="${SKILLS_AGENT:-claude-code}"

# Roles live at ~/.orca/roles/ and are shared across every project this workflow
# drives. Skills are the same shape: install them GLOBALLY so every Claude Code
# session on this host has them, regardless of which project it runs in. Override
# with SKILLS_SCOPE=project to install into the current directory's .claude/skills/.
SKILLS_SCOPE="${SKILLS_SCOPE:-global}"
scope_flag=""
if [ "$SKILLS_SCOPE" = "global" ]; then
  scope_flag="--global"
fi

failed=()

# add <owner/repo> <skill> [<skill>...]
add() {
  local repo="$1"; shift
  local skill
  for skill in "$@"; do
    printf '  %-34s %s\n' "$skill" "($repo)"
    # shellcheck disable=SC2086
    if ! $SKILLS_CLI add "$repo" --skill "$skill" --agent "$AGENT" $scope_flag --yes >/dev/null 2>&1; then
      failed+=("$repo@$skill")
    fi
  done
}

echo "Installing agent skills (agent: $AGENT)"

add mattpocock/skills handoff
add mattpocock/skills grill-with-docs domain-modeling
add mattpocock/skills improve-codebase-architecture codebase-design
add anthropics/skills frontend-design
add vercel-labs/agent-skills web-design-guidelines
add leonxlnx/taste-skill high-end-visual-design
add coreyhaines31/marketingskills seo-audit
add scrapegraphai/just-scrape just-scrape
add mattpocock/skills diagnosing-bugs
add mattpocock/skills tdd
add anthropics/skills webapp-testing
add mattpocock/skills code-review

echo
if [ ${#failed[@]} -eq 0 ]; then
  echo "All skills installed."
else
  echo "Failed (${#failed[@]}):" >&2
  printf '  %s\n' "${failed[@]}" >&2
  echo "Re-run this script; GitHub rate limits are the usual cause." >&2
  exit 1
fi
```

Run it once on the host: `bash .orca/setup_skills.sh` (defaults to `--global`).
Verify: `ls ~/.claude/skills` shows all fourteen.

---

## 7. Step 4 -- the roles, installed on the host

**Role prompts are HOST-LEVEL at `~/.orca/roles/` and shared across every project this
workflow drives. Never create a per-project copy** (`.orca/system_prompts/` or similar) --
a per-project copy is the drift trap that broke the first version of this setup, twice.
Populate the directory **once per machine** from the seven files below; editing one later
changes it for all projects, which is intended.

```bash
mkdir -p ~/.orca/roles
# then write each block below to the path in its heading
```

Six agent prompts plus one policy file. `dispatcher.md` is NOT an agent prompt -- it is the
human-readable policy the dispatcher process enforces; when one changes, change the other.

| Role | Host path | Owns | May merge |
| --- | --- | --- | --- |
| Dispatcher (policy) | `~/.orca/roles/dispatcher.md` | enforced by `.orca/dispatcher/dispatch.py` | nothing |
| PO & Analyst | `~/.orca/roles/po-analyst.md` | core document, specs, ADRs, issues | nothing |
| Architect | `~/.orca/roles/architect.md` | contracts, schemas, boundaries | nothing |
| Researcher | `~/.orca/roles/researcher.md` | facts + evidence | nothing |
| Developer | `~/.orca/roles/developer.md` | one issue in one worktree | nothing |
| Tester | `~/.orca/roles/tester.md` | FMEA, test suites, `state:*` labels | nothing |
| Reviewer | `~/.orca/roles/reviewer.md` | standards, spec compliance | **`dev` only** |

Nobody merges to `main`. That is the human's, always.

### A. `~/.orca/roles/dispatcher.md`

```markdown
# Dispatcher policy

**This is a POLICY document, not an agent prompt.** The Dispatcher is a deterministic
Python process on the host, shipped in every project at `.orca/dispatcher/dispatch.py` and
installed as a scheduled task. This file records the rules that process enforces. If you
are an LLM and someone handed you this file expecting you to *be* the Dispatcher: stop,
say so, and point them at `dispatch.py doctor` / `dispatch.py status`.

This file lives at `~/.orca/roles/dispatcher.md` -- the host-level roles directory shared
by every project. Editing it changes the policy for all projects; change the script too.

## The one idea

The dispatcher never trusts an agent to report back. Every tick it observes reality
(GitHub issues, PRs, labels, diffs; Orca worktrees and terminal liveness), computes what
should be true, and does the difference. All actions are idempotent.

## Labels it reads and writes

| Label | Set by | Meaning |
| --- | --- | --- |
| `role:developer` / `role:architect` / `role:researcher` | PO & Analyst | Which role prompt (`~/.orca/roles/<role>.md`) the issue loads. Exactly one. |
| `ready` | PO & Analyst (or human) | The issue may be dispatched once its dependencies are closed. |
| `trivial` | PO & Analyst | Tester is skipped (Reviewer never is). Refused if `ui`, `scraper` or `data` is also present. |
| `ui` `seo` `scraper` `bug` `data` | PO & Analyst | Skill routing (see `.orca/dispatch.yml`). |
| `state:tested` | Tester, or dispatcher for docs-only/trivial PRs | PR passed; Reviewer next. |
| `state:blocked` | Tester or Reviewer | PR goes back to the Developer; cycle +1. |
| `needs-human` | Any agent | The human is needed in that Orca tab. Dispatcher pages the human and stops nudging. |
| `escalated` | Dispatcher | Circuit breaker tripped (3 cycles). Human resolves. |

## Issues

1. Gate: nothing is dispatched while `docs/CORE_DOCUMENT.md` on `dev` still says
   `Status: EMPTY`. The PO's onboarding PR opens the gate when it merges.
2. Candidate: open, `ready`, exactly one `role:*`, not `escalated`/`needs-human`, every
   issue named in a `Depends on: #a, #b` / `Blocked by:` line is closed, no worktree yet,
   and fewer than `max_active_issues` issues are in flight.
3. Dispatch: Orca worktree `issue-<n>` off `dev`, agent started with a one-line prompt
   pointing at a brief file (`.orca/dispatcher/prompts/issue-<n>-cycle<k>.md`). The brief
   says: read `~/.orca/roles/<role>.md`, `gh issue view <n>`, the core document and specs;
   do the work; `git push -u origin HEAD` and `gh pr create --base dev`; stop.
4. Liveness: a session idle `idle_minutes_before_nudge` without a PR gets one nudge; still
   idle after `idle_minutes_after_nudge` -> `needs-human`.

## Pull requests (base `dev`)

1. No `state:*` label: docs-only diff (only `docs/`, `*.md`, `*.txt`) or issue `trivial`
   -> dispatcher sets `state:tested` itself. Otherwise -> spawn the **Tester** as a new
   terminal in the developer's worktree, with a brief. The Tester ends with exactly one of
   `state:tested` / `state:blocked` plus a comment.
2. `state:tested` -> spawn the **Reviewer** the same way. The Reviewer ends with
   `gh pr merge <n> --squash --delete-branch` (into `dev`) or `state:blocked` + comment.
3. `state:blocked` -> issue cycle +1. Under `max_cycles`: the blocker's comments are sent
   into the developer's session ("fix, push, `gh pr edit <n> --remove-label state:blocked
   --remove-label state:tested`"); the tester then runs again. Over `max_cycles`: issue
   labelled `escalated`, human paged.
4. Tester/Reviewer idle without an outcome -> one nudge (re-sends the brief) -> `needs-human`.
5. Merged -> the dispatcher closes the linked issue (merging into `dev` does not
   auto-close issues; only the default branch does), marks the worktree completed and
   removes it. Closing the issue satisfies other issues' `Depends on:`, so phases advance
   by themselves.

## Human contact

`needs-human` on an issue or PR: the dispatcher comments with an `@mention` (GitHub
e-mails it), brings the Orca tab forward, sets a board comment, and sends SMTP mail if
`SMTP_*` variables are set. The human answers *in that agent's session*; the agent removes
the label when it has what it needs. The human's only other touchpoints: the PO interview,
FMEA interviews, and opening the `dev -> main` pull request.

## Merge control

The Dispatcher is STRICTLY FORBIDDEN from merging any branch into `main`. It does not
merge into `dev` either -- that is the Reviewer's role. Only the human merges `dev -> main`.
```

### B. `~/.orca/roles/po-analyst.md`

```markdown
You are the PO & Analyst Agent.

RESPONSIBILITIES:
1. Own `docs/CORE_DOCUMENT.md`. Every project starts from it and every later artefact is
   derived from it. It is the single source of truth for what this project is.
2. Populate it by DEEP INTERVIEW with the human. Never infer, never assume, never fill a gap
   with a plausible default.
3. Once it is agreed and merged into `dev`: derive `docs/specs/*.md` and ADRs, and write the
   GitHub Issues that drive all further work.

INTERVIEW PROTOCOL:
- The core document comes FIRST. No architecture, no issues and no code exist until it is
  agreed with the human.
- Interview in rounds. Each round: ask, record the answer in the document, read it back, ask
  what is now wrong or missing.
- Interrogate rather than collect. Push until a boundary is sharp enough that a developer
  could not build the wrong thing without noticing.
- Cover at minimum: purpose and success criteria; target users; scope and explicit
  NON-scope; the domain model and its vocabulary; data sources and their constraints;
  external systems; legal, privacy and compliance limits; what must never happen.
- Where the human does not know, record it as OPEN with their name against it. An open
  question written down is a decision waiting; a guess written down is a defect shipped.
- Use `grill-with-docs` for the interview and `domain-modeling` to fix the vocabulary. One
  name per concept, written down, used everywhere afterwards.

HOW YOU FINISH PHASE 1 (the document):
- When the human says it is agreed: replace `Status: EMPTY` with `Status: AGREED -- <date>`,
  commit, `git push -u origin HEAD`, and open a pull request into `dev`
  (`gh pr create --base dev --title "Populate core document from owner interview"`).
  The pipeline reviews and merges it. Nothing can be dispatched before it is on `dev`.

PHASE 2 (after the PR is merged):
- Specs in `docs/specs/<topic>.md`. ADRs in `docs/adrs/ADR-<issue#>-<slug>.md`, numbered by
  the GitHub issue that produced them -- never by a running counter, so parallel agents
  cannot collide. An ADR records ONE decision, with the alternatives rejected and why.
- Issues. Each states what is wanted, how anyone will know it worked, and what is out of
  scope. Labels: exactly one `role:*` (`role:researcher`, `role:architect`,
  `role:developer`); skill labels `ui` `seo` `scraper` `bug` `data` where they apply;
  `trivial` when a tester has nothing to run; `ready` when the issue may start.
- Ordering is expressed ONLY by a body line `Depends on: #a, #b`. The dispatcher holds an
  issue until every dependency is closed. Typical: research first; architecture depends on
  research; implementation depends on architecture. Mark everything `ready`; the
  dependencies do the sequencing.
- `docs/CORE_DOCUMENT.md` is living. When a decision changes it, update it (through a PR)
  and say what changed; never let a spec contradict it silently.

FORBIDDEN:
- You do not write implementation code.
- You are STRICTLY FORBIDDEN from merging any branch into `main`. You do not merge into
  `dev` either; the Reviewer does.
```

### C. `~/.orca/roles/architect.md`

```markdown
You are the Architect Agent.

RESPONSIBILITIES:
1. Read `docs/CORE_DOCUMENT.md` and the specs derived from it.
2. Produce and FREEZE the system contracts on `dev` before feature work opens: interface
   types, schemas, API shapes, module boundaries.
3. Record each boundary decision as an ADR in `docs/adrs/`.
4. Define the repository layout and the seams new work must fit into.

DESIGN RULES:
- Prefer a deep module with a narrow interface to a shallow one with a wide interface.
- Contracts are frozen before feature worktrees open against them. A contract that changes
  while three worktrees depend on it is not a contract.
- If a frozen contract turns out to be wrong, STOP the work that depends on it, say so, and
  change it deliberately. Never let it drift.
- Never widen an interface to unblock one caller.
- Use `improve-codebase-architecture` and `codebase-design`.

FILES AND NAMES (parallel architects must not collide):
- ADR: `docs/adrs/ADR-<issue#>-<slug>.md` where `<issue#>` is the GitHub issue you were
  dispatched for. Never a running counter, never `docs/adr/` (singular).
- Spec: `docs/specs/<topic>.md`. Research evidence: `docs/research/issue-<issue#>-<slug>.md`.
- An ADR records ONE decision with the alternatives rejected and why.

HOW YOU FINISH:
- Commit, `git push -u origin HEAD`, `gh pr create --base dev --title "... (#<issue#>)"
  --body "... Refs #<issue#>"`. Then stop. The dispatcher routes review and closes the issue
  on merge. If the Reviewer sends the PR back (`state:blocked`), you will be told in this
  session: fix, push, `gh pr edit <pr> --remove-label state:blocked --remove-label state:tested`.
- Need the human? Comment exactly what you need, add `needs-human`, wait here.

FORBIDDEN:
- You do not implement features.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

### D. `~/.orca/roles/researcher.md`

```markdown
You are the Researcher Agent.

RESPONSIBILITIES:
1. Answer questions of fact so that no other agent has to guess.
2. Establish what an external source, endpoint or page ACTUALLY returns. Use `just-scrape`
   when it is configured; otherwise fetch directly (curl / WebFetch / package inspection).
   Never let a missing tool turn into a guess.
3. Reproduce and localise failures with `diagnosing-bugs` before anyone proposes a fix.
4. Write findings where the agent that needs them will find them, and state how you know.

EVIDENCE RULES:
- MEASURE, DO NOT INFER. "The documentation says" is not an observation.
- Quote the request you sent and the response you got. A finding without its evidence is a
  rumour with a citation.
- Label a hypothesis as a hypothesis. Report an honest UNKNOWN rather than a confident guess.

FILES AND NAMES:
- Evidence: `docs/research/issue-<issue#>-<slug>.md`.
- If the issue asks for a decision, record it as `docs/adrs/ADR-<issue#>-<slug>.md` with the
  rejected alternatives. `<issue#>` is the GitHub issue you were dispatched for.

HOW YOU FINISH:
- Commit, `git push -u origin HEAD`, `gh pr create --base dev --title "... (#<issue#>)"
  --body "... Refs #<issue#>"`. Then stop. If sent back (`state:blocked`) you will be told
  here: fix, push, `gh pr edit <pr> --remove-label state:blocked --remove-label state:tested`.
- Need the human (a key, a decision)? Comment exactly what you need, add `needs-human`, wait.

FORBIDDEN:
- You do not change production code.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

### E. `~/.orca/roles/developer.md`

```markdown
You are the Developer Agent.

RESPONSIBILITIES:
1. Implement ONE issue inside the Orca worktree assigned to you.
2. Read `docs/CORE_DOCUMENT.md`, the relevant spec and the ADRs before writing anything.
3. Publish a pull request into `dev` when the issue is implemented.

CODE STANDARD:
- Easily readable, minimalist code. No unnecessary abstraction, no premature generalisation,
  no design pattern the problem did not ask for. The Reviewer will reject all three.
- Match the surrounding code: its naming, its idioms, its comment density.
- Comment WHY, never what. Complete docstrings on every public function, class and module.
- The smallest change that satisfies the issue. Anything more is scope you were not given.
- Tests for new behaviour ship in the same PR.

SKILL RULES:
- The dispatch brief lists the skills your issue's labels earned (`ui`, `seo`, `scraper`,
  `bug`, `data`). Use them.
- If the work needs a skill you were not given: STOP and say so on the issue (`needs-human`).
  Do not improvise around a missing skill -- the label is wrong and a human must fix it.

REPORTING RULES:
- The PR body states what you changed, why, and WHAT YOU MEASURED. For anything touching
  data or a pipeline that means a number, not an adjective (see `evidence_gates` in
  `.orca/dispatch.yml`).
- A defect you found but did not fix is written down in the PR. A finding that the issue
  contradicts the core document or a spec is reported, not routed around.

HOW YOU FINISH:
- Commit in small clear commits, `git push -u origin HEAD`, then
  `gh pr create --base dev --title "<what> (#<issue#>)" --body "<summary, evidence>. Refs #<issue#>"`.
  Then stop. The Tester and Reviewer are dispatched automatically.
- If the PR comes back (`state:blocked`) you will be told in this session with the
  comments: address every point, push, then
  `gh pr edit <pr> --remove-label state:blocked --remove-label state:tested`. If a point is
  wrong or impossible, say so on the PR and add `needs-human`.

FORBIDDEN:
- Never merge your own work. The Reviewer merges into `dev`.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

### F. `~/.orca/roles/tester.md`

```markdown
You are the Testing Agent.

RESPONSIBILITIES:
1. Validate the pull request you were dispatched to, inside its worktree.
2. Run everything that exists: test suite, linters, type checks, builds. Add tests where
   new behaviour has none, commit them on the PR branch and push.

HYBRID TESTING STRATEGY:
- STANDARD CODE (pure functions, utilities, parsing): write automated tests directly from
  the spec. Use `tdd` and `webapp-testing`.
- LARGE COMPONENTS (data fetching, storage, core frontend):
  1. STOP before writing test suites.
  2. Generate an FMEA table: | Proposed Component | Failure Mode | Default Recovery Action |
     Human Confirmation Needed? |
  3. INTERVIEW THE HUMAN: post the table as a PR comment, add the label `needs-human`
     (`gh pr edit <pr> --add-label needs-human`) and WAIT in this session. The human
     answers here. Remove the label when confirmed.
  4. Write the full suite from the confirmed table; commit the table beside the tests.
- A fake that behaves better than the real thing proves nothing. A test can pin a mistake:
  if the change is right and the old test is wrong, change the test deliberately, in its
  own commit, with the reason.

HOW YOU FINISH (the dispatcher reads exactly this):
- PASS:  `gh pr edit <pr> --add-label state:tested` and a comment: what ran, counts, what
  you added.
- FAIL:  `gh pr edit <pr> --add-label state:blocked` and a comment: exactly what failed and
  what the Developer must change.
- Always exactly one of the two. A session that ends without a label stalls the pipeline.
- Docs-only PRs never reach you (the dispatcher marks them tested). If one does, mark it
  `state:tested` with the comment "docs-only".

FORBIDDEN:
- You do not merge. You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

### G. `~/.orca/roles/reviewer.md`

```markdown
You are the Reviewer Agent.

RESPONSIBILITIES:
1. Review pull requests into `dev` that the Tester has passed (`state:tested`).
2. Ensure the change strictly fulfils the specification in `docs/specs/` and does not
   contradict `docs/CORE_DOCUMENT.md`.
3. Enforce the code standard: easily readable, minimalist code. Reject unnecessary
   abstraction, premature generalisation and design patterns the problem did not ask for.
4. Verify the tests pass and the evidence gates in `.orca/dispatch.yml` are met for PRs
   labelled `data`, `ui` or `pipeline`: a number reconciled against an independent source,
   real output sampled, cost measured where a budget exists. Green tests alone are not
   evidence.
5. For docs-only PRs (ADRs, specs, research): file names follow
   `docs/adrs/ADR-<issue#>-<slug>.md` / `docs/specs/<topic>.md` /
   `docs/research/issue-<issue#>-<slug>.md`; links resolve; numbering does not collide with
   ADRs already on `dev`; one decision per ADR with rejected alternatives.
6. The `trivial` label is a request, not a permission: if the change touches behaviour,
   send it back for tests.

HOW YOU FINISH (the dispatcher reads exactly this):
- APPROVE:  `gh pr merge <pr> --squash --delete-branch` (this merges into `dev`), then a
  one-paragraph review comment. If the merge fails on conflicts with `dev`, treat it as
  CHANGES and ask the developer to rebase.
- CHANGES:  `gh pr edit <pr> --add-label state:blocked --remove-label state:tested` and a
  numbered comment of required changes. The Developer gets it in their session.
- You never close issues; the dispatcher does that on merge.

MERGE CONTROL:
- You are PERMITTED to merge approved pull requests into `dev`, and only `dev`.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

---

## 8. Step 5 -- the dispatcher

### `.orca/dispatch.yml`

Substitute `github_mention` for the owner's handle.

```yaml
version: 2

branches:
  base: dev
  protected: [main]          # no agent may merge into these; the human opens dev -> main

# The dispatcher process (.orca/dispatcher/dispatch.py) reads this block.
dispatcher:
  agent: claude                      # Orca agent id used for every spawn
  poll_interval_seconds: 60
  max_active_issues: 4               # issues with a live worktree at once; protects quota and Orca
  idle_minutes_before_nudge: 15      # agent silent this long without a PR/label -> one nudge
  idle_minutes_after_nudge: 15       # still silent -> label needs-human
  cleanup_worktrees_on_merge: true   # remove the developer worktree when its PR merges into dev
  docs_only:                         # a PR touching only these is auto state:tested (tester skipped)
    prefixes: [docs/]
    suffixes: [.md, .txt]
  notify:
    github_mention: "@IdseVal"       # mentioned on GitHub when an agent needs the human; GitHub e-mails mentions
    # SMTP_* environment variables (same names as the daily digest) add an e-mail on top, if set.

gates:
  core_document:                     # nothing is dispatched until the PO's core document is on `dev`
    path: docs/CORE_DOCUMENT.md
    empty_marker: "Status: EMPTY"

# Role prompts are HOST-LEVEL at ~/.orca/roles/<role>.md and shared across every project.
# This block only lists the baseline skills each role is told it has.
roles:
  po-analyst:
    skills: [handoff, grill-with-docs, domain-modeling]
  architect:
    skills: [handoff, improve-codebase-architecture, codebase-design]
  researcher:
    skills: [handoff, just-scrape, diagnosing-bugs]
  developer:
    skills: [handoff]                # baseline; labels add to it
  tester:
    skills: [handoff, tdd, webapp-testing]
  reviewer:
    skills: [handoff, code-review]

# A label is a claim about the work. It earns the skills that work needs and no others.
labels:
  ui:
    skills: [frontend-design, web-design-guidelines, high-end-visual-design]
  seo:
    skills: [seo-audit]
  scraper:
    skills: [just-scrape]
  bug:
    skills: [diagnosing-bugs]
  data:
    skills: [codebase-design]

pipelines:
  default:
    stages: [developer, tester, reviewer]
  # `trivial` skips the TESTER, never the reviewer. The Reviewer refuses the label when the
  # change turns out to touch behaviour: it is a request, not a permission.
  trivial:
    stages: [developer, reviewer]
    forbid_labels: [ui, scraper, data]

circuit_breaker:
  max_cycles: 3                      # dispatches + send-backs per issue before `escalated`

evidence_gates:
  - id: counts-reconciled
    applies_to: [data, pipeline]
    requires: >-
      A count from the run reconciled against a count from an independent source, both quoted
      in the pull request. "It completed" is not a count.
  - id: output-sampled
    applies_to: [data, ui]
    requires: >-
      A sample of the actual output pasted into the pull request. Enough rows that a wrong
      one would be visible.
  - id: cost-measured
    applies_to: [pipeline]
    requires: >-
      The measured cost of the change against its budget, where one is documented.
```

### Labels

Do not create labels by hand; `python .orca/dispatcher/dispatch.py doctor --fix` creates
every label the workflow uses, with descriptions and colors, and is idempotent.

### How dispatch actually reaches an agent (why briefs are files)

The dispatcher never passes real content on a command line. Long `--prompt` strings get
truncated by the CLI path, and multi-line `terminal send` text submits early in the
agent's TUI -- both actually happened. Instead every dispatch writes a **brief file** to
`.orca/dispatcher/prompts/` and hands the agent one line:

```
Read the file C:/.../.orca/dispatcher/prompts/issue-12-cycle1.md and follow its instructions exactly.
```

The brief tells the agent to load its own context (role file, `gh issue view`, core
document, specs) and exactly how to finish (open a PR / set a `state:*` label). The brief
files stay on disk, so a human triaging a confused agent can read precisely what it was
told.

### `.orca/dispatcher/dispatch.py` -- full source

The behavioural contract is the module docstring. Subcommands: `run`, `once`, `status`,
`doctor [--fix]`, `onboard`; flags `--dry-run`, `--interval`, `--verbose`.

```python
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
    # Under pythonw there is no console, so every console child (gh/git/orca) would
    # otherwise get a brand-new visible window that steals focus from the human.
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
```

### `.orca/dispatcher/install-task.ps1` -- run it forever

```powershell
<#
.SYNOPSIS
  Install (or remove) the dispatcher as a Windows scheduled task for the current user.

.DESCRIPTION
  Registers "OrcaDispatcher-<repo folder>" to start `pythonw dispatch.py run` at logon,
  restart it if it dies, and never time it out. The dispatcher is then independent of any
  terminal or chat session. Logs go to .orca\dispatcher\dispatcher.log.

  Run from anywhere:   powershell -ExecutionPolicy Bypass -File .orca\dispatcher\install-task.ps1
  Remove:              ... install-task.ps1 -Uninstall
  Show:                ... install-task.ps1 -Status
#>
[CmdletBinding()]
param(
  [switch]$Uninstall,
  [switch]$Status
)

$ErrorActionPreference = 'Stop'
$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $here '..\..')).Path
$repoName = Split-Path -Leaf $repoRoot
$taskName = "OrcaDispatcher-$repoName"
$script   = Join-Path $here 'dispatch.py'

if ($Status) {
  $t = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
  if (-not $t) { Write-Host "not installed: $taskName"; exit 0 }
  $i = Get-ScheduledTaskInfo -TaskName $taskName
  Write-Host ("{0}: state={1} lastRun={2} lastResult={3}" -f $taskName, $t.State, $i.LastRunTime, $i.LastTaskResult)
  exit 0
}

if ($Uninstall) {
  $t = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
  if ($t) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    Write-Host "removed $taskName"
  } else { Write-Host "not installed: $taskName" }
  exit 0
}

# pythonw: no console window every minute, no stdout; the script logs to a file.
$pythonw = $null
$cmd = Get-Command pythonw.exe -ErrorAction SilentlyContinue
if ($cmd) { $pythonw = $cmd.Source }
if (-not $pythonw) {
  $py = (Get-Command python.exe -ErrorAction SilentlyContinue)
  if ($py) { $candidate = Join-Path (Split-Path -Parent $py.Source) 'pythonw.exe'; if (Test-Path $candidate) { $pythonw = $candidate } }
}
if (-not $pythonw) { throw "pythonw.exe not found on PATH. Install Python 3.9+ for this user." }

$action    = New-ScheduledTaskAction -Execute $pythonw -Argument "`"$script`" run" -WorkingDirectory $repoRoot
$trigger   = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings  = New-ScheduledTaskSettingsSet -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
               -ExecutionTimeLimit ([TimeSpan]::Zero) -StartWhenAvailable -MultipleInstances IgnoreNew `
               -DontStopIfGoingOnBatteries -AllowStartIfOnBatteries
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited

$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing) { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false }
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal | Out-Null
Start-ScheduledTask -TaskName $taskName
Start-Sleep -Seconds 3
$info = Get-ScheduledTaskInfo -TaskName $taskName
Write-Host "installed and started $taskName (python: $pythonw)"
Write-Host "state: $((Get-ScheduledTask -TaskName $taskName).State); log: $here\dispatcher.log"
Write-Host "check:  python .orca\dispatcher\dispatch.py status"
```

### `.orca/dispatcher/README.md`

````markdown
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
python .orca\dispatcher\dispatch.py onboard        # start the PO & Analyst interview worktree
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
human and wait.

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

## Re-running an issue by hand

Remove `escalated`/`needs-human` as appropriate; if its worktree is gone the dispatcher
re-dispatches on the next tick (cycle continues counting). To reset the cycle count,
delete the issue's entry from `state.json`.
````

---

## 9. Step 6 -- bring-up, in order

Each step ends with a verification. Do not continue past a failing one.

**A -- repository.** `main` and `dev` exist on origin; the section-3 tree, core-document
template, `.gitignore`, `.gitattributes` committed to `dev` and pushed.
*Verify:* `git ls-remote --heads origin dev` shows the branch.

**B -- Orca knows the repo.**

```bash
orca open                                   # or start the desktop app
orca repo add --path .
orca repo set-base-ref --repo path:$PWD --ref dev
```

*Verify:* `orca repo show --repo path:$PWD` prints `worktreeBaseRef: dev`.

**C -- host installs.** Roles (section 7), skills (`bash .orca/setup_skills.sh`), labels
and everything else:

```bash
python .orca/dispatcher/dispatch.py doctor --fix
```

*Verify:* doctor ends with `0 problem(s)` — except the core-document gate, which stays
FAIL until step E merges. Everything else must be OK.

**D -- the dispatcher runs forever.**

```powershell
powershell -ExecutionPolicy Bypass -File .orca\dispatcher\install-task.ps1
```

*Verify:* `install-task.ps1 -Status` shows `state=Running`, and
`.orca/dispatcher/dispatcher.log` gains a line each minute. (Non-Windows: run
`python .orca/dispatcher/dispatch.py run` under systemd/launchd instead; the script is
platform-neutral, only the installer is Windows-specific.)

**E -- onboarding.**

```bash
python .orca/dispatcher/dispatch.py onboard
```

This creates the `onboarding` worktree with the PO & Analyst and brings it to the front.
The human is interviewed in rounds; on agreement the PO opens a PR into `dev`; the PR is
docs-only so the dispatcher marks it tested, spawns the Reviewer, and the Reviewer merges.
*Verify:* the gate line of `dispatch.py status` flips to `OPEN`.

**F -- issues flow.** The PO derives specs and ADRs, then writes issues: one `role:*`
label each, skill labels, `trivial` where apt, `ready`, and `Depends on: #a, #b` lines for
ordering (research first; architecture depends on research; implementation depends on
architecture). From here the loop is autonomous: dispatch -> PR -> tester (or docs-only
skip) -> reviewer -> merge into `dev` -> issue closed -> dependencies release the next
phase. *Verify:* `dispatch.py status` shows issues moving without anyone touching GitHub.

**G -- promotion.** The human inspects `dev` and opens the pull request into `main`
themselves. No agent ever does.

---

## 10. The circuit breaker

`circuit_breaker.max_cycles: 3`. A cycle is spent each time an issue is dispatched and
each time its PR comes back `state:blocked`. On the cycle after the third, the dispatcher
does not re-dispatch: it labels the issue `escalated`, posts
`Circuit breaker tripped: 3 failed attempts.`, and pages the human.

Do not raise the limit to force something through. An issue that has failed three times is
saying the specification is wrong, and a fourth attempt at the wrong thing costs more than
asking.

---

## 11. The FMEA protocol

For **large components** -- data fetching, storage, core frontend -- the Tester stops
before writing anything and produces a failure matrix:

| Proposed Component | Failure Mode | Default Recovery Action | Human Confirmation Needed? |
| --- | --- | --- | --- |

It posts the table as a PR comment, adds **`needs-human`**, and waits in its session. The
dispatcher pages the human (GitHub @mention -> e-mail, Orca tab brought forward). The human
answers *in the Tester's session*, the Tester removes the label and writes the suite from
the confirmed table, committing the table beside the tests.

Two standing warnings for the Tester: **a fake that behaves better than the real thing
proves nothing**, and **a test can pin a mistake** -- if the change is right and the old
test is wrong, the test changes, deliberately, in its own commit, with the reason stated.

---

## 12. Evidence before merge

A circuit breaker catches thrashing. It does not catch the run that **reports success
while being wrong**. Green tests prove the code does what the tests assumed -- a different
claim from "the code is right". So for PRs labelled `data`, `ui` or `pipeline`, the
Reviewer requires (see `evidence_gates` in `dispatch.yml`): counts reconciled against an
independent source, real output sampled into the PR, cost measured against a documented
budget. The principle: **an artefact describes itself by observation, not by declaration.**

---

## 13. The daily digest

Derived from commit and pull-request history, never from agent-written logs: commits and
merged PRs exist anyway and cannot be forgotten or double-written. Quiet day -> no mail.
No SMTP secrets -> renders into the workflow log instead of sending (always safe).

### `mail/daily_digest.py` -- full source

```python
"""Daily agent digest.

Derived from commit and pull-request history rather than from agent-written logs. Agents
forget, parallel worktrees conflict on a shared file, and a report assembled from
self-description tells you what each agent believed — the very thing being checked.

Contract (see agent-workflow-setup.md section 13):

* Sources: ``git log <branch> --since=<day> --until=<day+1> --no-merges`` and
  ``gh pr list --base <branch> --state merged --json ...``.
* Grouping: merged pull requests first, then commits grouped by author.
* Quiet day: prints ``nothing landed`` and sends no mail.
* No credentials: prints the rendered HTML instead of sending.
* Any missing tool, branch or auth degrades to "nothing from this source"; the script
  never exits non-zero.
* Every string coming from a commit message, title or label is HTML-escaped.
* Field split uses ``\\x1f`` / ``\\x1e`` control characters; a commit subject can
  contain any printable delimiter you might otherwise pick.
* Mail is sent as ``MIMEText(body, "html", "utf-8")``; the default is us-ascii and
  mangles punctuation.
"""

from __future__ import annotations

import argparse
import html
import json
import os
import smtplib
import subprocess
import sys
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from email.mime.text import MIMEText

# Control characters, not printable ones. A commit subject can legitimately contain any
# character a human types, so a printable delimiter is a bug waiting for a colourful
# subject line.
UNIT_SEP = "\x1f"
RECORD_SEP = "\x1e"

# git and gh are cheap; a 60s ceiling keeps the workflow from hanging on a broken remote.
SUBPROCESS_TIMEOUT_SECONDS = 60


@dataclass(frozen=True)
class Commit:
    """One commit, as reported by ``git log``."""

    sha: str
    author: str
    subject: str


@dataclass(frozen=True)
class PullRequest:
    """One merged pull request, as reported by ``gh pr list``."""

    number: int
    title: str
    author: str
    labels: tuple[str, ...]
    url: str
    merged_at: str


def parse_args() -> argparse.Namespace:
    """Parse the CLI. All arguments are optional; defaults match the workflow."""

    parser = argparse.ArgumentParser(
        description="Daily agent digest, derived from git and merged pull requests.",
    )
    parser.add_argument(
        "--branch",
        default="dev",
        help="Branch to summarise. Defaults to dev.",
    )
    parser.add_argument(
        "--day",
        default="",
        help="Day to report, YYYY-MM-DD (UTC). Empty means today.",
    )
    parser.add_argument(
        "--print",
        dest="print_only",
        action="store_true",
        help="Render the HTML to stdout and send no mail.",
    )
    return parser.parse_args()


def resolve_day(day_arg: str) -> date:
    """Return the day to report on. Bad input falls back to today rather than raising —
    the workflow must never fail because someone typed a date wrong.
    """

    if not day_arg:
        return datetime.now(timezone.utc).date()
    try:
        return datetime.strptime(day_arg, "%Y-%m-%d").date()
    except ValueError:
        print(
            f"!! Bad --day {day_arg!r}; using today (UTC).",
            file=sys.stderr,
        )
        return datetime.now(timezone.utc).date()


def _run(argv: list[str]) -> tuple[bool, str]:
    """Run a command and return ``(ok, stdout)``. Never raises.

    A missing tool, an auth failure or a non-zero exit degrades to ``(False, "")`` so the
    caller can treat the source as empty for the day.
    """

    try:
        result = subprocess.run(
            argv,
            check=False,
            capture_output=True,
            text=True,
            timeout=SUBPROCESS_TIMEOUT_SECONDS,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as exc:
        print(f"!! {argv[0]} unavailable: {exc}", file=sys.stderr)
        return False, ""
    if result.returncode != 0:
        stderr = (result.stderr or "").strip()
        if stderr:
            print(
                f"!! {' '.join(argv[:3])} failed: {stderr}",
                file=sys.stderr,
            )
        return False, ""
    return True, result.stdout


def read_commits(branch: str, day: date) -> list[Commit]:
    """Return commits on ``branch`` for the UTC day given.

    ``--since`` and ``--until`` bracket a 24-hour window; ``--no-merges`` drops merge
    commits so a pull request does not double-count with its own contents.
    """

    since = day.isoformat()
    until = (day + timedelta(days=1)).isoformat()
    pretty = f"%H{UNIT_SEP}%an{UNIT_SEP}%s{RECORD_SEP}"
    ok, out = _run(
        [
            "git",
            "log",
            branch,
            f"--since={since}",
            f"--until={until}",
            "--no-merges",
            f"--pretty=format:{pretty}",
        ]
    )
    if not ok or not out.strip():
        return []

    commits: list[Commit] = []
    for record in out.split(RECORD_SEP):
        record = record.strip("\n")
        if not record:
            continue
        parts = record.split(UNIT_SEP)
        if len(parts) < 3:
            continue
        # The subject can itself contain UNIT_SEP in the pathological case; rejoin the tail
        # rather than dropping content.
        sha, author = parts[0], parts[1]
        subject = UNIT_SEP.join(parts[2:])
        commits.append(Commit(sha=sha, author=author, subject=subject))
    return commits


def _parse_merged_at(raw: str) -> datetime | None:
    """Parse gh's ISO-8601 ``mergedAt`` field. Returns None if unparseable."""

    if not raw:
        return None
    try:
        return datetime.fromisoformat(raw.replace("Z", "+00:00"))
    except ValueError:
        return None


def read_prs(branch: str, day: date) -> list[PullRequest]:
    """Return pull requests merged into ``branch`` during the UTC day given.

    gh has no ``--merged-on`` filter, so we list merged PRs and window-filter locally.
    """

    ok, out = _run(
        [
            "gh",
            "pr",
            "list",
            "--base",
            branch,
            "--state",
            "merged",
            "--limit",
            "200",
            "--json",
            "number,title,author,labels,mergedAt,url",
        ]
    )
    if not ok or not out.strip():
        return []

    try:
        raw_entries = json.loads(out)
    except json.JSONDecodeError as exc:
        print(f"!! gh returned unparseable JSON: {exc}", file=sys.stderr)
        return []

    start = datetime.combine(day, datetime.min.time(), tzinfo=timezone.utc)
    end = start + timedelta(days=1)

    prs: list[PullRequest] = []
    for entry in raw_entries:
        merged = _parse_merged_at(entry.get("mergedAt") or "")
        if merged is None or not (start <= merged < end):
            continue
        author_field = entry.get("author") or {}
        author = author_field.get("login") or "unknown"
        labels = tuple(
            item.get("name", "")
            for item in (entry.get("labels") or [])
            if item.get("name")
        )
        prs.append(
            PullRequest(
                number=int(entry.get("number", 0)),
                title=entry.get("title", ""),
                author=author,
                labels=labels,
                url=entry.get("url", ""),
                merged_at=entry.get("mergedAt") or "",
            )
        )
    prs.sort(key=lambda pull: pull.merged_at)
    return prs


def render_html(
    branch: str,
    day: date,
    prs: list[PullRequest],
    commits: list[Commit],
) -> str:
    """Render the digest as HTML. Every value from git or gh is escaped."""

    escape = html.escape
    parts: list[str] = [
        f"<h1>Daily digest &mdash; {escape(branch)} &mdash; {day.isoformat()}</h1>",
    ]

    parts.append("<h2>Merged pull requests</h2>")
    if not prs:
        parts.append("<p><em>none</em></p>")
    else:
        parts.append("<ul>")
        for pull in prs:
            label_html = ""
            if pull.labels:
                label_html = " " + " ".join(
                    f"<code>{escape(name)}</code>" for name in pull.labels
                )
            title_html = escape(pull.title)
            link_open = f'<a href="{escape(pull.url)}">' if pull.url else ""
            link_close = "</a>" if pull.url else ""
            parts.append(
                f"<li>#{pull.number} {link_open}{title_html}{link_close}"
                f" &mdash; {escape(pull.author)}{label_html}</li>"
            )
        parts.append("</ul>")

    parts.append("<h2>Commits</h2>")
    if not commits:
        parts.append("<p><em>none</em></p>")
    else:
        grouped: dict[str, list[Commit]] = {}
        for commit in commits:
            grouped.setdefault(commit.author, []).append(commit)
        for author in sorted(grouped):
            parts.append(f"<h3>{escape(author)}</h3><ul>")
            for commit in grouped[author]:
                parts.append(
                    f"<li><code>{escape(commit.sha[:7])}</code> "
                    f"{escape(commit.subject)}</li>"
                )
            parts.append("</ul>")

    return "\n".join(parts)


def _smtp_config() -> dict[str, str] | None:
    """Return the SMTP config if every variable is set, else None."""

    keys = (
        "SMTP_SENDER_EMAIL",
        "SMTP_RECEIVER_EMAIL",
        "SMTP_PASSWORD",
        "SMTP_SERVER",
        "SMTP_PORT",
    )
    values = {key: os.environ.get(key, "").strip() for key in keys}
    if not all(values.values()):
        return None
    return values


def send_or_print(html_body: str, day: date, print_only: bool) -> None:
    """Print the body when asked, when credentials are missing, or when SMTP fails.

    The script always leaves the reader with the digest somewhere they can see it; a mail
    outage must not silently swallow the day.
    """

    if print_only:
        print(html_body)
        return

    config = _smtp_config()
    if config is None:
        # Missing credentials are the deliberate first-run state; do not warn.
        print(html_body)
        return

    try:
        port = int(config["SMTP_PORT"])
    except ValueError:
        print(
            f"!! Bad SMTP_PORT {config['SMTP_PORT']!r}; printing instead.",
            file=sys.stderr,
        )
        print(html_body)
        return

    message = MIMEText(html_body, "html", "utf-8")
    message["Subject"] = f"Daily agent digest - {day.isoformat()}"
    message["From"] = config["SMTP_SENDER_EMAIL"]
    message["To"] = config["SMTP_RECEIVER_EMAIL"]

    try:
        with smtplib.SMTP_SSL(config["SMTP_SERVER"], port, timeout=30) as smtp:
            smtp.login(config["SMTP_SENDER_EMAIL"], config["SMTP_PASSWORD"])
            smtp.sendmail(
                config["SMTP_SENDER_EMAIL"],
                [config["SMTP_RECEIVER_EMAIL"]],
                message.as_string(),
            )
    except (smtplib.SMTPException, OSError) as exc:
        print(f"!! SMTP send failed: {exc}", file=sys.stderr)
        print(html_body)


def main() -> int:
    """Entry point. Always returns 0."""

    args = parse_args()
    day = resolve_day(args.day)
    prs = read_prs(args.branch, day)
    commits = read_commits(args.branch, day)

    if not prs and not commits:
        print(f"nothing landed on {args.branch} on {day.isoformat()}")
        return 0

    body = render_html(args.branch, day, prs, commits)
    send_or_print(body, day, args.print_only)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        # Ctrl+C on a scheduled job should not page anyone.
        sys.exit(0)
```

### `.github/workflows/daily-digest.yml`

```yaml
name: Daily agent digest

on:
  schedule:
    - cron: '10 18 * * *'      # off the hour: GitHub queues the hour heavily
  workflow_dispatch:
    inputs:
      day:
        description: 'Day to report, YYYY-MM-DD. Empty means today (UTC).'
        type: string
        default: ''
      print_only:
        description: 'Render into the log and send no mail'
        type: boolean
        default: true

concurrency:
  group: daily-digest
  cancel-in-progress: false

permissions:
  contents: read
  pull-requests: read

jobs:
  digest:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0        # a shallow clone reports an empty day, every day
          ref: dev
      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'
      - name: Render and send
        env:
          GH_TOKEN: ${{ github.token }}
          SMTP_SENDER_EMAIL: ${{ secrets.SMTP_SENDER_EMAIL }}
          SMTP_RECEIVER_EMAIL: ${{ secrets.SMTP_RECEIVER_EMAIL }}
          SMTP_PASSWORD: ${{ secrets.SMTP_PASSWORD }}
          SMTP_SERVER: ${{ secrets.SMTP_SERVER }}
          SMTP_PORT: ${{ secrets.SMTP_PORT }}
        run: |
          args=(--branch dev)
          if [ -n "${{ inputs.day }}" ]; then args+=(--day "${{ inputs.day }}"); fi
          if [ "${{ inputs.print_only }}" = "true" ]; then args+=(--print); fi
          python mail/daily_digest.py "${args[@]}"
```

Leave the SMTP secrets unset at first: the scheduled run renders into the workflow log,
which is how you find out whether the content is worth reading before it starts arriving.

---

## 14. Branch protection

The human gate on `main` is a rule in every role prompt **and** a rule on the repository.
Set both -- a prompt is an instruction, a branch rule is an enforcement.

GitHub -> Settings -> Rules -> Rulesets -> New branch ruleset: name `main protection`,
enforcement Active, target `main`; enable *Restrict deletions*, *Block force pushes*,
*Require a pull request before merging* (1 approval). Or via the classic API:

```bash
gh api -X PUT repos/:owner/:repo/branches/main/protection \
  -f "required_pull_request_reviews[required_approving_review_count]=1" \
  -F "enforce_admins=false" -F "restrictions=null" -F "required_status_checks=null"
```

`dev` stays unprotected: the Reviewer must be able to merge there, and the dispatcher
must be able to see it. Note one consequence the dispatcher already handles: **merging a
PR into `dev` does not auto-close issues** ("Closes #N" only fires on the default
branch), so the dispatcher closes issues itself when their PR merges.

---

## 15. Known pitfalls -- all of these actually happened

| Trap | What is actually true |
| --- | --- |
| Orca via npm / in the container | Not on npm. Host desktop app; cannot be containerised. |
| `skills install ... --target` | The verb is `add`, the flag `--skill`, scope `--global`. Agent id is `claude-code`; plain `claude` is rejected. |
| Skills installed in the container | Agents run on the HOST via Orca. Install skills globally on the host or no agent has any. |
| Role prompts copied per project | Roles are host-level at `~/.orca/roles/`, shared by all projects. Per-project copies drift. |
| `dispatcher.md` treated as an agent | It is policy. The runtime is `dispatch.py` as a scheduled task; without it running, nothing is routed and the pipeline stalls silently. |
| "A webhook notifies the dispatcher" | Nowhere to land on a desktop host. The dispatcher polls `gh`. |
| Long `--prompt` strings | Truncated in the CLI path; the agent gets half its instructions. Briefs are files; the prompt is one line pointing at the file. |
| Multi-line `terminal send` | The agent TUI submits on newline; later lines arrive as separate messages. Same fix. |
| Trusting agents to set labels | A tester with nothing to test ends silently and the pipeline waits forever. The dispatcher watches session liveness: idle -> nudge -> `needs-human`. Docs-only PRs are auto-`state:tested`. |
| "Closes #N" into `dev` | Only auto-closes on the default branch. The dispatcher closes issues on merge itself; without that, `Depends on:` phases never release. |
| Parallel agents numbering ADRs | Three agents all wrote `ADR-002-*`. ADRs are numbered by ISSUE: `docs/adrs/ADR-<issue#>-<slug>.md`. |
| Orca renames branches | `--name feature/issue-4` becomes branch `<gituser>/feature-issue-4`. Match branches by regex `issue-(\d+)`, never by exact name. |
| Base image `1-22` clears the Node floor | Ships 22.16; the CLI wants >= 22.20. Pin upward with `n`; do not chain npm after `n` in one RUN. |
| CRLF in shell scripts from Windows | The container bind-mounts the working tree; CRLF reaches bash and fails as `set: pipefail: invalid option name`. `.gitattributes` pins `*.sh` to LF. |
| `npm ci` over a bind mount | Replaces the host's `node_modules` with Linux binaries. Shadow it with a volume. |
| Dispatcher inside a chat session | Dies with the session. It is a scheduled task. |
| Two dispatchers at once | `state.json` is unlocked between processes; the lock file refuses the second. |
| `just-scrape` without an API key | Hangs on an interactive prompt. The researcher role says: use it when configured, otherwise fetch directly. |

---

## 16. Checklist

```
[ ] main + dev exist on origin; ruleset on main: PR required, no force pushes
[ ] section-3 tree committed to dev: dispatch.yml, dispatcher/, docs/, mail/, workflows
[ ] docs/CORE_DOCUMENT.md template on dev (Status: EMPTY until the interview)
[ ] ~/.orca/roles/ holds all seven role files (once per machine, never per project)
[ ] ~/.claude/skills/ holds all fourteen skills (bash .orca/setup_skills.sh)
[ ] python .orca/dispatcher/dispatch.py doctor --fix -> 0 problems (gate FAIL allowed pre-onboarding)
[ ] install-task.ps1 -> scheduled task Running; dispatcher.log ticking
[ ] dispatch.py onboard -> PO interview -> PR merged -> status gate OPEN
[ ] PO issues carry: one role:*, ready, Depends on: lines, skill labels
[ ] dispatch.py status shows the loop moving with no human action
[ ] SMTP secrets set (or deliberately unset); daily digest renders a real day
[ ] Owner has read section 17
```

---

## 17. The owner's runbook

*Hand this section to the human. Everything above it is for whoever builds the setup.*

**What runs by itself.** A scheduled task (`OrcaDispatcher-<repo>`) polls GitHub every
minute and runs the whole loop: dispatching agents into Orca worktrees, testing, review,
merging into `dev`, closing issues, advancing phases. Your machine being on and Orca being
open is all it needs. Reboots are fine -- the task restarts at logon.

**Your four touchpoints:**

1. **The PO interview** (once per project, plus when direction changes). An Orca tab opens
   and interviews you in rounds. Answer, correct, and finally say it is agreed.
2. **FMEA interviews.** For big risky components the Tester posts a failure-mode table and
   pages you. Go to the Orca tab it names, add the failure cases only you know, confirm.
3. **`needs-human` pings.** Any agent that needs a decision, a credential or an
   interview labels its issue/PR `needs-human`. You get a GitHub @mention (e-mail via
   GitHub notifications; plus direct SMTP mail if configured) and the Orca tab comes
   forward. Answer in that tab; the agent removes the label and continues.
4. **Promotion.** When `dev` looks good, open the PR `dev -> main` yourself and merge it.
   No agent will ever do this; the ruleset enforces it.

**Reading the board.** `python .orca\dispatcher\dispatch.py status` -- every issue
(queued / worktree / in PR, what it waits on), every PR (awaiting tester / tested /
blocked), every live session and how long it has been idle.

**Steering the queue.** Everything is labels on GitHub -- you never talk to the
dispatcher. Pause an issue: remove `ready`. Rush one: it dispatches in issue order as slots
free (4 in flight at a time; `max_active_issues` in `.orca/dispatch.yml`). Add work: write
an issue the way the PO does (one `role:*`, `ready`, `Depends on:` if it must wait). An
`escalated` issue is the breaker telling you the specification is wrong three times over --
fix the issue text, remove `escalated`, delete its entry in
`.orca/dispatcher/state.json` to reset the count.

**One-time GitHub setup (if the installer has not done it):** the `main` ruleset
(section 14) and, optionally, Actions secrets `SMTP_SENDER_EMAIL`, `SMTP_RECEIVER_EMAIL`,
`SMTP_PASSWORD`, `SMTP_SERVER`, `SMTP_PORT` for the daily digest and dispatcher pages --
use port 465 (SSL) and an app password, not your login password. The same five variables
set in *your Windows user environment* let the dispatcher e-mail you directly on
`needs-human`.

**If something looks stuck:** `dispatch.py status` first. A PR "awaiting tester" or
"tested" with an idle session resolves itself (nudge at 15 min, `needs-human` at 30).
Task not running? `install-task.ps1 -Status`, then re-run the installer. Orca closed?
Open it; the dispatcher retries every tick and loses nothing. Worst case: delete
`.orca/dispatcher/state.json` -- the truth lives in GitHub and Orca; you get at most a
duplicate comment.
