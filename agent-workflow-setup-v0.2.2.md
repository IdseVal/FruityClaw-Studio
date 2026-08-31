# The ORCA ADE agent workflow: complete setup (v0.2.2)

How to build this environment from an empty repository. **Every file is listed in full**, so
the whole workflow is constructable from this document alone -- including the dispatcher
runtime, the role prompts, the GitHub Actions pipeline, and the final instructions for the
human owner.

*Supersedes v0.1.1. v0.2 is a structural rework built on three changes, each fixing a
failure the previous version actually had:*

*1. **Workers are headless and ephemeral.** v0.1.1 ran every agent as a persistent
interactive TUI session and kept the developer's session open through the whole PR
pipeline. With 8 issues in flight that meant ~20+ live sessions idling on CPU, and
anything that stalled parked its sessions forever. In v0.2 an agent run is a PROCESS,
not a tab: `claude -p <brief>` starts, does one job, exits. The dispatcher tracks PIDs
and wall-clock, not terminal liveness. Only conversations with the human (the Planner
interview) are interactive.*

*2. **Seven roles become five.** PO & Analyst and the Researcher merge into the
**Planner** -- scope and facts are one mind, and `research` issues simply load the
Planner headless. The **Architect** keeps its own role and prompt: deciding WHAT to build
and designing HOW are different modes of thinking, and the checkpoint between them is
worth its file. Developer + Tester become the **Implementer** (the tests ship with the
change -- TDD makes a separate standing tester redundant). The independent check survives
as the **Verifier** and **Reviewer**, which now run in CI (change 3): the value of a
reviewer was never the persona -- it was the fresh context, and CI gives fresh context
for free. All fourteen skills are kept; section 6 explains how they travel with the
repository.*

*3. **The mechanical stages run in GitHub Actions.** Testing and review were the least
interactive stages and the biggest session hoarders. They are now one Actions workflow
that fires when a pull request opens or updates: an ephemeral runner spins up, the
Verifier and Reviewer agents run headless, the runner dies. Orphaning those stages is now
structurally impossible -- the compute does not exist between runs. Your machine only
runs the dispatcher, the Implementer processes, and the interviews.*

*Also new: the **autonomy dial** (section 14). One line in `.orca/dispatch.yml` --
`autonomy: manual | propose | auto` -- controls whether you write the GitHub issues
yourself, the Planner proposes them for your one-click approval, or the loop closes
itself as in v0.1.1. The onboarding command asks you which one you want if the line is
missing. The v0.1.1 pause brake, circuit breaker, evidence gates, core-document gate and
briefs-as-files all carry forward unchanged in spirit.*

*New in v0.2.1, after design review with the owner: the **Architect returns as its own
role** -- v0.2.0 briefly routed `architecture` issues to the Implementer, which put
contract thinking inside a prompt about code style; the Researcher folds into the
**Planner** instead. Notifications are cut to **one e-mail per day**: an 18:00-Amsterdam
digest with a new "Waiting on you" section (section 11); the dispatcher's own e-mail
channel is deleted, @mentions ship switched off, and OWNER STEP 3 mutes GitHub's
per-event mail. The **dev container is standard** now, not an appendix (section 5). Issues
themselves now carry a **writing standard** (section 7): self-contained, plain-language
context -- what the problem is, why it exists, what done looks like -- because both the
approving owner and the executing agent read the text cold, and a dispatched run that
cannot understand its issue now stops and asks instead of building a guess.*

*v0.2.2: scope hygiene. Forward-looking notes about future hosting are removed.
Everything in this document applies to the version being installed; nothing here is a
preview, so the installing agent has nothing to skip or anticipate.*

Project-agnostic. Two things must be substituted wherever they appear; nothing else is
tied to a particular project:

| Where | Replace |
| --- | --- |
| `.orca/dispatch.yml` -> `notify.github_mention` (ships EMPTY = digest-only) | the owner's `"@handle"` ONLY if instant pings are wanted (section 11) |
| `PROJECT_DATA_HOST_DIR` (dev container mount, section 5) | the project's large-data path, or delete the mount |

**If you are a Claude instance performing this setup:** work top to bottom. Sections 4-11
create files; section 12 is the bring-up order with verification after each step; section
18 is what you hand the human at the end. Do not improvise around a failing check --
every check exists because the naive path silently breaks the workflow. Two conventions
for you specifically:

- Blocks marked **OWNER STEP** are written in plain, non-technical language for the human
  owner. When a step needs the human (creating a secret on the GitHub website, flipping a
  settings toggle), relay the OWNER STEP to them **verbatim** -- do not compress it into
  jargon. They may be reading it days later with no context.
- During onboarding (section 12, step H) you must ask the owner which **autonomy mode**
  they want, in the plain-language terms of section 14, unless `autonomy:` is already set
  in `.orca/dispatch.yml`. Do not silently pick one.

The version of this document you are reading was produced after running v0.1.1 on a real
project; the pitfalls in section 17 either actually happened or are the documented
failure modes of the components introduced in v0.2.

---

## Contents

1.  [What you are building](#1-what-you-are-building)
2.  [Prerequisites](#2-prerequisites)
3.  [Repository layout](#3-repository-layout)
4.  [Step 1 -- the core document](#4-step-1----the-core-document)
5.  [Step 2 -- the dev container](#5-step-2----the-dev-container)
6.  [Step 3 -- skills, committed to the repository](#6-step-3----skills-committed-to-the-repository)
7.  [Step 4 -- the three role prompts](#7-step-4----the-three-role-prompts)
8.  [Step 5 -- the GitHub Actions pipeline](#8-step-5----the-github-actions-pipeline)
9.  [Step 6 -- the dispatcher](#9-step-6----the-dispatcher)
10. [Step 7 -- branch protection and repository settings](#10-step-7----branch-protection-and-repository-settings)
11. [Step 8 -- notifications and the daily digest](#11-step-8----notifications-and-the-daily-digest)
12. [Step 9 -- bring-up, in order](#12-step-9----bring-up-in-order)
13. [The circuit breaker](#13-the-circuit-breaker)
14. [The autonomy dial](#14-the-autonomy-dial)
15. [The FMEA protocol, now asynchronous](#15-the-fmea-protocol-now-asynchronous)
16. [Evidence before merge](#16-evidence-before-merge)
17. [Known pitfalls](#17-known-pitfalls)
18. [The owner's runbook](#18-the-owners-runbook)

---

## 1. What you are building

```
                 +----------------------------------+
                 |           HUMAN OWNER            |
                 |  Planner interviews - FMEA       |
                 |  answers on PRs - promotes       |
                 |  proposed->ready - dev->main PR  |
                 +----------------+-----------------+
                                  |
                                  v
   +-----------------------------------------------------------+
   |          DISPATCHER  (deterministic, on your machine)     |
   |  a reconciler: observes GitHub + Orca every 60s,          |
   |  computes what should be true, does the difference.       |
   |  Spawns HEADLESS `claude -p` runs that exit when done.    |
   +------+--------------------+---------------------+---------+
          |                    |                     |
          v                    v                     v
   +-------------+     +---------------+     +---------------+
   |   PLANNER   |     |   ARCHITECT   |     |  IMPLEMENTER  |
   | interactive |     | headless run: |     | headless run: |
   | interviews; |     | contracts,    |     | one issue ->  |
   | headless    |     | schemas,      |     | code + tests  |
   | research &  |     | ADRs          |     | -> PR to dev  |
   | audits      |     | -> PR to dev  |     | (fix cycles   |
   +------+------+     +-------+-------+     | re-read PR)   |
          |                    |             +-------+-------+
          v                    v                     v
   ~~~~~~~~~~~~~~~~~~ GITHUB ACTIONS (ephemeral, in the cloud) ~~~~~~~~~~~~~~~~~~
   |  PR opened/updated -> classify (docs-only? trivial?)                       |
   |    -> VERIFIER agent: runs tests, probes the change, FMEA on risky work    |
   |         ends with state:tested or state:blocked                            |
   |    -> REVIEWER agent: spec compliance, code standard, evidence gates       |
   |         merges into dev, or state:blocked                                  |
   |  The runner is destroyed when the job ends. Nothing to orphan.             |
   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

**The one idea that makes it work** carries over from v0.1.1: the dispatcher never trusts
an agent to report back. Every tick it observes reality (GitHub issues, PRs, labels;
process liveness of the runs it spawned; Orca worktrees), computes what the rules say
should be true, and does the difference. Every action is idempotent -- crashes, restarts
and double runs are harmless.

**The one idea that is new:** *GitHub is the memory; a run is disposable.* v0.1.1 kept a
developer's session alive for the whole PR lifetime so that blocked feedback could be
"told to it in this session". That was sessions-as-memory, and it is why the fleet
hoarded CPU. In v0.2 a blocked PR simply gets a *fresh* headless run whose brief contains
the blocker's comments -- everything a fixer needs is already on GitHub. No run ever
waits for anything. A run that needs the human says so **on the issue or PR**, labels it
`needs-human`, and *ends*; when the human has answered in the comments and removed the
label, the dispatcher sends a fresh run that reads those comments. The human answers on
GitHub -- from a phone, hours later -- instead of hunting for a live tab.

**Signals, not memory.** The pipeline state lives in GitHub labels:

| Signal | Meaning |
| --- | --- |
| issue: `proposed` | filed by the Planner, awaiting your approval (see the autonomy dial) |
| issue: `ready` | may be dispatched once its `Depends on: #n` issues are closed |
| issue body: `Depends on: #a, #b` | phase ordering; held until those issues are closed |
| issue: `research` / `architecture` | issue TYPE: routes the headless run to the Planner (research) or the Architect; the deliverables are documents |
| issue: `ui` `seo` `scraper` `bug` `data` | skill routing: the brief points the Implementer at the matching skills |
| issue: `trivial` | Verifier skipped (the Reviewer never is) |
| PR: `state:tested` | Verifier passed (set automatically for docs-only PRs) |
| PR: `state:blocked` | back for a fix cycle; a fresh Implementer run is dispatched; breaker at 3 |
| either: `needs-human` | you are paged; nothing moves on that item until you answer in the comments and remove the label |
| issue: `escalated` | circuit breaker tripped; you resolve |
| core document: `Status: EMPTY` | gate closed; nothing dispatches until the Planner interview PR merges |
| core document: `Status: ACHIEVED` | project complete: dispatching stops, you are paged; a revision interview reopens |

**Where each part runs:**

```
YOUR MACHINE (host)                        GITHUB (cloud)
  Orca (desktop app)  - owns worktrees       Actions: Verifier + Reviewer agents
  dispatcher (scheduled task) - the loop     (ephemeral runners, per-PR)
  headless claude -p runs (short-lived)      Issues + labels = the state machine
  interactive Planner sessions (interviews)  Branch dev = integration, main = yours
  repo: .claude/skills, .orca/roles          Secrets: the agents' API credential
```

Orca is a desktop application on the host; the dispatcher POLLS `gh` (60s tick) because
webhooks have nowhere to land on a desktop machine. **Everything the workflow needs --
skills, role prompts, config, the Actions workflow -- now travels inside the repository**
(a deliberate reversal from v0.1.1; sections 6-7 explain why).

**The human's touchpoints, in total:** the Planner interview (first and revision rounds);
FMEA answers as PR comments; `needs-human` answers as issue/PR comments; promoting
`proposed` issues to `ready` (in `manual`/`propose` modes); reviewing the result when the
loop declares completion; opening the `dev -> main` pull request; and reading the one
daily e-mail (section 11) that carries all of it. Everything else runs itself.

### Isolation: decide which one you want

Unchanged from v0.1.1, and it matters MORE now: headless runs are started with
`--dangerously-skip-permissions` so they cannot hang on a permission prompt (section 17
explains why that flag is load-bearing). That means an Implementer run can execute any
command on your machine that you can.

| You want | Do this | Cost |
| --- | --- | --- |
| **Reproducibility** -- same toolchain everywhere | the dev container of section 5. No `docker.sock`. | none |
| **Containment** -- agents cannot reach the host | rootless Docker, sysbox, or a VM; or run the dispatcher + worktrees inside a VM | real setup work |

The GitHub Actions half needs no such decision: those agents run in GitHub's sandbox with
a scoped token, and the runner is destroyed after every job.

---

## 2. Prerequisites

All on the host. Verify each before starting; `dispatch.py doctor` (section 9) re-checks
most of them at any time.

| Tool | Why | Check |
| --- | --- | --- |
| Orca (desktop app) | owns worktrees; hosts the interactive Planner sessions | `orca status` -> `runtimeReachable: true` |
| Claude Code CLI on PATH | the agents (headless and interactive) | `claude --version` |
| GitHub CLI, authenticated with push rights | the dispatcher's eyes and hands | `gh auth status` |
| Python >= 3.9 + PyYAML | the dispatcher runtime | `python -c "import yaml"` |
| `pythonw.exe` on PATH (Windows) | the scheduled task runs without a console | `where pythonw` |
| Node >= 22.20 | the `skills` CLI floor | `node --version` |
| A Claude credential for GitHub Actions | the Verifier/Reviewer agents in CI | created in section 8's OWNER STEP 2 |
| Docker Desktop + VS Code Dev Containers | the dev container (section 5) | `docker info` |

A GitHub repository with a remote must exist, with two branches: `main` (default,
protected, human-only) and `dev` (integration; the Reviewer merges here). Create `dev`
from `main` before anything else and push it.

---

## 3. Repository layout

```
.claude/
  skills/                 the agent skills, COMMITTED (section 6) -- host and CI read the same set
.orca/
  dispatch.yml            config: autonomy, labels->skills, gates, breaker, dispatcher tuning
  setup_skills.sh         fetches every skill into .claude/skills/ (run once, then commit)
  roles/
    planner.md            the Planner prompt (interviews + headless research and audits)
    architect.md          the Architect prompt (headless: contracts, schemas, ADRs)
    implementer.md        the Implementer prompt (headless: one issue -> code+tests -> PR)
  dispatcher/
    dispatch.py           the reconciler (full source in section 9)
    install-task.ps1      registers it as a Windows scheduled task
    dispatcher-pause.cmd  the brake, for double-clicking or pinning
    dispatcher-resume.cmd lifts it
    README.md             operations crib sheet
    state.json            (gitignored) de-dup memory; safe to delete
    paused.json           (gitignored) present = this project is paused
    prompts/              (gitignored) the brief files headless runs are pointed at
    runs/                 (gitignored) one log file per headless run -- your window into them
    dispatcher.log        (gitignored)
.github/workflows/
  agent-pipeline.yml      the Verifier + Reviewer, in GitHub Actions (section 8)
  daily-digest.yml        the one daily e-mail (section 11)
docs/
  CORE_DOCUMENT.md        single source of truth; everything derives from it
  specs/                  docs/specs/<topic>.md
  adrs/                   docs/adrs/ADR-<issue#>-<slug>.md   (numbered by ISSUE, never a counter)
  research/               docs/research/issue-<issue#>-<slug>.md
mail/
  daily_digest.py         the one daily e-mail (section 11)
.devcontainer/            the reproducible toolchain (section 5)
.gitattributes            *.sh pinned to LF
.gitignore
```

Note what is GONE relative to v0.1.1: `~/.orca/roles/` and `~/.claude/skills/` as
host-level shared directories. Roles and skills now live **in the repository** (sections
5-6 say why), so a fresh clone -- or a fresh CI runner -- has everything.

Create the tree:

```bash
mkdir -p .claude/skills .orca/roles .orca/dispatcher docs/adrs docs/specs docs/research mail .github/workflows
```

### `.gitignore`

```
mail/__pycache__/
.orca/dispatcher/__pycache__/
.orca/dispatcher/state.json
.orca/dispatcher/state.tmp
.orca/dispatcher/paused.json
.orca/dispatcher/paused.tmp
.orca/dispatcher/prompts/
.orca/dispatcher/runs/
.orca/dispatcher/dispatcher.lock
.orca/dispatcher/dispatcher.log*
```

(`.claude/skills/` is deliberately NOT ignored: the skills are content, and CI needs them.)

### `.gitattributes`

```gitattributes
* text=auto eol=lf

# Executed inside Linux (the dev container, and every GitHub Actions runner). A CRLF here
# is invisible on Windows and fails on Linux with `set: pipefail: invalid option name`,
# which points nowhere near the real cause.
*.sh text eol=lf
```

---

## 4. Step 1 -- the core document

**Every project starts from a core document, and nothing else exists until it does.** It is
the single source of truth; every later artefact -- specs, ADRs, issues, tests -- derives
from it. The Planner populates it by **deep interview with the human**: never inferred,
never defaulted.

The dispatcher enforces this mechanically: **no issue is dispatched while the copy on
`dev` still contains `Status: EMPTY`** (the `gates.core_document` block in
`dispatch.yml`). The Planner's onboarding PR opens the gate by merging.

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
(the full protocol is in the Planner role, section 7):

- **Interview in rounds.** Ask, record, read back, ask what is now wrong or missing.
- **An unknown is recorded as OPEN, never guessed.** A guess written down is a defect
  shipped, and every later artefact inherits it.

> If the project already has a core document, **port it rather than re-interviewing**, then
> change the Status line and open the PR to `dev` the same way.

---

## 5. Step 2 -- the dev container

Standard in v0.2.1, no longer an appendix. The workflow's AGENTS do not strictly need it
-- headless runs execute on the host and the Verifier/Reviewer run on GitHub's runners --
but the humans do: it pins one toolchain so a test that passes for you passes for a
collaborator, it is the shape the project deploys in later, and it is where "works on my
machine" arguments go to die. Skip it only for a throwaway repository, and know you are
choosing that.

One change from v0.1.1: the skills-install step is GONE from `post-create.sh`, because
skills are committed in `.claude/skills/` and arrive with the checkout (section 6).

### `.devcontainer/Dockerfile`

```dockerfile
# The reproducible toolchain for tests and builds.
#
# It pins the toolchain so a test that passes here passes on any machine, and it is the shape
# the project deploys in later. It is NOT a sandbox: the container is not a security boundary.
#
# Orca is not installed. It is a desktop application on the host that owns the worktrees; it
# cannot live inside the environment it manages, and it is not published to npm at all.

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

  // No docker.sock. See "Isolation" in section 1.

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
# change with the lockfiles. (Skills are committed in .claude/skills/ and arrive with the
# checkout -- the v0.1.1 install step is gone on purpose.)
#
# Deliberately NOT `set -e` around the whole file. A failed optional step must not leave a
# developer with no container at all; a failed dependency install must.
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
```

---

## 6. Step 3 -- skills, committed to the repository

All fourteen skills from v0.1.1 are kept. What changes is WHERE they live and HOW they are
routed.

**Where.** v0.1.1 installed skills globally on the host (`~/.claude/skills/`). That worked
for host agents and was invisible to everything else -- and v0.2 has agents running on
GitHub's runners, which have no host. So skills now install into the **project scope**
(`.claude/skills/` inside the repo) and are **committed**. Consequences, all good:

- The Verifier and Reviewer in CI get the exact same skills, for free, from the checkout.
- A fresh clone of the repo is fully equipped. No per-machine setup step to forget.
- The set is versioned: a skill update is a reviewable diff, not silent drift.
- The v0.1.1 pitfall class "skills installed in the wrong scope, so no agent had any"
  is structurally gone -- there is only one scope, and it travels with the code.

(v0.1.1 said "per-project copies drift", and for hand-written role prompts shared across
projects that was true. Fetched, versioned skill directories are the opposite case:
committing a pinned copy is what makes CI reproducible. To share the setup across
projects, keep a template repository and copy `.claude/`, `.orca/` and
`.github/workflows/agent-pipeline.yml` into new projects -- which is how a devops team
would do it anyway.)

**How they are routed.** Claude Code auto-surfaces a skill when its description matches
the task, so committed skills are *available* to every run. The label -> skills map in
`dispatch.yml` is kept as an **explicit pointer**: the dispatcher writes the matching
skill names into the run's brief ("skills earned by this issue's labels: ..."), because an
explicit "use the `tdd` skill" outperforms hoping the auto-match fires. Labels are claims
about the work; they earn the skills that work needs.

### The skill matrix (unchanged skills, new routing)

| Used by | Skills | Repository |
| --- | --- | --- |
| every run | `handoff` | `mattpocock/skills` |
| Planner (interviews) | `grill-with-docs`, `domain-modeling` | `mattpocock/skills` |
| Planner + label `architecture` | `improve-codebase-architecture`, `codebase-design` | `mattpocock/skills` |
| label `research` | `just-scrape` | `scrapegraphai/just-scrape` |
| label `bug` | `diagnosing-bugs` | `mattpocock/skills` |
| label `ui` | `frontend-design` | `anthropics/skills` |
| label `ui` | `web-design-guidelines` | `vercel-labs/agent-skills` |
| label `ui` | `high-end-visual-design` | `leonxlnx/taste-skill` |
| label `seo` | `seo-audit` | `coreyhaines31/marketingskills` |
| label `scraper` | `just-scrape` | `scrapegraphai/just-scrape` |
| Implementer (always) | `tdd` | `mattpocock/skills` |
| Verifier (CI) + label `ui` | `webapp-testing` | `anthropics/skills` |
| Reviewer (CI) | `code-review` | `mattpocock/skills` |
| label `data` | `codebase-design` | `mattpocock/skills` |

> `just-scrape` needs a ScrapeGraph API key to actually run; without one it hangs on an
> interactive prompt. The Implementer role therefore says: use it when configured,
> otherwise fetch directly. Tell the owner (section 18) it is optional.

### `.orca/setup_skills.sh`

```bash
#!/usr/bin/env bash
#
# Fetch every agent skill this workflow uses, into the PROJECT scope (.claude/skills/),
# so it can be committed and shared with CI. Run once at setup, re-run to update, then
# commit the result.
#
# The CLI verb is `add`, not install. There is no --target. The agent id is `claude-code`.
# Node >= 22.20.0 is required.
#
# Every skill name here was read back from the repository with `--list` rather than copied
# from a plan. A name that is wrong breaks the setup for everyone.
set -uo pipefail

SKILLS_CLI="${SKILLS_CLI:-npx -y skills@latest}"
AGENT="${SKILLS_AGENT:-claude-code}"

# PROJECT scope is the default in v0.2: skills are committed with the repo so the host
# runs and the GitHub Actions runs read the identical set. Override with
# SKILLS_SCOPE=global only if you know why.
SKILLS_SCOPE="${SKILLS_SCOPE:-project}"
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

echo "Installing agent skills (agent: $AGENT, scope: $SKILLS_SCOPE)"

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
  echo "All skills installed. Now COMMIT them:"
  echo "  git add .claude/skills && git commit -m 'Add agent skills'"
else
  echo "Failed (${#failed[@]}):" >&2
  printf '  %s\n' "${failed[@]}" >&2
  echo "Re-run this script; GitHub rate limits are the usual cause." >&2
  exit 1
fi
```

Run it once from the repo root: `bash .orca/setup_skills.sh`.
Verify: `ls .claude/skills` shows all fourteen. **Then commit the directory** -- an
uncommitted skill exists only on your machine, and the CI agents will silently lack it.

---

## 7. Step 4 -- the three role prompts

v0.1.1 had seven role files at `~/.orca/roles/`. v0.2.1 has **three, in the repository**
at `.orca/roles/`: the Planner, the Architect and the Implementer. The Researcher of
v0.1.1 folds into the Planner -- establishing facts and owning scope are the same mind --
and `research`-labelled issues simply load the Planner headless. The Verifier and
Reviewer prompts are embedded in the Actions workflow (section 8) because that file is
the self-contained unit
a repository carries into CI. The old `dispatcher.md` policy file is folded into the
dispatcher README.

Why in-repo now: the roles must reach CI, and the repo is the only thing CI reliably has.
The drift v0.1.1 feared is handled the same way as skills: a template repository is the
sharing mechanism, and a role edit is a reviewable commit instead of an invisible
host-file change.

| Role | Path | Runs as | Owns | May merge |
| --- | --- | --- | --- | --- |
| Planner | `.orca/roles/planner.md` | interactive (interviews) and headless (research issues, backlog audits) | core document, specs, issues, facts + evidence | nothing |
| Architect | `.orca/roles/architect.md` | headless, per `architecture` issue | contracts, schemas, boundaries, ADRs | nothing |
| Implementer | `.orca/roles/implementer.md` | headless, one run per issue or fix cycle | one issue in one worktree, incl. its tests | nothing |
| Verifier | inline in `agent-pipeline.yml` | GitHub Actions | `state:*` labels, FMEA | nothing |
| Reviewer | inline in `agent-pipeline.yml` | GitHub Actions | standards, spec compliance, evidence gates | **`dev` only** |

Nobody merges to `main`. That is the human's, always.

### A. `.orca/roles/planner.md`

```markdown
You are the Planner. You combine what v0.1 called the PO & Analyst and the Architect:
you own WHAT the project is and the CONTRACTS it is built on.

RESPONSIBILITIES:
1. Own `docs/CORE_DOCUMENT.md`. Every project starts from it and every later artefact is
   derived from it. Populate it by DEEP INTERVIEW with the human. Never infer, never
   assume, never fill a gap with a plausible default.
2. Once it is agreed and merged into `dev`: derive `docs/specs/*.md` and ADRs, freeze the
   system contracts (interface types, schemas, API shapes, module boundaries), and write
   the GitHub issues that drive all further work.
3. Answer questions of FACT: issues labelled `research` load this role, headless. See
   RESEARCH DUTIES below.
4. When the pipeline drains, audit the backlog against the core document (you will be
   dispatched headless with a brief that says so).

INTERVIEW PROTOCOL (interactive sessions only):
- The core document comes FIRST. No architecture, no issues and no code exist until it is
  agreed with the human.
- Interview in rounds. Each round: ask, record the answer in the document, read it back,
  ask what is now wrong or missing.
- Interrogate rather than collect. Push until a boundary is sharp enough that an
  Implementer could not build the wrong thing without noticing.
- Cover at minimum: purpose and success criteria; target users; scope and explicit
  NON-scope; the domain model and its vocabulary; data sources and their constraints;
  external systems; legal, privacy and compliance limits; what must never happen.
- Where the human does not know, record it as OPEN with their name against it. An open
  question written down is a decision waiting; a guess written down is a defect shipped.
- Use `grill-with-docs` for the interview and `domain-modeling` to fix the vocabulary. One
  name per concept, written down, used everywhere afterwards.
- When the human says it is agreed: replace `Status: EMPTY` with `Status: AGREED -- <date>`,
  commit, `git push -u origin HEAD`, and open a pull request into `dev`
  (`gh pr create --base dev --title "Populate core document from owner interview"`).
  The pipeline reviews and merges it. Nothing can be dispatched before it is on `dev`.

SPECS, ADRS AND THE ARCHITECT (after the core document is on `dev`):
- Derive `docs/specs/<topic>.md` from the core document. Where you record a decision of
  your own, it is an ADR: `docs/adrs/ADR-<issue#>-<slug>.md`, numbered by the GitHub
  issue that produced it -- never a running counter, so parallel work cannot collide.
  One decision per ADR, with the alternatives rejected and why.
- SYSTEM CONTRACTS ARE NOT YOURS. Interface types, schemas, API shapes and module
  boundaries belong to the Architect: file `architecture` issues for them, and order the
  work so contracts freeze BEFORE the feature issues that depend on them (typical:
  research -> architecture -> implementation, expressed with `Depends on:` lines).

RESEARCH DUTIES (headless; issues labelled `research` load THIS role):
- MEASURE, DO NOT INFER. "The documentation says" is not an observation. Establish what
  an external source, endpoint or page ACTUALLY returns: use `just-scrape` when it is
  configured, otherwise fetch directly (curl / direct requests / package inspection).
  Never let a missing tool turn into a guess.
- Quote the request you sent and the response you got. A finding without its evidence is
  a rumour with a citation. Label a hypothesis as a hypothesis; report an honest UNKNOWN
  rather than a confident guess.
- Deliverables: `docs/research/issue-<issue#>-<slug>.md`; if the issue asks for a
  decision, record it as `docs/adrs/ADR-<issue#>-<slug>.md` with the rejected
  alternatives. Finish like any headless run: commit, `git push -u origin HEAD`,
  `gh pr create --base dev ... Refs #<issue#>`, then END YOUR RUN.

WRITING ISSUES (this is how work enters the system):
- Every issue is SELF-CONTAINED. Its two readers are a fresh headless agent with no
  memory of your planning session, and the owner deciding on a phone whether to approve
  it. Neither has your context; the text alone must carry it.
- Structure each issue as: CONTEXT (what the problem or gap is, why it exists, why it
  matters now -- whole sentences, not keywords), TASK (what is wanted), DONE WHEN
  (testable acceptance criteria anyone could check), OUT OF SCOPE (what a diligent agent
  might reasonably do here but must not). Vague issues are the #1 cause of failed runs.
- PLAIN LANGUAGE. Write so a bright newcomer to the project follows it. Any abbreviation
  or term of art that is not defined in the core document's vocabulary is spelled out at
  first use. Jargon does not make an issue smarter; it makes the run that reads it
  guess -- and runs are instructed to stop and ask rather than guess, so every unclear
  issue costs a round-trip through you.
- Labels: type labels `research` or `architecture` where the deliverable is documents
  rather than features (no type label = a build issue); skill labels `ui` `seo` `scraper`
  `bug` `data` where they apply; `trivial` when there is genuinely nothing for the
  Verifier to run.
- Ordering is expressed ONLY by a body line `Depends on: #a, #b`. The dispatcher holds an
  issue until every dependency is closed. Typical: research first; architecture depends
  on research; implementation depends on architecture.
- THE AUTONOMY RULE. Your brief states the project's autonomy mode:
    * `manual` or `propose`: label every issue you file `proposed`, NEVER `ready`. The
      human promotes issues to `ready` themselves; that is the design, not an oversight.
    * `auto`: label issues `ready` directly (plus `Depends on:` lines for ordering).
- `docs/CORE_DOCUMENT.md` is living. When a decision changes it, update it (through a PR)
  and say what changed; never let a spec contradict it silently.

BACKLOG AUDIT (headless; the brief says which mode):
- Compare what is merged on `dev` against the core document. Either file the missing
  issues (labels and `Depends on:` as above, `proposed` vs `ready` per the autonomy rule),
  or -- only when nothing is missing AND no OPEN item remains -- declare completion by
  setting the status line to `Status: ACHIEVED -- <date>` through a normal PR into `dev`.
  Never declare completion past an unanswered OPEN item: file it as a `needs-human` issue
  instead. State in one line which outcome you chose, then END YOUR RUN.

REVISION ROUND (interactive; the human wants more after completion):
- Interview them (same protocol) about new features, changes and feedback; update the
  core document; set `Status: AGREED -- <date>` again and PR it into `dev` -- that
  reopens the dispatch gate. Then derive specs/ADRs and file issues as above.

FORBIDDEN:
- You do not write implementation code.
- You do not freeze system contracts; the Architect does (you file the issues for it).
- You are STRICTLY FORBIDDEN from merging any branch into `main`. You do not merge into
  `dev` either; the Reviewer in CI does.
```

### B. `.orca/roles/architect.md`

```markdown
You are the Architect. You run HEADLESS: nobody is watching your terminal, and your
process ends when you stop. Everything a human or a later run must know goes on GitHub.

RESPONSIBILITIES:
1. Read `docs/CORE_DOCUMENT.md` and the specs derived from it.
2. Produce and FREEZE the system contracts on `dev` before feature work opens: interface
   types, schemas, API shapes, module boundaries.
3. Record each boundary decision as an ADR in `docs/adrs/`.
4. Define the repository layout and the seams new work must fit into.

DESIGN RULES:
- Prefer a deep module with a narrow interface to a shallow one with a wide interface.
- Contracts are frozen before feature worktrees open against them. A contract that
  changes while three worktrees depend on it is not a contract.
- If a frozen contract turns out to be wrong, say so ON THE ISSUE, file the correction as
  a new `architecture` issue with `Depends on:` lines blocking the affected work, and add
  `needs-human` if the change invalidates merged work. Never let it drift.
- Issues you file follow the writing standard in `.orca/roles/planner.md`: self-contained
  plain-language CONTEXT (what and why), TASK, DONE WHEN, OUT OF SCOPE.
- Never widen an interface to unblock one caller.
- Use `improve-codebase-architecture` and `codebase-design` from `.claude/skills/`.

FILES AND NAMES (parallel runs must not collide):
- ADR: `docs/adrs/ADR-<issue#>-<slug>.md` where `<issue#>` is the GitHub issue you were
  dispatched for. Never a running counter, never `docs/adr/` (singular).
- Spec: `docs/specs/<topic>.md`. One decision per ADR, with the alternatives rejected
  and why.

HOW YOU FINISH (a run that ends any other way stalls the pipeline):
- Commit, `git push -u origin HEAD`, then
  `gh pr create --base dev --title "<what> (#<issue#>)" --body "<decisions, alternatives rejected>. Refs #<issue#>"`.
  Say DONE in one line and END YOUR RUN. Do not wait; do not merge. If the CI Reviewer
  sends the PR back, a FRESH run with the comments in its brief will be dispatched --
  write your ADRs so that reader can pick up where you stopped.

FORBIDDEN:
- You do not implement features.
- You are STRICTLY FORBIDDEN from merging any branch into `main`, and you do not merge
  into `dev` either; the Reviewer in CI does.
```

### C. `.orca/roles/implementer.md`

```markdown
You are the Implementer. You run HEADLESS: nobody is watching your terminal, nothing you
print reaches a human unless you put it on GitHub, and your process ends when you stop.
You combine what v0.1 called the Developer and the Tester: the tests for your change are
YOUR change.

RESPONSIBILITIES:
1. Implement ONE issue inside the worktree you were started in. Your brief names it.
2. Read `docs/CORE_DOCUMENT.md`, the relevant spec and the ADRs before writing anything.
3. Ship the tests WITH the change, run everything that exists, and publish a pull request
   into `dev`. An independent Verifier in CI will re-run and probe your work -- write as
   if a hostile reader gets your diff, because one does.

ISSUE TYPES (from the issue's labels):
- no type label -> a build issue: code + tests.
- `bug` -> reproduce and localise with `diagnosing-bugs` BEFORE proposing a fix; the
  reproduction becomes a regression test that ships with the fix.
- (`research` and `architecture` issues never reach you: they load the Planner and the
  Architect. If your issue depends on their output, the ADRs and research notes under
  `docs/` are your inputs, not your job.)

CODE STANDARD:
- Easily readable, minimalist code. No unnecessary abstraction, no premature
  generalisation, no design pattern the problem did not ask for. The Reviewer rejects all
  three.
- Match the surrounding code: its naming, its idioms, its comment density.
- Comment WHY, never what. Complete docstrings on every public function, class, module.
- The smallest change that satisfies the issue. Anything more is scope you were not given.

TESTS (you are the tester now):
- Use the `tdd` skill: write the test first where the shape of the work allows it.
- New behaviour ships with tests in the same PR. Run the FULL suite, linters, type
  checks and builds that exist before you open the PR; fix what you broke.
- A fake that behaves better than the real thing proves nothing. A test can pin a
  mistake: if your change is right and an old test is wrong, change the test
  deliberately, in its own commit, with the reason stated.

REPORTING:
- The PR body states what you changed, why, and WHAT YOU MEASURED. For anything touching
  data or a pipeline that means a number, not an adjective (see `evidence_gates` in
  `.orca/dispatch.yml`): a count reconciled against an independent source, a sample of
  real output pasted in, a cost against a documented budget. The Reviewer will not merge
  claims without evidence.
- A defect you found but did not fix is written down in the PR. A finding that the issue
  contradicts the core document or a spec is reported, not routed around.
- An issue that does not give you enough context to know what is wanted and WHY is the
  same kind of defect: comment naming exactly what is missing, add `needs-human`, and
  end your run. A confident build of your best guess is worse than a question.

HOW YOU FINISH (a run that ends any other way stalls the pipeline):
- Commit in small clear commits, `git push -u origin HEAD`, then
  `gh pr create --base dev --title "<what> (#<issue#>)" --body "<summary, evidence>. Refs #<issue#>"`.
  Then say DONE in one line and END YOUR RUN. Do not wait for anything. Do not merge.
  The CI pipeline takes over; the dispatcher closes the issue when the PR merges.

FIX CYCLES (your brief will say "this PR was sent back" and include the comments):
- You are a FRESH run; your memory is the PR. Read the PR conversation and the diff,
  address every point, commit, `git push`.
- Reply on the PR with what you changed: `gh pr comment <pr> --body "..."`.
- Hand it back: `gh pr edit <pr> --remove-label state:blocked`. The push re-triggers the
  CI pipeline automatically.
- If a point is wrong or impossible, say so on the PR, add `needs-human`, and END YOUR
  RUN. Do not argue in circles.

NEEDING THE HUMAN (a decision, a credential, a missing skill):
- Post a comment on the ISSUE or PR explaining exactly what you need and what you will do
  with the answer. Add the label `needs-human` (`gh issue edit N --add-label needs-human`
  or `gh pr edit N --add-label needs-human`). Commit and push any work worth keeping.
  Then END YOUR RUN. The human answers in the comments and removes the label; a fresh run
  (you, later, with no memory of now) will be dispatched and will read those comments --
  so write the comment for that reader.

FORBIDDEN:
- Never merge your own work. Never touch `main`. Never force-push.
- Never leave work uncommitted when you end -- a headless run's uncommitted work is gone.
```

---

## 8. Step 5 -- the GitHub Actions pipeline

This is the mechanical half of the old pipeline -- the Tester (now **Verifier**) and the
**Reviewer** -- moved off your machine and into GitHub Actions, where every run gets a
fresh, disposable computer that is destroyed when the job ends. There is nothing to
orphan and nothing to clean up, ever, for these two stages.

**How it chains.** One workflow file, three jobs in a row: `classify` -> `verify` ->
`review`. They are chained with `needs:` (job B starts when job A ends) rather than with
label-triggered separate workflows. This is deliberate: GitHub has a safety rule that
*events created by the built-in Actions token do not start new workflows* -- so if the
Verifier's "I passed" label were supposed to trigger a separate Reviewer workflow, the
Reviewer would simply never run, silently. Chaining with `needs:` inside one workflow
sidesteps the whole trap (section 17 has the pitfall entry). The labels `state:tested` /
`state:blocked` are still written -- but as **signals for you and the dispatcher**, not as
the chaining mechanism.

**What triggers it.** Any pull request into `dev` being opened or updated. The
Implementer opens PRs with your `gh` login (a real user), so the trigger always fires.
When a fix cycle pushes new commits, the same workflow runs again from the top -- and the
`concurrency` block cancels any run that the new push made obsolete, so pushes never
stack up runs (and never stack up cost).

**What each job does:**

- `classify` -- no AI. Clears stale `state:*` labels from the previous round, then looks
  at the diff: a docs-only change (only `docs/`, `*.md`, `*.txt`) or an issue labelled
  `trivial` skips the Verifier (the Reviewer still reads every line). Docs-only PRs get
  `state:tested` set right here, for free.
- `verify` -- the Verifier agent, fresh context, adversarial by instruction: runs the
  test suite, linters and builds; probes the change against the issue's acceptance
  criteria; checks that new behaviour has tests; runs the FMEA protocol for risky work
  (section 15). Ends with exactly one of `state:tested` / `state:blocked` plus a comment.
  It does not edit code -- weak tests are a `state:blocked` message to the Implementer,
  not something the Verifier patches over.
- `review` -- the Reviewer agent, also fresh context: spec compliance against
  `docs/specs/` and the core document, the code standard, the evidence gates of section
  14, the docs-only naming rules. It either **merges into `dev`** (squash) or sets
  `state:blocked` with a numbered list of required changes. If the Verifier blocked the
  PR, the review job notices the label and stands down without spending tokens.

After the pipeline: `state:blocked` -> the dispatcher on your machine sends a fresh
headless fix run (section 9). Merged -> the dispatcher closes the issue and removes the
worktree. Nothing on GitHub's side waits for anything.

### OWNER STEP 1 -- let the workflow write to your repository

*(Plain language, for the project owner. Takes one minute, once per repository.)*

The pipeline needs permission to add labels, write comments, and merge pull requests into
the `dev` branch. GitHub ships new repositories with this switched off.

1. Open your repository on the GitHub website.
2. Click **Settings** (the tab with the gear icon, top of the page).
3. In the left sidebar, click **Actions**, then **General**.
4. Scroll to the section called **Workflow permissions**.
5. Select **"Read and write permissions"**.
6. Click **Save**.

That is all. If you skip this, the pipeline will run but fail at the moment it tries to
label or merge, with a "Resource not accessible by integration" error.

### OWNER STEP 2 -- give the cloud agents a Claude credential

*(Plain language, for the project owner. Takes five minutes, once per repository.)*

The Verifier and Reviewer run on GitHub's computers, so they cannot use the Claude login
on your machine. They need their own credential, stored as a repository **secret** --
a value only your repository's workflows can read. You need ONE of the following two.
If you pay for a Claude subscription (Pro or Max), route A costs you nothing extra;
route B uses pay-per-use API billing instead.

**Route A -- use your Claude subscription:**

1. On your own computer, open a terminal (on Windows: press the Start key, type
   `terminal`, press Enter).
2. Type `claude setup-token` and press Enter. A browser window opens; approve it.
3. The terminal prints a long code starting with `sk-ant-`. Copy the whole thing.
4. The secret's name will be: `CLAUDE_CODE_OAUTH_TOKEN`.

**Route B -- use an Anthropic API key (pay per use):**

1. Go to `console.anthropic.com` in your browser and sign in (or create an account).
2. In the left menu choose **API Keys**, then click **Create Key**. Give it any name,
   e.g. "github agents". Copy the key it shows you -- it also starts with `sk-ant-`
   and is shown only once.
3. The secret's name will be: `ANTHROPIC_API_KEY`.

**Now store it in the repository (same clicks for either route):**

1. Open your repository on the GitHub website and click **Settings**.
2. In the left sidebar, click **Secrets and variables**, then **Actions**.
3. Click the green **New repository secret** button.
4. In **Name**, type the secret's name from above, EXACTLY as written
   (`CLAUDE_CODE_OAUTH_TOKEN` or `ANTHROPIC_API_KEY`).
5. In **Secret**, paste the long code you copied.
6. Click **Add secret**.

Set exactly one of the two. The workflow file below mentions both and uses whichever
exists.

### `.github/workflows/agent-pipeline.yml`

```yaml
name: Agent pipeline

# The mechanical half of the workflow: Verifier + Reviewer, on ephemeral runners.
# Chained with `needs:` inside ONE workflow on purpose -- labels set with the built-in
# Actions token do NOT trigger other workflows, so label-chained designs die silently.

on:
  pull_request:
    types: [opened, synchronize, reopened, ready_for_review]
    branches: [dev]

permissions:
  contents: write        # merge into dev
  pull-requests: write   # labels, comments, merge
  issues: write          # read/label linked issues

# A new push makes the previous run obsolete: cancel it. This is the cost brake.
concurrency:
  group: agent-pipeline-${{ github.event.pull_request.number }}
  cancel-in-progress: true

jobs:
  classify:
    if: github.event.pull_request.draft == false
    runs-on: ubuntu-latest
    timeout-minutes: 10
    outputs:
      skip_verify: ${{ steps.decide.outputs.skip_verify }}
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Clear stale pipeline labels from the previous round
        env:
          GH_TOKEN: ${{ github.token }}
          PR: ${{ github.event.pull_request.number }}
        run: |
          gh pr edit "$PR" --remove-label state:tested  || true
          gh pr edit "$PR" --remove-label state:blocked || true

      - name: Decide docs-only / trivial
        id: decide
        env:
          GH_TOKEN: ${{ github.token }}
          PR: ${{ github.event.pull_request.number }}
          HEAD_REF: ${{ github.head_ref }}
        run: |
          set -uo pipefail
          git fetch -q origin dev
          files=$(git diff --name-only "origin/dev...HEAD")
          docs_only=true
          if [ -z "$files" ]; then docs_only=false; fi
          while IFS= read -r f; do
            [ -z "$f" ] && continue
            case "$f" in
              docs/*|*.md|*.txt) ;;
              *) docs_only=false ;;
            esac
          done <<< "$files"

          trivial=false
          issue=$(printf '%s' "$HEAD_REF" | grep -oE 'issue-[0-9]+' | grep -oE '[0-9]+' | head -n1 || true)
          if [ -n "$issue" ]; then
            if gh issue view "$issue" --json labels --jq '.labels[].name' | grep -qx 'trivial'; then
              trivial=true
            fi
          fi

          skip=false
          if [ "$docs_only" = true ]; then
            skip=true
            gh pr edit "$PR" --add-label state:tested || true
            gh pr comment "$PR" --body "Verifier skipped (docs-only change); the Reviewer still reads every line." || true
          elif [ "$trivial" = true ]; then
            skip=true
            gh pr edit "$PR" --add-label state:tested || true
            gh pr comment "$PR" --body "Verifier skipped (issue labelled trivial); the Reviewer still reads every line -- trivial is a request, not a permission." || true
          fi
          echo "skip_verify=$skip" >> "$GITHUB_OUTPUT"

  verify:
    needs: classify
    if: needs.classify.outputs.skip_verify != 'true'
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      # --- Project toolchain (ADAPT THIS BLOCK to the project, or delete it) -----------
      # The Verifier runs the project's own tests, so the runner needs the project's
      # toolchain. Uncomment / adjust what applies. If the project has no automated
      # tests yet, the Verifier will say so and judge the diff on its own.
      #
      # - uses: actions/setup-node@v4
      #   with: { node-version: '22' }
      # - run: npm ci
      # - uses: actions/setup-python@v5
      #   with: { python-version: '3.12' }
      # - run: pip install -e ".[dev]"
      # ---------------------------------------------------------------------------------

      - name: Verifier agent
        uses: anthropics/claude-code-action@v1
        env:
          GH_TOKEN: ${{ github.token }}
        with:
          # Set exactly ONE of these two secrets (OWNER STEP 2). If the action version
          # you are on rejects an empty input, delete the unused line.
          anthropic_api_key: ${{ secrets.ANTHROPIC_API_KEY }}
          claude_code_oauth_token: ${{ secrets.CLAUDE_CODE_OAUTH_TOKEN }}
          claude_args: "--max-turns 80"
          prompt: |
            You are the VERIFIER for pull request #${{ github.event.pull_request.number }}
            into `dev`. You are a fresh, independent, adversarial check on an
            Implementer's work. You run headless in CI; everything you want a human or
            the next agent to know goes on the pull request via `gh`.

            Do these in order:
            1. `gh pr view ${{ github.event.pull_request.number }} --comments` and
               `git diff origin/dev...HEAD`. Read the linked issue
               (`gh issue view <n> --comments` -- the branch is named issue-<n>), the
               relevant `docs/specs/*.md` and `docs/CORE_DOCUMENT.md`.
            2. Run everything that exists: test suite, linters, type checks, builds.
               Use the `tdd` and `webapp-testing` skills in `.claude/skills/` to judge
               test quality. Then probe the change yourself against the issue's
               acceptance criteria -- execute it, poke its edges, try to break it.
            3. Check that new behaviour has tests. Missing or cosmetic tests are a
               failure: name exactly which behaviours are untested. Remember: a fake
               that behaves better than the real thing proves nothing. DO NOT edit or
               commit code yourself -- weak work is a message to the Implementer, not
               something you patch.
            4. FMEA (only for changes labelled `data`, `ui` or `pipeline`, or that touch
               data fetching, storage or core frontend): before judging, write a failure
               matrix as a PR comment -- | Component | Failure Mode | Default Recovery |
               Human Confirmation Needed? |. If any row genuinely needs the human's
               judgement, post the table, add BOTH labels `needs-human` and
               `state:blocked` (`gh pr edit N --add-label needs-human --add-label state:blocked`),
               comment that the owner should answer each flagged row in a PR comment and
               then remove the `needs-human` label, and STOP -- that is a valid, complete
               outcome. Otherwise include the table in your final comment.
            5. Finish with EXACTLY ONE of:
               - PASS:  `gh pr edit ${{ github.event.pull_request.number }} --add-label state:tested`
                 then a comment: what ran, counts, what you probed.
               - FAIL:  `gh pr edit ${{ github.event.pull_request.number }} --add-label state:blocked`
                 then a comment: exactly what failed and what the Implementer must change,
                 as a numbered list.
               A run that ends without a label stalls the pipeline. Never merge anything.

  review:
    needs: [classify, verify]
    if: >-
      !cancelled() &&
      needs.classify.result == 'success' &&
      (needs.verify.result == 'success' || needs.verify.result == 'skipped')
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Stand down if the Verifier blocked the PR
        id: gate
        env:
          GH_TOKEN: ${{ github.token }}
          PR: ${{ github.event.pull_request.number }}
        run: |
          if gh pr view "$PR" --json labels --jq '.labels[].name' | grep -qx 'state:blocked'; then
            echo "blocked=true" >> "$GITHUB_OUTPUT"
          else
            echo "blocked=false" >> "$GITHUB_OUTPUT"
          fi

      - name: Reviewer agent
        if: steps.gate.outputs.blocked != 'true'
        uses: anthropics/claude-code-action@v1
        env:
          GH_TOKEN: ${{ github.token }}
        with:
          anthropic_api_key: ${{ secrets.ANTHROPIC_API_KEY }}
          claude_code_oauth_token: ${{ secrets.CLAUDE_CODE_OAUTH_TOKEN }}
          claude_args: "--max-turns 60"
          prompt: |
            You are the REVIEWER for pull request #${{ github.event.pull_request.number }}
            into `dev`. You are the last gate before integration. Fresh context, headless,
            in CI; everything you decide goes on the pull request via `gh`.

            Do these in order:
            1. `gh pr view ${{ github.event.pull_request.number }} --comments`,
               `git diff origin/dev...HEAD`, the linked issue (branch is issue-<n>), the
               relevant `docs/specs/*.md`, `docs/adrs/*.md` and `docs/CORE_DOCUMENT.md`.
               Use the `code-review` skill in `.claude/skills/`.
            2. Verify the change strictly fulfils the specification and does not
               contradict the core document. Enforce the code standard: easily readable,
               minimalist code -- reject unnecessary abstraction, premature
               generalisation, and design patterns the problem did not ask for.
            3. Evidence gates (for PRs labelled `data`, `ui` or `pipeline` -- read
               `evidence_gates` in `.orca/dispatch.yml`): the PR body must contain a
               count reconciled against an independent source, real output sampled in,
               and cost against a documented budget where one exists. Green tests alone
               are not evidence. "It completed" is not a count.
            4. For docs-only PRs (ADRs, specs, research): file names follow
               `docs/adrs/ADR-<issue#>-<slug>.md` / `docs/specs/<topic>.md` /
               `docs/research/issue-<issue#>-<slug>.md`; links resolve; numbering does
               not collide with ADRs already on `dev`; one decision per ADR with
               rejected alternatives.
            5. If the Verifier was skipped for `trivial` but the change touches
               behaviour: that label was a request, not a permission -- send it back for
               tests.
            6. Decide, with EXACTLY ONE of:
               - APPROVE: `gh pr merge ${{ github.event.pull_request.number }} --squash --delete-branch`
                 (this merges into `dev`), then one paragraph of review as a comment.
                 If the merge fails on conflicts, treat it as CHANGES and ask for a
                 rebase.
               - CHANGES: `gh pr edit ${{ github.event.pull_request.number }} --add-label state:blocked --remove-label state:tested`
                 then a NUMBERED comment of required changes.
            You may merge into `dev` and only `dev`; never `main`. You never close
            issues; the dispatcher does that on merge.
```

Two notes on this file, for whoever maintains it:

- **Action inputs drift.** `anthropics/claude-code-action` is actively developed. If a
  run fails complaining about an unknown or empty input, open the action's README on
  GitHub and adjust the `with:` block -- the prompts and the job structure are the stable
  part of this design, the input names are not.
- **The agents inherit the checkout**, which is why committed `.claude/skills/` (section
  5) just works in CI, and why the prompts can reference `docs/` and `.orca/` paths
  directly.

---

## 9. Step 6 -- the dispatcher

The dispatcher shrank. It no longer routes testers and reviewers (CI does), no longer
watches terminal liveness (there are no terminals to watch), and no longer needs the
docs-only classifier (CI does that too). What remains: dispatch `ready` issues as
headless runs -- the Planner for `research`, the Architect for `architecture`, the
Implementer otherwise -- dispatch fix runs when CI blocks a PR, close issues and remove
worktrees on merge, run the backlog loop, page you, and hold the brake.

### How a headless run works (read this before the source)

1. The dispatcher creates an Orca worktree off `dev` (isolation, as before) -- but starts
   **no terminal** in it.
2. It writes a **brief file** to `.orca/dispatcher/prompts/` -- exactly the v0.1.1
   mechanism, kept because it solved two real failures (CLI truncation of long prompts,
   and the triage value of being able to read precisely what an agent was told).
3. It starts `claude -p "Read the file <brief> and follow its instructions exactly."` as
   a plain background process **in the worktree directory**, with stdout+stderr going to
   a per-run log in `.orca/dispatcher/runs/`. `-p` is print mode: the agent works, prints,
   and **exits**. No TUI, no tab, no idle session.
4. The run is started with `--dangerously-skip-permissions`. This is load-bearing, not
   convenience: a headless run that hits a permission prompt hangs forever with nobody
   there to press "allow" -- the single most common way headless automation dies. The
   flag is also why the isolation section (1) exists: decide deliberately how contained
   you want these processes to be.
5. The dispatcher tracks the **PID and the wall clock** -- deterministic facts, unlike
   "does the TUI look idle". A run that exceeds `max_run_minutes` is killed (whole
   process tree) and the circuit breaker accounts for it. A run that exits without
   producing a PR gets exactly one fresh retry, then `needs-human`.
6. Success is observed on GitHub, never claimed by the agent: an open PR for the issue,
   a removed `state:blocked` label, new issues on the board. The run's own log is for
   your curiosity (`tail -f .orca/dispatcher/runs/<name>.log`), not for the state machine.

The ONLY interactive sessions left are the Planner interviews (`dispatch.py onboard`),
which spawn a real Orca terminal and bring it to the front -- because a conversation with
you is the entire point of those.

### `.orca/dispatch.yml`

Substitute `github_mention` for the owner's handle.

```yaml
version: 3

branches:
  base: dev
  protected: [main]          # no agent may merge into these; the human opens dev -> main

# THE AUTONOMY DIAL (section 14). Uncomment to preset; leave commented and
# `dispatch.py onboard` will ask the owner in plain language and write it here.
#   manual  - you write the issues; the Planner only files `proposed` issues when you
#             explicitly run onboarding/revision; no backlog audits.
#   propose - when the pipeline drains, the Planner audits and files `proposed` issues;
#             you promote the ones you want to `ready`.
#   auto    - the Planner files `ready` issues; the loop closes itself (v0.1.1 behaviour).
# autonomy: propose

dispatcher:
  poll_interval_seconds: 60
  max_active_issues: 3               # concurrent headless Implementer runs. These are real
                                     # local processes doing real work; 3 keeps a laptop
                                     # usable. Raise it on a workstation if you like --
                                     # idle hoarding is gone, this only bounds LIVE work.
  max_run_minutes: 30                # hard wall-clock ceiling per run; then kill + breaker
  retry_empty_run: true              # one fresh retry when a run exits without a PR
  cleanup_worktrees_on_merge: true
  claude_cmd: claude                 # absolute path if the scheduled task cannot find it
  permission_args: ["--dangerously-skip-permissions"]   # see "How a headless run works"
  extra_args: []                     # e.g. ["--model", "claude-sonnet-4-6"]
  interactive_agent: claude          # Orca agent id for the interview sessions only
  notify:
    # Ships EMPTY on purpose (v0.2.1): no instant pings; everything that needs you
    # reaches you in the daily digest (section 11). Put "@YourHandle" here only if you
    # WANT to be @-mentioned (and therefore e-mailed by GitHub) the moment something
    # needs a human.
    github_mention: ""

gates:
  core_document:                     # nothing is dispatched until the Planner's core document is on `dev`
    path: docs/CORE_DOCUMENT.md
    empty_marker: "Status: EMPTY"
    achieved_marker: "Status: ACHIEVED"

# A label is a claim about the work. It earns the skills that work needs; the dispatcher
# writes the matching names into the run's brief. All skills are available to every run
# (they are committed in .claude/skills/); this map is the explicit pointer.
labels:
  ui:
    skills: [frontend-design, web-design-guidelines, high-end-visual-design, webapp-testing]
  seo:
    skills: [seo-audit]
  scraper:
    skills: [just-scrape]
  bug:
    skills: [diagnosing-bugs]
  data:
    skills: [codebase-design]
  research:
    skills: [just-scrape]
  architecture:
    skills: [improve-codebase-architecture, codebase-design]

# Baseline skills named in every brief, per role; labels add to these.
roles:
  planner:
    skills: [handoff, grill-with-docs, domain-modeling]
  architect:
    skills: [handoff, improve-codebase-architecture, codebase-design]
  implementer:
    skills: [handoff, tdd]

circuit_breaker:
  max_cycles: 3                      # runs + send-backs per issue before `escalated`

# Enforced by the CI Reviewer (section 8 embeds the same rules in its prompt); kept here
# as the single written source both the Implementer and the Reviewer are pointed at.
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

### `.orca/dispatcher/dispatch.py` -- full source

The behavioural contract is the module docstring. Subcommands: `run`, `once`, `status`,
`doctor [--fix]`, `onboard`, `pause [-m WHY]`, `resume`; flags `--dry-run`, `--interval`,
`--verbose`.

```python
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

  NOTE (v0.2): the dispatcher spawns `claude` itself. Scheduled tasks often run with a
  minimal PATH; if `dispatch.py doctor` passes in your terminal but headless runs fail
  from the task, set `dispatcher.claude_cmd` in .orca/dispatch.yml to the ABSOLUTE path
  that `where claude` prints.

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

(Non-Windows: run `python .orca/dispatcher/dispatch.py run` under systemd/launchd instead;
the script is platform-neutral, only the installer is Windows-specific.)

### `.orca/dispatcher/dispatcher-pause.cmd`

The brake as a button. Double-click it, or pin a shortcut to the taskbar. (v0.1.1 listed
these two files but never gave their contents; that is fixed.)

```bat
@echo off
rem Soft-pause THIS project: nothing new starts, work in flight lands. Lift with resume.
cd /d "%~dp0..\.."
python .orca\dispatcher\dispatch.py pause -m "paused from desktop button"
echo.
pause
```

### `.orca/dispatcher/dispatcher-resume.cmd`

```bat
@echo off
cd /d "%~dp0..\.."
python .orca\dispatcher\dispatch.py resume
echo.
pause
```

### `.orca/dispatcher/README.md`

````markdown
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
````

---

## 10. Step 7 -- branch protection and repository settings

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

`dev` stays unprotected: the CI Reviewer must be able to merge there with the built-in
Actions token. Two consequences the system already handles: **merging a PR into `dev`
does not auto-close issues** ("Closes #N" only fires on the default branch), so the
dispatcher closes issues itself; and the Reviewer's merge does not need approval reviews,
because the review IS the pipeline.

Also required here: **OWNER STEP 1** (Actions write permissions) and **OWNER STEP 2**
(the Claude secret) from section 8, and **OWNER STEPS 3-4** (notifications) from
section 11, if they have not been done yet.

---

## 11. Step 8 -- notifications and the daily digest

**The doctrine, in one line: everything is a signal on GitHub; exactly one e-mail per day
tells you what the signals say.** v0.1.1 got this badly wrong in practice -- the
dispatcher mailed directly through its own sender address, every @mention triggered a
GitHub mail, and GitHub itself mailed the owner about every comment on every PR of a
watched repository. Three overlapping channels, dozens of mails a day, none of which
could be turned off without turning off the others. v0.2.1 replaces all of it:

- The dispatcher **never sends e-mail**. Its old SMTP channel is deleted, not just
  disabled. It writes labels and comments; that is all.
- `notify.github_mention` in `dispatch.yml` **ships empty**. Comments are posted without
  an @mention, so GitHub has no "you were mentioned" mail to send. (Set your handle there
  only if you decide you *want* instant pings.)
- GitHub's own per-event mail is switched off once, by you, in OWNER STEP 3 below.
- One workflow sends **one e-mail per day, around 18:00 Amsterdam time**, and only when
  there is something to say. It covers what landed, what was opened -- and, new in
  v0.2.1, a **"Waiting on you"** section listing every open item labelled `needs-human`,
  `escalated` or `proposed`, so nothing an agent asked for can slip past a day.

The honest trade-off, stated plainly: if a worker asks you a question at 09:00, you learn
about it at 18:00 -- unless you glance at the board (`dispatch.py status` or the GitHub
Issues page) or keep GitHub's *web/app* notifications, which OWNER STEP 3 leaves on. If
one day turns out to be too slow, there are two opt-ins that do not reopen the mail
floodgates: the GitHub phone app can send push notifications (not e-mails), and setting
`notify.github_mention` back to your handle restores instant pings for `needs-human`
moments only.

### OWNER STEP 3 -- switch off GitHub's per-event e-mails

*(Plain language, for the project owner. Takes two minutes, once per GitHub account.
Without this step, GitHub -- not this workflow -- will e-mail you about every comment.)*

1. Go to `github.com` and make sure you are signed in.
2. Click your profile photo in the top-right corner, then click **Settings**.
3. In the left sidebar, click **Notifications**.
4. Find the section about how you receive notifications (it mentions **Participating**
   and **Watching**). For both of them, **untick "Email"** and leave **"GitHub"** (the
   website bell) ticked.
5. That is all. From now on GitHub collects everything in the bell icon on the website
   (and in the GitHub phone app, if you install it), and sends you no mail. The one
   daily digest mail comes from this project's own workflow instead.

If you would rather keep GitHub mail for OTHER projects: instead of the above, open this
repository, click the **Watch** button (top of the page, next to Star), and choose
**Participating and @mentions**. Since v0.2.1 posts no @mentions, that quiets this
repository specifically.

### OWNER STEP 4 -- the address the daily digest is sent from

*(Plain language, for the project owner. Takes five minutes, once per repository. Skip it
and the digest still runs -- it just writes its report into the workflow's log on GitHub
instead of mailing you.)*

The digest needs an e-mail account to send FROM. Use a dedicated address (a free Gmail
works) -- do not use your personal password; create an **app password** instead:

1. Decide the sender account (e.g. `myproject.digest@gmail.com`) and the receiver (your
   normal address).
2. For Gmail: go to `myaccount.google.com`, turn on 2-step verification if it is not on,
   then search the settings for **"App passwords"**, create one named "digest", and copy
   the 16-character code it shows.
3. In your repository on GitHub: **Settings -> Secrets and variables -> Actions**, then
   **New repository secret**, five times, with these exact names:

| Name | Value |
| --- | --- |
| `SMTP_SENDER_EMAIL` | the sender address from step 1 |
| `SMTP_RECEIVER_EMAIL` | your address |
| `SMTP_PASSWORD` | the app password from step 2 |
| `SMTP_SERVER` | `smtp.gmail.com` (or your provider's SMTP server) |
| `SMTP_PORT` | `465` |

These five secrets are used by **exactly one thing**: the daily digest. The v0.1.1
behaviour where the dispatcher's own address mailed you updates throughout the day is
gone; if you had such an address set up, you can retire it or reuse it here -- either
way it will only ever send the one daily mail.

### When the mail arrives

GitHub runs scheduled jobs on UTC clocks and Amsterdam does not stay a fixed distance
from UTC, so the workflow below fires at `16:05 UTC`: **18:05 in Amsterdam during summer
time, 17:05 in winter**. If the winter hour bothers you, change the `16` to `17` in the
cron line when the clocks change (and back in spring); the pitfalls table has a reminder
entry. The five minutes past the hour are deliberate -- GitHub's queue is heavily loaded
exactly on the hour.

Quiet-day rule, updated for v0.2.1: no merged PRs, no opened PRs, no commits **and
nothing waiting on you** -> no mail at all. If anything carries `needs-human`,
`escalated` or `proposed`, the digest is sent even on an otherwise silent day -- an
unanswered question must not age quietly.

### `mail/daily_digest.py`

```python
"""Daily agent digest -- the workflow's ONE e-mail per day.

Derived from commit and pull-request history plus the live label state, never from
agent-written logs. Agents forget, parallel worktrees conflict on a shared file, and a
report assembled from self-description tells you what each agent believed -- the very
thing being checked.

Contract (see agent-workflow-setup.md section 11):

* Sources: ``git log <branch> --since=<day> --until=<day+1> --no-merges``,
  ``gh pr list --base <branch> --state merged|all --json ...`` and
  ``gh issue list --state open --json ...`` for the attention section.
* Sections, in order: WAITING ON YOU (open items labelled needs-human / escalated /
  proposed), pull requests opened today, pull requests merged today, commits by author.
* Quiet day: nothing landed, nothing opened AND nothing waiting -> prints a line and
  sends no mail. Anything waiting -> the mail is sent even on a silent day.
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

# Open items carrying any of these labels appear under WAITING ON YOU.
ATTENTION_LABELS = ("needs-human", "escalated", "proposed")


@dataclass(frozen=True)
class Commit:
    """One commit, as reported by ``git log``."""

    sha: str
    author: str
    subject: str


@dataclass(frozen=True)
class PullRequest:
    """One pull request, as reported by ``gh pr list``."""

    number: int
    title: str
    author: str
    labels: tuple[str, ...]
    url: str
    stamp: str  # mergedAt for merged lists, createdAt for opened lists


@dataclass(frozen=True)
class Attention:
    """An open issue or pull request that is waiting on the human."""

    kind: str  # "issue" or "PR"
    number: int
    title: str
    url: str
    flags: tuple[str, ...]  # the attention labels it carries


def parse_args() -> argparse.Namespace:
    """Parse the CLI. All arguments are optional; defaults match the workflow."""

    parser = argparse.ArgumentParser(
        description="Daily agent digest: git history, pull requests, and open items "
                    "waiting on the human.",
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
    """Return the day to report on. Bad input falls back to today rather than raising --
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


def _gh_list(argv: list[str]) -> list[dict]:
    """Run a gh listing command and parse its JSON. Empty list on any failure."""

    ok, out = _run(argv)
    if not ok or not out.strip():
        return []
    try:
        parsed = json.loads(out)
    except json.JSONDecodeError as exc:
        print(f"!! gh returned unparseable JSON: {exc}", file=sys.stderr)
        return []
    return parsed if isinstance(parsed, list) else []


def _parse_stamp(raw: str) -> datetime | None:
    """Parse gh's ISO-8601 timestamps. Returns None if unparseable."""

    if not raw:
        return None
    try:
        return datetime.fromisoformat(raw.replace("Z", "+00:00"))
    except ValueError:
        return None


def _window(day: date) -> tuple[datetime, datetime]:
    start = datetime.combine(day, datetime.min.time(), tzinfo=timezone.utc)
    return start, start + timedelta(days=1)


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
        # The subject can itself contain UNIT_SEP in the pathological case; rejoin the
        # tail rather than dropping content.
        sha, author = parts[0], parts[1]
        subject = UNIT_SEP.join(parts[2:])
        commits.append(Commit(sha=sha, author=author, subject=subject))
    return commits


def _pull_from_entry(entry: dict, stamp_field: str) -> PullRequest:
    author_field = entry.get("author") or {}
    author = author_field.get("login") or "unknown"
    labels = tuple(
        item.get("name", "")
        for item in (entry.get("labels") or [])
        if item.get("name")
    )
    return PullRequest(
        number=int(entry.get("number", 0)),
        title=entry.get("title", ""),
        author=author,
        labels=labels,
        url=entry.get("url", "") or "",
        stamp=entry.get(stamp_field) or "",
    )


def read_merged_prs(branch: str, day: date) -> list[PullRequest]:
    """Pull requests merged into ``branch`` during the UTC day given.

    gh has no ``--merged-on`` filter, so we list merged PRs and window-filter locally.
    """

    entries = _gh_list(
        [
            "gh", "pr", "list",
            "--base", branch,
            "--state", "merged",
            "--limit", "200",
            "--json", "number,title,author,labels,mergedAt,url",
        ]
    )
    start, end = _window(day)
    prs: list[PullRequest] = []
    for entry in entries:
        merged = _parse_stamp(entry.get("mergedAt") or "")
        if merged is None or not (start <= merged < end):
            continue
        prs.append(_pull_from_entry(entry, "mergedAt"))
    prs.sort(key=lambda pull: pull.stamp)
    return prs


def read_opened_prs(branch: str, day: date) -> list[PullRequest]:
    """Pull requests OPENED against ``branch`` during the UTC day given, whatever their
    state now -- the reader wants to know work was published, not only that it landed."""

    entries = _gh_list(
        [
            "gh", "pr", "list",
            "--base", branch,
            "--state", "all",
            "--limit", "200",
            "--json", "number,title,author,labels,createdAt,url",
        ]
    )
    start, end = _window(day)
    prs: list[PullRequest] = []
    for entry in entries:
        created = _parse_stamp(entry.get("createdAt") or "")
        if created is None or not (start <= created < end):
            continue
        prs.append(_pull_from_entry(entry, "createdAt"))
    prs.sort(key=lambda pull: pull.stamp)
    return prs


def read_attention(branch: str) -> list[Attention]:
    """Every OPEN issue and pull request carrying an attention label, regardless of age.

    This is the section that lets one mail a day be enough: a `needs-human` question,
    an `escalated` issue or a `proposed` suggestion reappears every day until acted on.
    """

    items: list[Attention] = []

    for entry in _gh_list(
        [
            "gh", "issue", "list",
            "--state", "open",
            "--limit", "200",
            "--json", "number,title,labels,url",
        ]
    ):
        names = {item.get("name", "") for item in (entry.get("labels") or [])}
        flags = tuple(label for label in ATTENTION_LABELS if label in names)
        if flags:
            items.append(Attention(
                kind="issue",
                number=int(entry.get("number", 0)),
                title=entry.get("title", ""),
                url=entry.get("url", "") or "",
                flags=flags,
            ))

    for entry in _gh_list(
        [
            "gh", "pr", "list",
            "--base", branch,
            "--state", "open",
            "--limit", "200",
            "--json", "number,title,labels,url",
        ]
    ):
        names = {item.get("name", "") for item in (entry.get("labels") or [])}
        flags = tuple(label for label in ATTENTION_LABELS if label in names)
        if flags:
            items.append(Attention(
                kind="PR",
                number=int(entry.get("number", 0)),
                title=entry.get("title", ""),
                url=entry.get("url", "") or "",
                flags=flags,
            ))

    # Questions before escalations before suggestions; then oldest number first.
    order = {label: rank for rank, label in enumerate(ATTENTION_LABELS)}
    items.sort(key=lambda a: (min(order[f] for f in a.flags), a.number))
    return items


def _pr_list_html(prs: list[PullRequest]) -> list[str]:
    escape = html.escape
    parts: list[str] = ["<ul>"]
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
    return parts


def render_html(
    branch: str,
    day: date,
    attention: list[Attention],
    opened: list[PullRequest],
    merged: list[PullRequest],
    commits: list[Commit],
) -> str:
    """Render the digest as HTML. Every value from git or gh is escaped."""

    escape = html.escape
    parts: list[str] = [
        f"<h1>Daily digest &mdash; {escape(branch)} &mdash; {day.isoformat()}</h1>",
    ]

    parts.append("<h2>Waiting on you</h2>")
    if not attention:
        parts.append("<p><em>nothing -- the machine needs no answers today</em></p>")
    else:
        parts.append("<ul>")
        for item in attention:
            flags = " ".join(f"<code>{escape(flag)}</code>" for flag in item.flags)
            link_open = f'<a href="{escape(item.url)}">' if item.url else ""
            link_close = "</a>" if item.url else ""
            parts.append(
                f"<li>{escape(item.kind)} #{item.number} {link_open}"
                f"{escape(item.title)}{link_close} &mdash; {flags}</li>"
            )
        parts.append("</ul>")
        parts.append(
            "<p><em>Answer in the item's comments; remove the label when done. "
            "A <code>proposed</code> item wants the <code>ready</code> label (or to be "
            "closed).</em></p>"
        )

    parts.append("<h2>Pull requests opened</h2>")
    if not opened:
        parts.append("<p><em>none</em></p>")
    else:
        parts.extend(_pr_list_html(opened))

    parts.append("<h2>Pull requests merged</h2>")
    if not merged:
        parts.append("<p><em>none</em></p>")
    else:
        parts.extend(_pr_list_html(merged))

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


def send_or_print(html_body: str, subject: str, print_only: bool) -> None:
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
    message["Subject"] = subject
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
    attention = read_attention(args.branch)
    opened = read_opened_prs(args.branch, day)
    merged = read_merged_prs(args.branch, day)
    commits = read_commits(args.branch, day)

    if not attention and not opened and not merged and not commits:
        print(f"nothing landed on {args.branch} on {day.isoformat()}, nothing waiting")
        return 0

    subject = f"Daily agent digest - {day.isoformat()}"
    if attention:
        subject += f" -- {len(attention)} item(s) waiting on you"

    body = render_html(args.branch, day, attention, opened, merged, commits)
    send_or_print(body, subject, args.print_only)
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
    # GitHub cron runs in UTC. 16:05 UTC = 18:05 Amsterdam in summer, 17:05 in winter;
    # swap 16 <-> 17 at the clock changes if the winter hour bothers you. Off the hour
    # on purpose: GitHub queues the hour itself heavily.
    - cron: '5 16 * * *'
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
  issues: read

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

(On a manually triggered run the "print only" box is ticked by default, so you can read
the rendered digest in the workflow's log before any mail ever goes out. The scheduled
18:05 run sends for real once the OWNER STEP 4 secrets exist -- and prints into the log
until then.)

---

## 12. Step 9 -- bring-up, in order

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

**C -- skills fetched and COMMITTED.** `bash .orca/setup_skills.sh`, then
`git add .claude/skills && git commit -m "Add agent skills" && git push`.
*Verify:* `git ls-files .claude/skills | head` prints paths (tracked, not just present).

**D -- roles, config, workflow committed.** `.orca/roles/planner.md`, `architect.md`
and `implementer.md`, `.orca/dispatch.yml`, the dispatcher directory, and
`.github/workflows/agent-pipeline.yml` + `daily-digest.yml` -- committed to `dev`
and pushed.
*Verify:* the repo's **Actions** tab on GitHub lists "Agent pipeline" (it appears once
the file is on a branch GitHub scans; it will show "no runs" until a PR exists).

**E -- GitHub settings.** Relay **OWNER STEPS 1-4** (sections 8 and 11) to the owner
verbatim -- Actions write permissions, the Claude secret, muting GitHub's per-event
e-mails, and the digest's SMTP secrets (the last is skippable) -- and set the `main`
ruleset (section 10).
*Verify:* Settings -> Actions -> General shows "Read and write permissions"; Settings ->
Secrets and variables -> Actions lists `ANTHROPIC_API_KEY` or `CLAUDE_CODE_OAUTH_TOKEN`.
There is no way to verify a secret's VALUE from outside; the first pipeline run is the
real test, which is why step G exists.

**F -- host checks and the scheduled task.**

```bash
python .orca/dispatcher/dispatch.py doctor --fix
```

*Verify:* doctor ends with `0 problem(s)` -- except the core-document gate, which stays
FAIL until step H merges. Everything else must be OK. Then:

```powershell
powershell -ExecutionPolicy Bypass -File .orca\dispatcher\install-task.ps1
```

*Verify:* `install-task.ps1 -Status` shows `state=Running`, and
`.orca/dispatcher/dispatcher.log` gains a line each minute.

**G -- pipeline smoke test.** Before any agent exists, prove the CI half end-to-end:

```bash
git checkout -b pipeline-smoke-test dev
echo "smoke test" >> docs/pipeline-smoke.md
git add docs/pipeline-smoke.md && git commit -m "Pipeline smoke test"
git push -u origin HEAD
gh pr create --base dev --title "Pipeline smoke test" --body "Docs-only; the Reviewer should merge this."
```

*Verify:* on the PR page, the "Agent pipeline" check appears; classify marks it
`state:tested` (docs-only); the Reviewer agent comments and **merges it** within a few
minutes. If the check never appears -> the workflow file is not on `dev` (step D). If it
fails with "Resource not accessible by integration" -> OWNER STEP 1. If the agent step
fails on credentials -> OWNER STEP 2. Do not proceed until this merges by itself.

**G2 -- digest smoke test.** On GitHub: **Actions** tab -> **Daily agent digest** ->
**Run workflow** (leave "print only" ticked) -> open the run and its log.
*Verify:* the log shows either the rendered digest for today or "nothing landed ...,
nothing waiting" -- anything but an error. If OWNER STEP 4 was done, run it once more
with "print only" unticked and confirm the mail arrives.

**H -- onboarding.**

```bash
python .orca/dispatcher/dispatch.py onboard
```

If `autonomy:` is not set in `dispatch.yml`, this first asks the owner the plain-language
question of section 14 and writes the answer. If you (the installing Claude) are running
this for the owner, ask THEM -- do not answer it yourself. Then an interactive Planner
worktree opens in Orca; the owner is interviewed in rounds; on agreement the Planner
opens a PR into `dev`; the PR is docs-only, so the pipeline merges it.
*Verify:* the gate line of `dispatch.py status` flips to `OPEN`.

**I -- issues flow.** The Planner derives specs and ADRs, then writes issues per its role
(`proposed` or `ready` per the autonomy mode). In `manual`/`propose` the owner promotes
by adding the `ready` label. From there the loop runs: headless Implementer run -> PR ->
CI verify -> CI review -> merge into `dev` -> issue closed -> dependencies release the
next phase.
*Verify:* `dispatch.py status` shows an issue go queued -> RUNNING -> in PR, and the PR's
Actions run go green, with no human action beyond promoting labels.

**J -- promotion.** The owner inspects `dev` and opens the pull request into `main`
themselves. No agent ever does.

**K -- completion and the next round.** When the pipeline drains (and autonomy is not
`manual`), the dispatcher spawns a headless Planner audit: it files what is missing
(`proposed`/`ready` per the dial) or opens a PR marking the core document
`Status: ACHIEVED -- <date>`. Once that merges, dispatching stops and the owner is paged.
`dispatch.py onboard` then runs a REVISION interview whose PR reopens the gate.
*Verify:* after the ACHIEVED merge, `dispatch.py status` says `project complete`; after a
revision round it flips back to `OPEN`.

### Checklist

```
[ ] main + dev exist on origin; ruleset on main: PR required, no force pushes
[ ] section-3 tree committed to dev: dispatch.yml, roles/, dispatcher/, workflows/, docs/
[ ] docs/CORE_DOCUMENT.md template on dev (Status: EMPTY until the interview)
[ ] .claude/skills committed (git ls-files .claude/skills is non-empty)
[ ] OWNER STEP 1 done: Actions -> Read and write permissions
[ ] OWNER STEP 2 done: ANTHROPIC_API_KEY or CLAUDE_CODE_OAUTH_TOKEN secret exists
[ ] OWNER STEP 3 done: GitHub's own e-mail notifications muted (the digest is the one mail)
[ ] OWNER STEP 4 done or deliberately skipped: SMTP secrets for the daily digest
[ ] dev container opens (VS Code: "Reopen in Container") and post-create finishes
[ ] digest smoke test rendered (step G2)
[ ] dispatch.py doctor --fix -> 0 problems (gate FAIL allowed pre-onboarding)
[ ] install-task.ps1 -> scheduled task Running; dispatcher.log ticking
[ ] smoke-test PR merged BY THE PIPELINE with no human click
[ ] dispatch.py onboard -> autonomy answered by the OWNER -> interview -> gate OPEN
[ ] issues flow: queued -> RUNNING -> PR -> green check -> merged -> closed, hands-free
[ ] on a drained pipeline: audit files proposed/ready issues, or ACHIEVED PR pages the owner
[ ] Owner has read section 18
```

---

## 13. The circuit breaker

`circuit_breaker.max_cycles: 3`. A cycle is spent each time a headless run is STARTED for
an issue -- first dispatch, the automatic retry after an empty exit, and every fix run
after a `state:blocked`. On the cycle after the third, the dispatcher does not start
another run: it labels the issue `escalated`, posts the reason, and pages the owner.

Do not raise the limit to force something through. An issue that has burned three runs is
saying the specification is wrong, and a fourth attempt at the wrong thing costs more
than asking. (Headless runs make this dial MORE important, not less: a run you are not
watching must have a hard budget. The wall-clock ceiling `max_run_minutes` is the same
idea on a different axis.)

---

## 14. The autonomy dial

One line in `.orca/dispatch.yml` decides who feeds the machine:

| `autonomy:` | Who writes issues | What the Planner may label | Backlog audits |
| --- | --- | --- | --- |
| `manual` | **you** | `proposed` only, and only during interviews you start | never |
| `propose` | the Planner suggests, **you approve** | `proposed` only | on drain |
| `auto` | the Planner | `ready` directly | on drain |

Three facts about the dial, because they are easy to get wrong:

1. **It is a repository setting, not a GitHub setting.** GitHub never decides autonomy;
   it only stores the labels. The dispatcher on your machine reads `dispatch.yml` and the
   Planner's briefs quote it. Changing the line changes the behaviour on the next tick --
   no reinstall, no redeploy, and different projects can sit at different settings.
2. **The dispatcher only ever dispatches `ready`.** In every mode. The dial only controls
   who is allowed to APPLY that label. That is why the whole mechanism is one line: the
   safety property ("nothing runs that a human did not release") is enforced in exactly
   one place, and `auto` mode is precisely the decision to delegate that release.
3. **If the line is missing, `dispatch.py onboard` asks the owner** in plain language
   (the text is in `dispatch.py`, `AUTONOMY_QUESTION`) and writes the answer into the
   file. A dispatcher running without the line behaves as `propose` and says so in its
   log -- the conservative default.

**Promoting a proposed issue (plain language, for the owner):** open the repository on
GitHub, click **Issues**. Suggestions from the Planner carry the yellow `proposed` label.
Open one; if you want it built, click the gear next to **Labels** on the right, tick
**ready**, and untick **proposed**. That is the entire approval. Every suggestion is
required (section 7) to explain itself in plain language -- what the problem is, why it
exists, what done looks like -- precisely so this decision can be made from the text
alone. If you cannot tell what an issue is about from its own text, do not promote it:
edit it until you can, or close it. Ignore or close the
suggestions you do not want -- nothing runs without that `ready` label. (On the Issues
list you can also filter by `label:proposed` to see all pending suggestions at once.)
The daily digest's "Waiting on you" section lists every open `proposed` item too, so in
`propose` mode approval can be a once-a-day habit rather than a vigil.

---

## 15. The FMEA protocol, now asynchronous

For **large or risky components** -- data fetching, storage, core frontend, anything
labelled `data`, `ui` or `pipeline` -- the Verifier stops before judging and produces a
failure matrix:

| Proposed Component | Failure Mode | Default Recovery Action | Human Confirmation Needed? |
| --- | --- | --- | --- |

In v0.1.1 the Tester posted this and **waited in its session** for the human. There is no
session to wait in any more, and that is an upgrade: the Verifier posts the table as a PR
comment, adds `needs-human` **and** `state:blocked`, and its run ends. The PR appears in
your daily digest under "Waiting on you" (and in GitHub's web/app notifications, if you
kept those on). You answer the flagged rows **as an
ordinary PR comment** -- from a phone, hours later, it does not matter -- and remove the
`needs-human` label. The dispatcher then sends a fresh fix run whose brief embeds the
whole conversation, so your answers arrive as requirements; it writes the confirmed
tests, pushes, clears `state:blocked`, and the pipeline re-runs.

The two standing warnings survive verbatim, now split across the roles: **a fake that
behaves better than the real thing proves nothing** (Verifier), and **a test can pin a
mistake** -- if the change is right and the old test is wrong, the test changes,
deliberately, in its own commit, with the reason stated (Implementer).

---

## 16. Evidence before merge

A circuit breaker catches thrashing. It does not catch the run that **reports success
while being wrong**. Green tests prove the code does what the tests assumed -- a
different claim from "the code is right". So for PRs labelled `data`, `ui` or `pipeline`,
the Reviewer requires (the rules live in `evidence_gates` in `dispatch.yml`, and the
Reviewer's prompt in the workflow embeds them): counts reconciled against an independent
source, real output sampled into the PR, cost measured against a documented budget. The
Implementer's role tells it to put that evidence in the PR body up front. The principle:
**an artefact describes itself by observation, not by declaration.**

---

## 17. Known pitfalls

The v0.1.1 entries that still apply, plus the new failure modes of headless runs and the
CI pipeline. The starred rows actually happened; the rest are the documented sharp edges
of the components v0.2 introduces.

| Trap | What is actually true |
| --- | --- |
| * Persistent sessions as workers | The v0.1.1 root failure: interactive TUIs kept alive as memory hoard CPU and orphan. A run is a process that EXITS; GitHub is the memory. Do not reintroduce "wait here" into any brief. |
| Headless run hangs forever | A `claude -p` run that hits a permission prompt has nobody to press "allow". `permission_args` (default `--dangerously-skip-permissions`) is load-bearing; if you tighten it, you own the hangs -- `max_run_minutes` is the backstop. |
| `claude` works in your terminal, not from the task | Scheduled tasks get a minimal PATH. Set `dispatcher.claude_cmd` to the absolute path from `where claude`. `doctor` checks it. |
| Killing the PID leaves children running | `claude` spawns tool subprocesses. Runs are started as their own group/tree and killed with `taskkill /T /F` (Windows) or `killpg` (POSIX) -- kill the TREE or you rebuild the orphan problem one layer down. |
| * Labels set by the Actions token trigger nothing | Events created with the built-in `GITHUB_TOKEN` do not start workflows -- a label-chained Verifier->Reviewer design dies silently. The pipeline chains jobs with `needs:` inside ONE workflow instead. If you ever split it, the label-setter needs a PAT or App token. |
| CI agent pushes do not re-trigger CI | Same rule, as a feature: if you ever let a CI agent push commits, that push will not restart the pipeline. In this design only the HOST pushes (implementer/fix runs, with your `gh` login), so every push triggers. |
| "Resource not accessible by integration" | OWNER STEP 1 was skipped: Actions still has read-only workflow permissions. |
| Pipeline runs but the agent step fails immediately | OWNER STEP 2: the secret is missing, misnamed (exact names matter), or you set both and the action rejects an empty one -- delete the unused line in the workflow. |
| Action inputs drift | `anthropics/claude-code-action` evolves. On an "unknown input" error, read the action's README and adjust the `with:` block; the job structure and prompts are the stable part. |
| Secrets on forked PRs | Fork PRs do not receive repository secrets, by design. This workflow only uses same-repo branches, so it never bites -- until someone "helpfully" opens a fork PR; it will sit unverified. |
| Superseded pushes stack up paid runs | The `concurrency` block cancels the previous run per PR. Do not remove it to "see full history"; it is the cost brake. |
| * "Closes #N" into `dev` | Only auto-closes on the default branch. The dispatcher closes issues on merge itself; without that, `Depends on:` phases never release. |
| * Parallel agents numbering ADRs | Three agents all wrote `ADR-002-*`. ADRs are numbered by ISSUE: `docs/adrs/ADR-<issue#>-<slug>.md`. |
| * Orca renames branches | `--name issue-4` can become branch `<gituser>/issue-4`. Match branches by regex `issue-(\d+)`, never by exact name. |
| Orca insists on an agent at worktree create | v0.2 creates worktrees with NO agent (headless runs are plain subprocesses). If your Orca version requires one, check `orca worktree create --help` for a skip flag; worst case create with an agent and close the spawned terminal -- the dispatcher only needs the directory. |
| Skills present but not committed | `.claude/skills/` on disk is enough for host runs and INVISIBLE to CI. `doctor` checks `git ls-files`; commit them. |
| * Skills installed in the wrong scope | The whole v0.1.1 host-vs-container confusion. v0.2 has one scope: the repository. Keep it that way. |
| * Long `--prompt` strings | Truncated in the CLI path. Briefs are files; the `-p` argument is one line pointing at the file. |
| * Role prompts copied per project drift | Still true BETWEEN projects -- which is why sharing is now done with a template repo and a reviewable copy, not a shared mutable host directory. Inside a project, in-repo is the point: CI needs them. |
| * Dispatcher inside a chat session | Dies with the session. It is a scheduled task. |
| * Two dispatchers at once | `state.json` is unlocked between processes; the lock file refuses the second. |
| * `just-scrape` without an API key | Hangs on an interactive prompt -- in a headless run, invisibly, until `max_run_minutes` kills it. The Planner's research duties say: use it when configured, otherwise fetch directly. |
| * CRLF in shell scripts from Windows | Reaches bash (container AND Actions runners) and fails as `set: pipefail: invalid option name`. `.gitattributes` pins `*.sh` to LF. |
| * `npm ci` over a bind mount | Replaces the host's `node_modules` with Linux binaries. Shadow it with a volume (section 5). |
| * GitHub e-mails you about everything | Not this workflow's mail -- GitHub's own notifications for a watched repository. OWNER STEP 3 turns that channel off; the signals still reach you in the daily digest. |
| The digest drifts an hour twice a year | GitHub cron is UTC and Amsterdam is not, always. `5 16 * * *` = 18:05 Amsterdam in summer, 17:05 in winter; swap 16 <-> 17 at the clock change if you care. |
| Issues written in shorthand | The reader is always a fresh agent, or you on a phone days later. Since v0.2.1 every dispatched run STOPS and asks (`needs-human`) when an issue's text does not carry its own context -- so a jargon issue costs a round-trip through you instead of a wrong build. The standard is in section 7. |
| A `proposed` issue sits forever | Working as designed: `propose` mode does not nag. If the pipeline seems idle, check `label:proposed` on the Issues tab -- the machine is waiting for you. |

---

## 18. The owner's runbook

*Hand this section to the human. Everything above it is for whoever builds the setup. No
programming knowledge is assumed.*

**What runs by itself.** A small program on your computer (a "scheduled task" named
`OrcaDispatcher-<project>`) checks GitHub every minute. When there is approved work, it
starts a short-lived AI worker for one task at a time each; the worker writes the code
and its tests, publishes a "pull request" (a proposed change) on GitHub, and shuts
itself down. GitHub then runs two more AI checks in the cloud -- a Verifier that tests
the change and a Reviewer that judges it -- and if both are satisfied, the change is
merged into the project's working branch, called `dev`. Your computer being on and the
Orca app being open is all it needs; reboots are fine. Once a day, around six in the
evening, one e-mail summarises it all: what landed, what was opened, and what is waiting
on you. That is the only e-mail this system sends.

**Your touchpoints -- there are six, and five of them are just "answer on GitHub":**

1. **The interview** (once per project, and again whenever you want a new round). A tab
   opens in Orca and interviews you about what to build. Answer, correct it, and finally
   say it is agreed. This is the only place you talk to an AI live.
2. **Approving suggested work** (unless you chose `auto`). The Planner's suggestions
   appear on your GitHub **Issues** page with a yellow `proposed` label. To approve one:
   open it, click the gear next to **Labels**, tick **ready**, untick **proposed**.
   Nothing is ever built without your `ready`. Each suggestion must explain itself in
   plain words -- the problem, why it matters, what done looks like. If one does not
   make sense from its own text, that is the suggestion's failure, not yours: edit the
   text until it does, or close it. Never approve a task you do not understand.
3. **Safety questions (FMEA).** For risky pieces, the Verifier posts a table of "what
   could go wrong / what should happen then" on the pull request. It will be in your
   daily e-mail under "Waiting on you". Write your answers as a normal comment, then remove the `needs-human` label (gear next to
   Labels on the right). Your answers become requirements.
4. **`needs-human` questions.** Any worker that needs a decision or a credential asks in
   the comments of its issue or pull request and flags it. It appears in your daily
   e-mail under "Waiting on you". Answer in the comments, remove the `needs-human` label, and a fresh worker picks it up with
   your answer in hand.
5. **Escalations.** A red `escalated` label means the same task failed three times --
   the machine is telling you the task description is wrong, not trying a fourth time.
   Fix the issue's text, remove `escalated`, and it will be retried.
6. **Publishing.** When you are happy with `dev`, open the pull request from `dev` to
   `main` on GitHub and merge it yourself. No AI can ever do this; a repository rule
   enforces it.

**Writing a task yourself.** In `manual` mode -- or any time you add work -- write the
task as if for a bright newcomer: a few plain sentences on what the problem is and why
it exists, what you want done, and how you will know it worked. Spell out abbreviations.
The worker that picks it up is always brand new, remembers nothing of your other
conversations, and is instructed to stop and ask rather than guess when the text leaves
gaps -- so the clearer you write, the less often you are asked. Then add the `ready`
label.

**Your one e-mail.** Around 18:00 (Amsterdam) each day, when there is anything to say,
you get a single digest: a "Waiting on you" list (questions, escalations and suggestions
-- these repeat daily until you act on them), the pull requests opened and merged, and
the day's commits. If a whole day is too slow for you, two opt-ins that stay mail-free:
install the GitHub app on your phone and allow its push notifications, or put your
handle in the `github_mention` line of `.orca/dispatch.yml` to be @-mentioned the
instant something needs you.

**Reading the board.** In a terminal, from the project folder:
`python .orca\dispatcher\dispatch.py status` -- every issue (queued / running / in a pull
request), every pull request and its stage, and the autonomy setting. On GitHub, the
**Actions** tab shows the cloud checks: a green tick means verified and reviewed; a red
cross means it was sent back and a fix worker is already on it.

**The brake.** Double-click `dispatcher-pause.cmd` (in `.orca\dispatcher\`; pin a
shortcut to your taskbar if you like). Nothing new starts; work already moving finishes
and lands. `dispatcher-resume.cmd` lifts it. Use it when you want to stop the spend, or
to work in the project yourself without workers moving underneath you.

**Changing how autonomous it is.** Open `.orca/dispatch.yml` in any text editor and set
the `autonomy:` line to `manual` (you write all tasks), `propose` (it suggests, you
approve), or `auto` (it feeds itself). Takes effect within a minute. Each of your
projects can be set differently.

**Costs, in one paragraph.** Workers on your computer use your Claude subscription
through the Claude app you already have. The two cloud checks use the credential you
stored in OWNER STEP 2: if that was a subscription token, they draw on your
subscription; if it was an API key, you pay per use -- roughly one Verifier plus one
Reviewer run per change, and superseded runs are cancelled automatically. The circuit
breaker (three tries per task) and the 30-minute cap per worker are the spending limits.

**If something looks stuck:** `dispatch.py status` first. An issue "queued" is usually
waiting on a dependency, a free slot, or your `ready` label. A pull request sitting
still: open it on GitHub -- if it has `needs-human`, it is waiting for YOU (point 4). If
the Actions tab shows nothing ever ran, the two OWNER STEPS in section 7 were probably
skipped. Task not running? `install-task.ps1 -Status`, then re-run the installer. Orca
closed? Open it; nothing is lost. Worst case: delete `.orca/dispatcher/state.json` --
the truth lives on GitHub; you get at most a duplicate comment. And your daily
e-mail's "Waiting on you" list is the same information, delivered.

