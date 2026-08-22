# The ORCA ADE agent workflow: complete setup

How to build this environment from an empty repository. Every file is listed in full, so the
whole workflow is constructable from this document alone.

Project-agnostic. Substitute your own names wherever a placeholder appears; nothing here is
tied to a particular codebase.

---

## Contents

1. [What you are building](#1-what-you-are-building)
2. [Prerequisites](#2-prerequisites)
3. [Repository layout](#3-repository-layout)
4. [Step 1 — The core document](#4-step-1--the-core-document)
5. [Step 2 — The container](#5-step-2--the-container)
6. [Step 3 — Skills](#6-step-3--skills)
7. [Step 4 — The agents](#7-step-4--the-agents)
8. [Step 5 — Routing and dispatch](#8-step-5--routing-and-dispatch)
9. [Step 6 — Running the workflow, A to G](#9-step-6--running-the-workflow-a-to-g)
10. [The circuit breaker](#10-the-circuit-breaker)
11. [The FMEA protocol](#11-the-fmea-protocol)
12. [Evidence before merge](#12-evidence-before-merge)
13. [The daily digest](#13-the-daily-digest)
14. [Branch protection](#14-branch-protection)
15. [Known tooling pitfalls](#15-known-tooling-pitfalls)
16. [Checklist](#16-checklist)

---

## 1. What you are building

```
                        ┌───────────────────────────┐
                        │      HUMAN GATEKEEPER     │
                        │  the only one who may     │
                        │  merge  dev  ->  main     │
                        └─────────────┬─────────────┘
                                      │
                                      ▼
                        ┌───────────────────────────┐
                        │      DISPATCHER AGENT     │
                        │  worktrees · skill        │
                        │  routing · circuit breaker│
                        └─────────────┬─────────────┘
                                      │
      ┌──────────────┬────────────────┼────────────────┬──────────────┐
      ▼              ▼                ▼                ▼              ▼
┌───────────┐ ┌───────────┐   ┌───────────┐   ┌───────────┐   ┌───────────┐
│    PO &   │ │ Architect │   │Researcher │   │ Developer │   │  Testing  │
│  Analyst  │ │           │   │           │   │           │   │           │
└───────────┘ └───────────┘   └───────────┘   └───────────┘   └─────┬─────┘
                                                                    │
                                                              ┌─────▼─────┐
                                                              │ Reviewer  │
                                                              │ merges to │
                                                              │    dev    │
                                                              └───────────┘
```

**Where each part runs:**

```
HOST                                    CONTAINER
  Orca (desktop application)     ──▶      repo + pinned toolchain + skills
  dispatches agents                       tests, builds, project commands
  owns the git worktrees                  large read-only data mounted in
```

Orca runs on the host. It is a desktop application, not an npm package, and it cannot live
inside the environment it dispatches into. The container is the reproducible place where
agents *execute*: a pinned toolchain, so a test that passes for one agent passes for every
agent and later on the deployment target.

### Isolation: decide which one you want

Two different things get called isolation, and conflating them is how a setup looks safe
without being safe.

| You want | Do this | Cost |
| --- | --- | --- |
| **Reproducibility** — same toolchain everywhere, and a path to deployment | A plain dev container. No `docker.sock`. | None |
| **Containment** — the agent genuinely cannot reach the host | Rootless Docker, sysbox, or a throwaway VM | Real setup work |

Mounting `/var/run/docker.sock` gives anything inside the container root-equivalent control of
the host daemon — enough to start a privileged container that mounts the host filesystem. It
is not isolation. Mount it only if you need container-in-container testing, and record that
you made the trade.

### The Dispatcher is a process, not a prompt

The Dispatcher is often mistaken for a chat agent because its policy sits next to the six
real agent prompts at `~/.orca/roles/dispatcher.md` (the shared, host-level roles directory
every project uses — see section 7). **It is not.** It is a running poll-and-dispatch
process on the host: `.orca/dispatcher/dispatch.py`. The prompt file records the *policy*;
the script *is* the runtime.

An LLM in an infinite poll loop is wasteful and unreliable — that is why the runtime is
deterministic Python. Deploying the workflow without starting `dispatch.py` leaves you with
a Dispatcher that exists only on paper: issues sit unrouted, the pipeline stalls silently,
and nothing tells you why.

Orca is a local desktop application. GitHub webhooks have nowhere on your machine to land.
**The Dispatcher polls, it does not receive events.** Any doc, prompt or diagram that says
"a webhook notifies the Dispatcher" is describing intent, not mechanism.

---

## 2. Prerequisites

| Tool | Why | Check |
| --- | --- | --- |
| Docker Desktop | Builds and runs the dev container | `docker info` |
| VS Code + Dev Containers extension | Opens the repo inside it | — |
| Orca | Dispatches agents, owns worktrees | `orca --version` |
| GitHub CLI, authenticated | The digest reads merged pull requests | `gh auth status` |
| Node ≥ 22.20 on the host | The `skills` CLI floor | `node --version` |

---

## 3. Repository layout

```
.devcontainer/
  Dockerfile              pinned toolchain
  devcontainer.json       mounts, environment, post-create hook
  post-create.sh          dependencies + skills, on first open
.orca/
  dispatch.yml            labels -> roles -> skills; pipelines; circuit breaker
  setup_skills.sh         fetches every skill
  dispatcher/
    dispatch.py           the poller: gh issue list -> orca worktree create
    README.md             how to run, when it retries, how to reset state
    state.json            (gitignored) what has been dispatched, cycle counts
docs/
  CORE_DOCUMENT.md        the single source of truth; everything derives from it
  specs/                  derived specifications
  adrs/                   one decision per file
mail/
  daily_digest.py         digest, derived from git and pull requests
.github/workflows/
  daily-digest.yml        sends it, and only when work landed
.gitattributes            *.sh pinned to LF
```

The seven role prompts (`dispatcher.md`, `po-analyst.md`, `architect.md`, `researcher.md`,
`developer.md`, `tester.md`, `reviewer.md`) are **not** part of this per-project layout.
They live at **`~/.orca/roles/`** on the host and are shared across every project this
workflow drives. Section 7 has the seed content — you populate `~/.orca/roles/` **once per
machine**, not once per repo.

Create it:

```bash
mkdir -p .devcontainer .orca/dispatcher docs/adrs docs/specs mail .github/workflows
```

---

## 4. Step 1 — The core document

**Every project starts from a core document, and nothing else exists until it does.** No
architecture, no issues, no code. It is the single source of truth for what the project is,
and every later artefact — specs, ADRs, issues, tests — is derived from it.

The PO & Analyst Agent owns it and populates it by **deep interview with the human**. It is
never inferred, never filled with a plausible default, and never written from the agent's own
idea of what the project probably wants.

Create the file before you start:

```bash
cat > docs/CORE_DOCUMENT.md <<'EOF'
# Core document

> Populated by deep interview with the project owner. Nothing here is inferred.
> Status: EMPTY — run the onboarding interview.

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
EOF
```

The interview protocol is in the PO & Analyst system prompt in
[section 7](#7-step-4--the-agents). Two rules govern it:

- **Interview in rounds.** Ask, record the answer, read it back, ask what is now wrong or
  missing. A single pass produces a document the owner does not recognise.
- **An unknown is recorded as OPEN, never guessed.** An open question written down is a
  decision waiting. A guess written down is a defect shipped, and every later artefact
  inherits it.

The core document is living. When a decision changes it, it is updated and the change is
stated. A spec may never silently contradict it.

---

## 5. Step 2 — The container

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

Substitute `PROJECT_DATA_HOST_DIR` and the target path for your project's own large dataset,
or delete the `mounts` block entirely if it has none.

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

Adapt the two dependency lines to your project's own layout.

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

### `.gitattributes`

Required if anyone works on Windows:

```gitattributes
* text=auto eol=lf

# Executed inside the Linux dev container, which bind-mounts the working tree rather than a
# fresh checkout. A CRLF here is invisible on Windows and fails in the container with
# `set: pipefail: invalid option name`, which points nowhere near the real cause.
*.sh text eol=lf
```

### Open it

```bash
export PROJECT_DATA_HOST_DIR=/path/to/your/data    # omit if you have none
code .                                              # -> "Reopen in Container"
```

Confirm inside:

```bash
python --version && node --version    # node must be >= 22.20.0
npx skills list
```

---

## 6. Step 3 — Skills

The CLI is [`skills`](https://github.com/vercel-labs/skills) (npm package: `skills`).

**Three facts that differ from most written instructions:**

- The verb is **`add`**, not `install`.
- There is **no `--target`**. Scope is project by default, or `--global`.
- The agent id is **`claude-code`**. Plain `claude` is rejected as invalid, which fails every
  install at once.

```bash
npx -y skills@latest add <owner>/<repo> --skill <skill-name> --agent claude-code --yes
```

**Read skill names back from the repository before committing them.** Names in a plan are
frequently wrong, and a wrong name fails the container build for everyone:

```bash
npx -y skills@latest add <owner>/<repo> --list
```

### The skill matrix

| Agent | Skills | Repository |
| --- | --- | --- |
| Every agent | `handoff` | `mattpocock/skills` |
| PO & Analyst | `grill-with-docs`, `domain-modeling` | `mattpocock/skills` |
| Architect | `improve-codebase-architecture`, `codebase-design` | `mattpocock/skills` |
| Researcher | `just-scrape` | `scrapegraphai/just-scrape` |
| Researcher | `diagnosing-bugs` | `mattpocock/skills` |
| Developer — label `ui` | `frontend-design` | `anthropics/skills` |
| Developer — label `ui` | `web-design-guidelines` | `vercel-labs/agent-skills` |
| Developer — label `ui` | `high-end-visual-design` | `leonxlnx/taste-skill` |
| Developer — label `seo` | `seo-audit` | `coreyhaines31/marketingskills` |
| Developer — label `scraper` | `just-scrape` | `scrapegraphai/just-scrape` |
| Developer — label `bug` | `diagnosing-bugs` | `mattpocock/skills` |
| Testing | `tdd` | `mattpocock/skills` |
| Testing | `webapp-testing` | `anthropics/skills` |
| Reviewer | `code-review` | `mattpocock/skills` |

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

failed=()

# add <owner/repo> <skill> [<skill>...]
add() {
  local repo="$1"; shift
  local skill
  for skill in "$@"; do
    printf '  %-34s %s\n' "$skill" "($repo)"
    # shellcheck disable=SC2086
    if ! $SKILLS_CLI add "$repo" --skill "$skill" --agent "$AGENT" --yes >/dev/null 2>&1; then
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

```bash
chmod +x .orca/setup_skills.sh .devcontainer/post-create.sh
```

---

## 7. Step 4 — The agents

**The role prompts are HOST-LEVEL and shared across every project this workflow drives.**
They live at `~/.orca/roles/`, not in any repo. Populate the directory **once per machine**
from the seven reference blocks below; do not paste them into the project you are setting
up. A per-project copy is exactly the failure mode that produces drift, and the reason
this section was rewritten.

Six agents plus one policy file. `dispatcher.md` is a POLICY document, not a system prompt
— it records the rules the Dispatcher process (`.orca/dispatcher/dispatch.py`, section 8)
enforces. Editing one without the other guarantees drift.

| Role | Host path | Owns | May merge |
| --- | --- | --- | --- |
| Dispatcher (policy, not an agent) | `~/.orca/roles/dispatcher.md` | Runtime policy (enforced by `.orca/dispatcher/dispatch.py`) | nothing |
| PO & Analyst | `~/.orca/roles/po-analyst.md` | `docs/CORE_DOCUMENT.md`, specs, ADRs, issues | nothing |
| Architect | `~/.orca/roles/architect.md` | Contracts, schemas, module boundaries | nothing |
| Researcher | `~/.orca/roles/researcher.md` | Facts about external systems, bug localisation | nothing |
| Developer | `~/.orca/roles/developer.md` | Implementation inside one worktree | nothing |
| Testing | `~/.orca/roles/tester.md` | FMEA, test suites, test execution | nothing |
| Reviewer | `~/.orca/roles/reviewer.md` | Standards, spec compliance | **`dev` only** |

Nobody merges to `main`. That is the human's, always.

Seed the host directory on a fresh machine:

```bash
mkdir -p ~/.orca/roles
# then write each of the seven blocks below to the path in its subheading.
# The Orca desktop GUI picks these up automatically; the dispatcher process
# (.orca/dispatcher/dispatch.py) reads them via `Path.home() / ".orca" / "roles"`.
```

### A. Dispatcher — `~/.orca/roles/dispatcher.md`

```markdown
# Dispatcher policy

**You are reading a POLICY document, not an agent system prompt.** The Dispatcher is a
running Python process on the host: every project that follows this workflow ships one at
`.orca/dispatcher/dispatch.py`. This file records the policy that process enforces. When
this file changes, the script must change with it, and vice versa.

This file lives at `~/.orca/roles/dispatcher.md` — the host-level, shared roles directory
every project uses. Editing it changes the policy for all projects. That is intended.

If you are an LLM and someone has handed you this file expecting you to *be* the
Dispatcher — stop. Say so, and point them at the project's `.orca/dispatcher/dispatch.py`.
An LLM in an infinite poll loop is wasteful and unreliable, which is exactly why the
runtime is deterministic code.

## Trigger

The Dispatcher watches open GitHub issues labelled `ready`. `ready` means the PO &
Analyst (or a human) has approved the issue for implementation. Issues without `ready`
are ignored.

## Worktree creation

For each ready issue not already in `.orca/dispatcher/state.json` and not carrying the
`escalated` label:

1. Create an Orca worktree off `dev` named `feature/issue-<id>`.
2. Start a Claude agent in that worktree with the Developer role prompt
   (`~/.orca/roles/developer.md`), the earned skills, and a DISPATCH CONTEXT block
   containing the issue body, labels, pipeline choice and cycle count.
3. Post a comment on the issue: `Dispatched to worktree feature/issue-<id> (pipeline: …,
   cycle N/3)`.

## Pipeline choice

- `trivial` if the issue carries the `trivial` label AND no label in
  `pipelines.trivial.forbid_labels` (`ui`, `scraper`, `data`).
- `default` otherwise.

The Reviewer refuses the `trivial` label when the change turns out to touch behaviour —
the label is a request, not a permission.

## Skill routing

Baseline skills for the Developer come from `roles.developer.skills` in the project's
`.orca/dispatch.yml`. Labels add to that set:

| Label | Skills earned |
| --- | --- |
| `ui` | `frontend-design`, `web-design-guidelines`, `high-end-visual-design` |
| `seo` | `seo-audit` |
| `scraper` | `just-scrape` |
| `bug` | `diagnosing-bugs` |
| `data` | `codebase-design` |

## Circuit breaker

`circuit_breaker.max_cycles = 3` in `.orca/dispatch.yml`.

- The Dispatcher increments an issue's cycle count each time it opens a worktree.
- On the fourth request (cycle > 3) the Dispatcher does not create a worktree. Instead:
  1. Posts `Circuit breaker tripped: 3 failed attempts.` on the issue.
  2. Adds the `escalated` label.
  3. Stops touching that issue. A human resolves.

Do not raise `max_cycles`. An issue that has failed three times is saying the
specification is wrong, and a fourth attempt at the wrong thing costs more than asking.

## Merge control

The Dispatcher is **STRICTLY FORBIDDEN from merging any branch into `main`**. It does
not merge into `dev` either — that is the Reviewer's role. Merging `dev` into `main` is
the human's alone.
```

### B. PO & Analyst — `~/.orca/roles/po-analyst.md`

```markdown
You are the PO & Analyst Agent.
RESPONSIBILITIES:
1. Own `/docs/CORE_DOCUMENT.md`. Every project starts from it and every later artefact is
   derived from it. It is the single source of truth for what this project is.
2. Populate it by DEEP INTERVIEW with the human. Never infer, never assume, never fill a gap
   with a plausible default.
3. Derive `/docs/specs/*.md` and `/docs/adrs/ADR-NNN-*.md` from the core document once it is
   agreed.
4. Write GitHub Issues carrying role and skill labels.

INTERVIEW PROTOCOL:
- The core document comes FIRST. No architecture, no issues and no code exist until it is
  agreed with the human.
- Interview in rounds. Each round: ask, record the answer in the core document, read it back,
  ask what is now wrong or missing.
- Interrogate rather than collect. The first answer is usually the answer to a question you
  did not ask. Push until a boundary is sharp enough that a developer could not build the
  wrong thing without noticing.
- Cover at minimum: purpose and success criteria; target users; scope and explicit
  NON-scope; the domain model and its vocabulary; data sources and their constraints;
  external systems; legal, privacy and compliance limits; what must never happen.
- Where the human does not know, record it as OPEN with their name against it. An open
  question written down is a decision waiting; a guess written down is a defect shipped.
- Use `grill-with-docs` for the interview and `domain-modeling` to fix the vocabulary. One
  name per concept, written down, used everywhere afterwards.

OUTPUT RULES:
- `/docs/CORE_DOCUMENT.md` is living. When a decision changes it, update it and say what
  changed; never let a spec contradict it silently.
- An ADR records ONE decision, with the alternatives rejected and why. A decision without its
  discarded options is an assertion, not a record.
- An Issue states what is wanted, how anyone will know it worked, and what is out of scope.

FORBIDDEN:
- You do not write implementation code.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

### C. Architect — `~/.orca/roles/architect.md`

```markdown
You are the Architect Agent.
RESPONSIBILITIES:
1. Read `/docs/CORE_DOCUMENT.md` and the specs derived from it.
2. Produce and FREEZE the system contracts on `dev` before any feature work opens: interface
   types, schemas, API shapes, module boundaries.
3. Record each boundary decision as an ADR in `/docs/adrs/`.
4. Define the repository layout and the seams new work must fit into.

DESIGN RULES:
- Prefer a deep module with a narrow interface to a shallow one with a wide interface. A seam
  that exposes as much as it hides has cost a name and bought nothing.
- Contracts are frozen before feature worktrees open against them. A contract that changes
  while three worktrees depend on it is not a contract.
- If a frozen contract turns out to be wrong, STOP the work that depends on it, say so, and
  change it deliberately. Never let it drift.
- Never widen an interface to unblock one caller. That is how an interface becomes the union
  of every caller's convenience.
- Use `improve-codebase-architecture` and `codebase-design`.

FORBIDDEN:
- You do not implement features.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

### D. Researcher — `~/.orca/roles/researcher.md`

```markdown
You are the Researcher Agent.
RESPONSIBILITIES:
1. Answer questions of fact so that no other agent has to guess.
2. Establish what an external source, endpoint or page ACTUALLY returns, using `just-scrape`.
3. Reproduce and localise failures with `diagnosing-bugs` before anyone proposes a fix.
4. Write findings where the agent that needs them will find them, and state how you know.

EVIDENCE RULES:
- MEASURE, DO NOT INFER. A claim about an external system is worth exactly what its
  observation is worth. "The documentation says" is not an observation.
- Quote the request you sent and the response you got. A finding without its evidence is a
  rumour with a citation.
- Label a hypothesis as a hypothesis. If you have not seen it, you do not know it.
- Report an honest UNKNOWN rather than a confident guess. The next agent builds on what you
  write, and a wrong certainty propagates further than a stated gap.

FORBIDDEN:
- You do not change production code.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

### E. Developer — `~/.orca/roles/developer.md`

```markdown
You are the Developer Agent.
RESPONSIBILITIES:
1. Implement ONE issue inside the Orca worktree assigned to you, `feature/issue-<id>`.
2. Work only inside the dev container.
3. Read `/docs/CORE_DOCUMENT.md`, the relevant spec and the ADRs before writing anything.
4. Hand the worktree to the Testing Agent when the issue is implemented.

CODE STANDARD:
- Easily readable, minimalist code. No unnecessary abstraction, no premature generalisation,
  no design pattern the problem did not ask for. The Reviewer will reject all three.
- Match the surrounding code: its naming, its idioms, its comment density.
- Comment WHY, never what. Complete docstrings on every public function, class and module.
- The smallest change that satisfies the issue. Anything more is scope you were not given.

SKILL RULES:
- The Dispatcher loads your skills from the issue labels (`ui`, `seo`, `scraper`, `bug`,
  `data`).
- IF the work needs a skill you were not given: STOP and say so. Do not improvise around a
  missing skill — the label is wrong and a human must fix it.

REPORTING RULES:
- State what you changed, why, and WHAT YOU MEASURED. For anything touching data or a
  pipeline that means a number, not an adjective.
- If you found a defect you did not fix, write it down. A known defect recorded is a
  decision; one left unsaid is a trap for the next agent.
- If the issue contradicts the core document or a spec, that is a FINDING. Report it; do not
  route around it.

FORBIDDEN:
- Never merge your own work. The Reviewer merges into `dev`.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

### F. Testing — `~/.orca/roles/tester.md`

```markdown
You are the Testing Agent.
RESPONSIBILITIES:
1. Validate all code inside the assigned Orca worktree.
2. Execute tests strictly inside isolated sandbox containers.

HYBRID TESTING STRATEGY:
- STANDARD CODE (Pure functions, utilities, parsing): Write automated tests directly based on specs.
- LARGE COMPONENTS (Data Fetching, Storage, Core Frontend):
  1. STOP before writing test suites.
  2. Generate a FMEA (Failure Mode and Effects Analysis) Markdown table covering:
     | Proposed Component | Failure Mode | Default Recovery Action | Human Confirmation Needed? |
  3. INTERVIEW THE HUMAN: Present this table to the user in chat. Ask for missing edge cases or custom failure actions.
  4. Once confirmed by the user, write the full test suite.
```

### G. Reviewer — `~/.orca/roles/reviewer.md`

```markdown
You are the Reviewer Agent.
RESPONSIBILITIES:
1. Review PRs merging feature worktrees into `dev`.
2. Ensure code strictly fulfills the specification in `/docs/specs/`.
3. Enforce Code Standard: "Easily readable, minimalist code." Reject unnecessary abstractions, premature generalizations, or overly complex design patterns.
4. Verify all automated tests pass.

MERGE CONTROL:
- You are PERMITTED to merge approved worktrees into `dev`.
- You are STRICTLY FORBIDDEN from merging any branch into `main`.
```

---

## 8. Step 5 — Routing and dispatch

### `.orca/dispatch.yml`

```yaml
version: 1

branches:
  base: dev
  protected: [main]          # no agent may merge into these
  worktree_prefix: feature/

# Role prompts are HOST-LEVEL at ~/.orca/roles/ and shared across every project.
# The dispatcher process reads them from there; nothing here names a repo-relative path.
roles:
  po-analyst:
    skills: [handoff, grill-with-docs, domain-modeling]
  architect:
    skills: [handoff, improve-codebase-architecture, codebase-design]
  researcher:
    skills: [handoff, just-scrape, diagnosing-bugs]
  developer:
    skills: [handoff]        # baseline; labels add to it
  tester:
    skills: [handoff, tdd, webapp-testing]
  reviewer:
    skills: [handoff, code-review]

# A label is a claim about the work. It earns the skills that work needs and no others:
# loading the whole design stack for a typo is how small work becomes too expensive to do.
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

  # `trivial` skips the TESTER, never the reviewer. For a typo, a comment, a version bump or
  # a doc edit a test author has nothing to write, and every line is still read before it
  # lands. The REVIEWER refuses the label when the change turns out to touch behaviour: it is
  # a request, not a permission.
  trivial:
    stages: [developer, reviewer]
    forbid_labels: [ui, scraper, data]

circuit_breaker:
  max_cycles: 3
  on_trip:
    freeze_worktree: true
    halt_automation: true
    assign_to: human
    attach: [diff, test_output, agent_transcript_tail]

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

### Create the labels

```bash
gh label create ui              --description "Interface work; loads the design skills"
gh label create seo             --description "Search work; loads seo-audit"
gh label create scraper         --description "External data acquisition; loads just-scrape"
gh label create bug             --description "Defect; loads diagnosing-bugs"
gh label create data            --description "Data or pipeline; evidence gate applies"
gh label create trivial         --description "Skips the tester, never the reviewer"
gh label create ready           --description "Approved for dispatch; the poller picks it up"
gh label create escalated       --description "Circuit breaker tripped; the poller stops touching this issue"
gh label create role:developer  --description "Implementation work; loads ~/.orca/roles/developer.md"
gh label create role:architect  --description "Design and contracts; loads ~/.orca/roles/architect.md"
gh label create role:researcher --description "Facts about external systems; loads ~/.orca/roles/researcher.md"
gh label create state:tested    --description "Tester passed; the dispatcher spawns Reviewer next"
gh label create state:blocked   --description "Tester or Reviewer needs human help; the dispatcher stops touching the PR"
```

### The dispatcher process

Routing is not automation. `dispatch.yml` names which labels earn which skills; the
Dispatcher process is what actually reads open issues and creates worktrees. Skip this
step and nothing runs, however complete the rest of the setup looks.

**Prerequisites on the host** (not inside the container — the poller calls `orca`, a host
desktop app):

- Python 3.9+ and PyYAML.
- `gh` authenticated for this repo (`gh auth status`).
- `orca` on PATH; Orca open (`orca status` reports `runtimeReachable: true`).
- The `ready` and `escalated` labels from the block above.

**What the poller does, in order:**

1. Calls `gh issue list --state open --label ready --json …` every `--interval` seconds
   (default 60).
2. Filters out issues already in `state.json` and issues carrying the `escalated` label.
3. **Reads the `role:*` label to pick the role.** Every ready issue must carry
   **exactly one** `role:*` label (`role:developer`, `role:architect`,
   `role:researcher`, etc.). Zero or multiple `role:*` labels → the poller comments
   `Cannot dispatch: no role:* label found` (or `multiple role:* labels found`), records
   `role_missing` in `state.json`, and skips the issue. When the labels change the
   comment does not re-post; the issue is re-evaluated.
4. Loads `~/.orca/roles/<role>.md` as the system prompt. Missing → comments once and
   skips.
5. Computes the pipeline: `role:developer` gets `default` or `trivial` (per the
   `trivial` label and `pipelines.trivial.forbid_labels`). Every other role is a
   single-agent dispatch named after the role.
6. Computes the skills: baseline for that role from `roles.<role>.skills`, plus any
   earned by the issue's other labels per `labels.*.skills`.
7. `orca worktree create --repo path:<repo> --name feature/issue-<n> --issue <n>
   --base-branch dev --agent claude --prompt "<role prompt + DISPATCH CONTEXT + issue body>"`.
8. Records the dispatch in `state.json` and comments on the issue with the worktree
   name, chosen role, pipeline and cycle count.

**PR pipeline (Tester and Reviewer follow-up)**

A second poll pass on the same tick walks every open PR whose base is `dev`, driving
Tester → Reviewer → merge automatically. This is standard — not a manual step.

1. **New PR seen** on a `feature/issue-<n>` branch that has an existing Orca worktree:
   spawn a Claude session in that worktree as the Tester
   (`orca terminal create --worktree branch:<name> --command "claude"`, then
   `orca terminal send` the tester prompt). The Tester runs the project's test suite.
2. **Tester adds `state:tested`** on the PR: spawn a Claude session in the same
   worktree as the Reviewer.
3. **Reviewer merges** with `gh pr merge <n> --squash --delete-branch`. PR closes,
   dispatcher drops the entry from `state.json`.

Failure signal: either agent adds `state:blocked` on the PR (test failure or
review-requires-changes). The dispatcher records the block, stops touching the PR,
and a human resolves. Re-dispatching Developer with the failure context is v2.

Deferral: a PR whose head branch has no Orca worktree is skipped that tick and
retried on the next, giving a human time to open the worktree.

Prompts fed to Tester and Reviewer are written to
`.orca/dispatcher/prompts/pr-<n>-<role>.txt` (gitignored) so you can see exactly what
each agent was told when triaging.

#### `.orca/dispatcher/dispatch.py`

The full runtime. It is deterministic Python because a poll loop is not a judgement
call: an LLM asked to do this would cost more, drift between ticks, and give you no way
to reason about how many worktrees it opened.

Two behaviours are worth reading before you run it. An issue counts as *in flight* while
Orca still reports a worktree whose `linkedIssue` is that number, so a renamed worktree
still suppresses a second dispatch; and state is written **before** the issue comment, so
a crash between the two costs a comment rather than a duplicate worktree.

```python
#!/usr/bin/env python3
"""Poll GitHub for approved issues and open an Orca worktree for each one.

This is the Dispatcher *runtime*. ``~/.orca/roles/dispatcher.md`` records the policy this
script enforces; the policy file is not itself executable, and an agent left to poll in a
loop would be slower, costlier and less predictable than the deterministic loop below.

Orca is a desktop application on this host, so there is nowhere for a GitHub webhook to
arrive. The Dispatcher polls ``gh`` and creates worktrees; it never receives events.

Run it on the HOST, not inside the dev container: it drives the ``orca`` CLI, which talks to
the desktop runtime.

    python .orca/dispatcher/dispatch.py                     # foreground, Ctrl+C to stop
    python .orca/dispatcher/dispatch.py --once              # one tick, for Task Scheduler
    python .orca/dispatcher/dispatch.py --once --dry-run    # decide, change nothing

State lives in ``.orca/dispatcher/state.json`` and is the resumption point: a tick that is
interrupted half way leaves the issues it already dispatched recorded, and the next tick
picks up from there. Exactly one process may run against a repository, because that file is
not locked between processes.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Final

import yaml

log = logging.getLogger("dispatcher")

#: Where this script lives, and the repository root above it.
HERE: Final[Path] = Path(__file__).resolve().parent
REPO_ROOT: Final[Path] = HERE.parent.parent

#: Host-level roles directory, shared by every project this workflow drives.
ROLES_DIR: Final[Path] = Path.home() / ".orca" / "roles"

#: Seconds between polls when running in the foreground.
DEFAULT_INTERVAL: Final[int] = 60

#: How long any single subprocess may take before it is abandoned.
COMMAND_TIMEOUT: Final[int] = 120

#: Longest issue body carried into a prompt. A very long issue is usually a discussion
#: thread; the agent is pointed at the issue rather than handed all of it.
MAX_BODY_CHARS: Final[int] = 8000

#: Set when a signal asks the loop to stop. Checked between ticks and between issues, so a
#: shutdown never interrupts a dispatch half way through recording it.
_stop = threading.Event()


class DispatchError(RuntimeError):
    """A step could not be completed. Never fatal to the loop; the next tick retries."""


# ------------------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Config:
    """The parts of ``.orca/dispatch.yml`` this runtime acts on.

    Attributes:
        base_branch: Branch every worktree is cut from.
        protected: Branches no agent may merge into. Carried into the prompt.
        worktree_prefix: Prefix for generated worktree names.
        ready_label: Label that marks an issue approved for dispatch.
        escalated_label: Label applied when the circuit breaker trips.
        max_cycles: Dispatches allowed per issue before the breaker trips.
        label_skills: Extra skills earned by each label.
        role_skills: Baseline skills for each role.
        pipelines: Pipeline name to its definition.
    """

    base_branch: str
    protected: tuple[str, ...]
    worktree_prefix: str
    ready_label: str
    escalated_label: str
    max_cycles: int
    label_skills: dict[str, tuple[str, ...]]
    role_skills: dict[str, tuple[str, ...]]
    pipelines: dict[str, dict[str, Any]]


def _skills_by_name(section: object) -> dict[str, tuple[str, ...]]:
    """Read a ``name -> {skills: [...]}`` mapping out of the configuration.

    Args:
        section: The ``roles`` or ``labels`` block, possibly absent.

    Returns:
        Skill tuples keyed by name.
    """
    if not isinstance(section, dict):
        return {}
    out: dict[str, tuple[str, ...]] = {}
    for name, body in section.items():
        skills = (body or {}).get("skills") or [] if isinstance(body, dict) else []
        out[str(name)] = tuple(str(skill) for skill in skills)
    return out


def load_config(path: Path) -> Config:
    """Read and validate the dispatch configuration.

    Args:
        path: Path to ``dispatch.yml``.

    Returns:
        The parsed configuration.

    Raises:
        DispatchError: The file is missing, unparsable, or missing a required key.
    """
    try:
        raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise DispatchError(f"no dispatch configuration at {path}") from exc
    except yaml.YAMLError as exc:
        raise DispatchError(f"{path} is not valid YAML: {exc}") from exc

    if not isinstance(raw, dict):
        raise DispatchError(f"{path} must contain a mapping")

    branches = raw.get("branches") or {}
    breaker = raw.get("circuit_breaker") or {}
    dispatch = raw.get("dispatch") or {}

    base = branches.get("base")
    if not base:
        raise DispatchError(f"{path}: branches.base is required")

    max_cycles = breaker.get("max_cycles", 3)
    if not isinstance(max_cycles, int) or max_cycles < 1:
        raise DispatchError(f"{path}: circuit_breaker.max_cycles must be a positive integer")

    return Config(
        base_branch=str(base),
        protected=tuple(str(branch) for branch in branches.get("protected") or ()),
        worktree_prefix=str(branches.get("worktree_prefix") or "feature/"),
        ready_label=str(dispatch.get("ready_label") or "ready"),
        escalated_label=str(dispatch.get("escalated_label") or "escalated"),
        max_cycles=max_cycles,
        label_skills=_skills_by_name(raw.get("labels")),
        role_skills=_skills_by_name(raw.get("roles")),
        pipelines=dict(raw.get("pipelines") or {}),
    )


# ------------------------------------------------------------------------------------------
# State
# ------------------------------------------------------------------------------------------


@dataclass
class IssueState:
    """What has happened to one issue.

    Attributes:
        cycles: Worktrees opened for this issue so far.
        open: Whether the most recent dispatch is still in flight.
        worktree: Name of the most recent worktree.
        last_dispatched: ISO timestamp of the most recent dispatch.
        escalated: Whether the breaker has tripped and the poller has let go.
    """

    cycles: int = 0
    open: bool = False
    worktree: str = ""
    last_dispatched: str = ""
    escalated: bool = False


@dataclass
class State:
    """Everything the poller remembers between ticks."""

    issues: dict[int, IssueState] = field(default_factory=dict)

    @classmethod
    def load(cls, path: Path) -> State:
        """Read state from disk, tolerating absence and corruption.

        A state file that cannot be read is renamed aside rather than deleted: it is the only
        record of what was already dispatched, and losing it silently means double-dispatch.

        Args:
            path: Path to ``state.json``.

        Returns:
            The stored state, or an empty one.
        """
        if not path.exists():
            return cls()
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            issues = {
                int(number): IssueState(**body)
                for number, body in (raw.get("issues") or {}).items()
            }
        except (json.JSONDecodeError, TypeError, ValueError) as exc:
            spoiled = path.with_suffix(f".corrupt-{int(time.time())}.json")
            path.rename(spoiled)
            log.error("state file unreadable (%s); moved to %s and starting empty", exc, spoiled)
            return cls()
        return cls(issues=issues)

    def save(self, path: Path) -> None:
        """Write state atomically, so an interrupted write cannot truncate it.

        Args:
            path: Path to ``state.json``.
        """
        payload = {
            "version": 1,
            "updated": datetime.now(UTC).isoformat(timespec="seconds"),
            "issues": {str(n): vars(s) for n, s in sorted(self.issues.items())},
        }
        path.parent.mkdir(parents=True, exist_ok=True)
        handle, tmp_name = tempfile.mkstemp(dir=str(path.parent), suffix=".tmp")
        tmp = Path(tmp_name)
        try:
            with os.fdopen(handle, "w", encoding="utf-8") as stream:
                json.dump(payload, stream, indent=2, sort_keys=True)
                stream.flush()
                os.fsync(stream.fileno())
            tmp.replace(path)
        finally:
            tmp.unlink(missing_ok=True)


# ------------------------------------------------------------------------------------------
# Subprocess helpers
# ------------------------------------------------------------------------------------------


def _executable(name: str) -> str:
    """Resolve a command to a full path.

    On Windows ``gh`` and ``orca`` are shims that :func:`subprocess.run` will not find
    without their extension, and resolving here keeps every call site free of ``shell=True``.

    Args:
        name: Command name.

    Returns:
        Absolute path to the executable.

    Raises:
        DispatchError: The command is not on PATH.
    """
    found = shutil.which(name)
    if not found:
        raise DispatchError(f"{name} is not on PATH")
    return found


def run_command(command: list[str], *, check: bool = True) -> str:
    """Run a command and return its stdout.

    Arguments are always passed as a list, never through a shell, so an issue title
    containing shell metacharacters is data rather than code.

    Args:
        command: Command and arguments; element zero is resolved on PATH.
        check: Raise when the command exits non-zero.

    Returns:
        Captured stdout, stripped. Empty when the command failed and ``check`` is unset.

    Raises:
        DispatchError: The command failed and ``check`` is set, or it timed out.
    """
    resolved = [_executable(command[0]), *command[1:]]
    try:
        completed = subprocess.run(
            resolved,
            capture_output=True,
            text=True,
            timeout=COMMAND_TIMEOUT,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise DispatchError(f"{command[0]} timed out after {COMMAND_TIMEOUT}s") from exc

    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "").strip()[:400]
        if check:
            raise DispatchError(f"{' '.join(command[:3])} exited {completed.returncode}: {detail}")
        log.debug("%s exited %s: %s", command[0], completed.returncode, detail)
        return ""
    return completed.stdout.strip()


def run_json(command: list[str]) -> Any:
    """Run a command and parse its stdout as JSON.

    Args:
        command: Command and arguments.

    Returns:
        The decoded payload.

    Raises:
        DispatchError: The command failed or did not emit JSON.
    """
    raw = run_command(command)
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise DispatchError(f"{command[0]} did not return JSON: {raw[:200]}") from exc


# ------------------------------------------------------------------------------------------
# GitHub and Orca
# ------------------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Issue:
    """An open issue approved for dispatch."""

    number: int
    title: str
    body: str
    labels: tuple[str, ...]


def ready_issues(config: Config, repo: str | None) -> list[Issue]:
    """List open issues carrying the ready label.

    Args:
        config: Dispatch configuration.
        repo: ``owner/name``, or ``None`` to let ``gh`` infer it from the checkout.

    Returns:
        One :class:`Issue` per approved issue, lowest number first.

    Raises:
        DispatchError: ``gh`` failed or returned something other than JSON.
    """
    command = [
        "gh", "issue", "list",
        "--state", "open",
        "--label", config.ready_label,
        "--limit", "100",
        "--json", "number,title,body,labels",
    ]
    if repo:
        command += ["--repo", repo]

    issues = [
        Issue(
            number=int(item["number"]),
            title=str(item.get("title") or ""),
            body=str(item.get("body") or ""),
            labels=tuple(str(label["name"]) for label in item.get("labels") or []),
        )
        for item in run_json(command)
    ]
    return sorted(issues, key=lambda issue: issue.number)


def comment_on_issue(number: int, body: str, repo: str | None) -> None:
    """Post a comment on an issue. A failure here is logged, never fatal.

    Args:
        number: Issue number.
        body: Comment text.
        repo: ``owner/name``, or ``None``.
    """
    command = ["gh", "issue", "comment", str(number), "--body", body]
    if repo:
        command += ["--repo", repo]
    run_command(command, check=False)


def add_label(number: int, label: str, repo: str | None) -> None:
    """Add a label to an issue. A failure here is logged, never fatal.

    Args:
        number: Issue number.
        label: Label to add.
        repo: ``owner/name``, or ``None``.
    """
    command = ["gh", "issue", "edit", str(number), "--add-label", label]
    if repo:
        command += ["--repo", repo]
    run_command(command, check=False)


def runtime_ready() -> bool:
    """Report whether the Orca desktop runtime can be reached.

    Returns:
        True when Orca is running and its runtime is reachable.
    """
    try:
        payload = run_json(["orca", "status", "--json"])
    except DispatchError as exc:
        log.warning("orca status failed: %s", exc)
        return False
    runtime = (payload.get("result") or {}).get("runtime") or {}
    return bool(runtime.get("reachable"))


def issues_in_flight() -> set[int]:
    """Read which issue numbers already have an Orca worktree.

    The worktree's own ``linkedIssue`` is the source of truth rather than its name: a
    worktree renamed by hand still belongs to its issue.

    Returns:
        Issue numbers with a live worktree. Empty when Orca cannot be asked, which makes the
        caller fall back to its own record rather than dispatching a second time.
    """
    try:
        payload = run_json(["orca", "worktree", "list", "--json"])
    except DispatchError as exc:
        log.warning("orca worktree list failed: %s", exc)
        return set()

    live: set[int] = set()
    for worktree in (payload.get("result") or {}).get("worktrees") or []:
        linked = worktree.get("linkedIssue")
        if isinstance(linked, bool):
            continue
        if isinstance(linked, int):
            live.add(linked)
        elif isinstance(linked, dict) and isinstance(linked.get("number"), int):
            live.add(int(linked["number"]))
    return live


def create_worktree(name: str, issue: Issue, prompt: str, config: Config, repo_path: Path) -> None:
    """Ask Orca to open a worktree and start an agent in it.

    Args:
        name: Worktree name.
        issue: The issue being dispatched.
        prompt: Full prompt for the agent.
        config: Dispatch configuration.
        repo_path: Filesystem path of the repository.

    Raises:
        DispatchError: Orca refused to create the worktree.
    """
    run_command(
        [
            "orca", "worktree", "create",
            "--repo", f"path:{repo_path.as_posix()}",
            "--name", name,
            "--issue", str(issue.number),
            "--base-branch", config.base_branch,
            "--agent", "claude",
            "--prompt", prompt,
            "--json",
        ]
    )


# ------------------------------------------------------------------------------------------
# Routing
# ------------------------------------------------------------------------------------------


def pipeline_for(issue: Issue, config: Config) -> str:
    """Choose the pipeline an issue runs through.

    ``trivial`` is a request, not a permission: an issue that also carries a label the
    pipeline forbids runs the default pipeline instead, and a test author is asked for.

    Args:
        issue: The issue.
        config: Dispatch configuration.

    Returns:
        Pipeline name.
    """
    if "trivial" not in issue.labels:
        return "default"
    forbidden = set(config.pipelines.get("trivial", {}).get("forbid_labels") or ())
    clashes = forbidden.intersection(issue.labels)
    if clashes:
        log.info(
            "issue #%s asked for the trivial pipeline but carries %s; running default",
            issue.number,
            ", ".join(sorted(clashes)),
        )
        return "default"
    return "trivial"


def skills_for(issue: Issue, role: str, config: Config) -> tuple[str, ...]:
    """Compute the skills a role earns on this issue.

    Args:
        issue: The issue.
        role: Role about to be dispatched.
        config: Dispatch configuration.

    Returns:
        Skill names, deduplicated, baseline first and label-earned after.
    """
    seen: dict[str, None] = dict.fromkeys(config.role_skills.get(role, ()))
    for label in issue.labels:
        for skill in config.label_skills.get(label, ()):
            seen.setdefault(skill, None)
    return tuple(seen)


def build_prompt(issue: Issue, role: str, pipeline: str, cycle: int, config: Config) -> str:
    """Assemble the prompt handed to the agent.

    The role prompt is read from the shared host-level roles directory, so every project
    dispatches the same agent definition and a fix to one is a fix to all.

    Args:
        issue: The issue being dispatched.
        role: Role to dispatch, e.g. ``developer``.
        pipeline: Pipeline name.
        cycle: Which attempt this is, counting from one.
        config: Dispatch configuration.

    Returns:
        The full prompt.

    Raises:
        DispatchError: The role prompt is missing from ``~/.orca/roles/``.
    """
    role_file = ROLES_DIR / f"{role}.md"
    try:
        role_text = role_file.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise DispatchError(
            f"no role prompt at {role_file}; populate ~/.orca/roles/ once per machine"
        ) from exc

    body = issue.body.strip()
    if len(body) > MAX_BODY_CHARS:
        body = body[:MAX_BODY_CHARS] + "\n\n[...truncated; read the issue for the rest]"

    skills = skills_for(issue, role, config)
    stages = config.pipelines.get(pipeline, {}).get("stages") or []

    return "\n".join(
        [
            role_text.rstrip(),
            "",
            "---",
            "",
            "## DISPATCH CONTEXT",
            "",
            f"- Issue: #{issue.number} - {issue.title}",
            f"- Role: {role}",
            f"- Pipeline: {pipeline} ({' -> '.join(str(stage) for stage in stages)})",
            f"- Attempt: {cycle} of {config.max_cycles}",
            f"- Labels: {', '.join(issue.labels) or 'none'}",
            f"- Skills earned: {', '.join(skills) or 'none beyond the baseline'}",
            f"- Base branch: {config.base_branch}",
            f"- Never merge into: {', '.join(config.protected) or 'main'}",
            "",
            "## ISSUE",
            "",
            body or "(the issue has no body; read it on GitHub before starting)",
        ]
    )


# ------------------------------------------------------------------------------------------
# The loop
# ------------------------------------------------------------------------------------------


def trip_breaker(issue: Issue, record: IssueState, config: Config, repo: str | None) -> None:
    """Escalate an issue to a human and stop touching it.

    Args:
        issue: The issue.
        record: Its state, mutated in place.
        config: Dispatch configuration.
        repo: ``owner/name``, or ``None``.
    """
    log.error("circuit breaker tripped on issue #%s after %s attempts", issue.number, record.cycles)
    comment_on_issue(
        issue.number,
        f"Circuit breaker tripped: {record.cycles} failed attempts.\n\n"
        "The dispatcher will not open another worktree for this issue. A human needs to read "
        "what the previous attempts produced before it goes round again.",
        repo,
    )
    add_label(issue.number, config.escalated_label, repo)
    record.escalated = True
    record.open = False


def tick(config: Config, state: State, args: argparse.Namespace) -> int:
    """Run one poll-and-dispatch pass.

    Args:
        config: Dispatch configuration.
        state: Poller state, mutated and saved as it goes.
        args: Parsed command line.

    Returns:
        How many worktrees were created, or would have been in a dry run.

    Raises:
        DispatchError: The issue list could not be read. The caller retries next tick.
    """
    if not args.dry_run and not runtime_ready():
        log.warning("Orca runtime is not reachable; skipping this tick")
        return 0

    issues = ready_issues(config, args.repo)
    if not issues:
        log.info("no open issues carry the '%s' label", config.ready_label)
        return 0

    live = issues_in_flight()
    dispatched = 0

    for issue in issues:
        if _stop.is_set():
            log.info("stop requested; leaving the remaining issues for the next tick")
            break

        record = state.issues.setdefault(issue.number, IssueState())

        if record.escalated or config.escalated_label in issue.labels:
            log.debug("issue #%s is escalated; leaving it alone", issue.number)
            continue

        if record.open:
            if issue.number in live:
                log.debug("issue #%s is still in flight in %s", issue.number, record.worktree)
                continue
            log.info(
                "issue #%s: worktree %s is gone, attempt %s is over",
                issue.number, record.worktree or "(unnamed)", record.cycles,
            )
            record.open = False

        if record.cycles >= config.max_cycles:
            if args.dry_run:
                log.error("DRY RUN: would trip the circuit breaker on #%s", issue.number)
            else:
                trip_breaker(issue, record, config, args.repo)
                state.save(args.state)
            continue

        pipeline = pipeline_for(issue, config)
        stages = config.pipelines.get(pipeline, {}).get("stages") or ["developer"]
        role = str(stages[0])
        cycle = record.cycles + 1
        name = f"{config.worktree_prefix}issue-{issue.number}"

        try:
            prompt = build_prompt(issue, role, pipeline, cycle, config)
        except DispatchError as exc:
            log.error("cannot dispatch #%s: %s", issue.number, exc)
            continue

        if args.dry_run:
            log.info(
                "DRY RUN: would dispatch #%s to %s as '%s' "
                "(pipeline %s, attempt %s of %s, prompt %s chars, skills: %s)",
                issue.number, role, name, pipeline, cycle, config.max_cycles, len(prompt),
                ", ".join(skills_for(issue, role, config)) or "baseline only",
            )
            dispatched += 1
            continue

        try:
            create_worktree(name, issue, prompt, config, args.repo_path)
        except DispatchError as exc:
            log.error("Orca refused a worktree for #%s: %s", issue.number, exc)
            continue

        record.cycles = cycle
        record.open = True
        record.worktree = name
        record.last_dispatched = datetime.now(UTC).isoformat(timespec="seconds")
        # Saved before the comment: a crash between the two costs a comment, not a second
        # worktree on the next tick.
        state.save(args.state)

        comment_on_issue(
            issue.number,
            f"Dispatched to the **{role}** in worktree `{name}` "
            f"(pipeline `{pipeline}`, attempt {cycle} of {config.max_cycles}).",
            args.repo,
        )
        log.info("dispatched #%s to %s in %s (attempt %s)", issue.number, role, name, cycle)
        dispatched += 1

    return dispatched


def install_signal_handlers() -> None:
    """Ask the loop to stop at the next safe boundary when interrupted."""

    def handler(signum: int, _frame: object) -> None:
        log.info("signal %s received; finishing the current issue, then stopping", signum)
        _stop.set()

    signal.signal(signal.SIGINT, handler)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, handler)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    """Parse the command line.

    Args:
        argv: Arguments, or ``None`` to read ``sys.argv``.

    Returns:
        Parsed arguments.
    """
    parser = argparse.ArgumentParser(
        description="Poll GitHub for approved issues and open an Orca worktree for each.",
    )
    parser.add_argument("--once", action="store_true", help="Run one tick and exit.")
    parser.add_argument(
        "--interval", type=int, default=DEFAULT_INTERVAL,
        help=f"Seconds between polls. Default: {DEFAULT_INTERVAL}.",
    )
    parser.add_argument(
        "--repo", default=None,
        help="owner/name for gh. Default: inferred from the checkout.",
    )
    parser.add_argument(
        "--repo-path", type=Path, default=REPO_ROOT,
        help="Repository path handed to Orca. Default: this checkout.",
    )
    parser.add_argument(
        "--config", type=Path, default=REPO_ROOT / ".orca" / "dispatch.yml",
        help="Path to dispatch.yml.",
    )
    parser.add_argument(
        "--state", type=Path, default=HERE / "state.json",
        help="Path to the state file.",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Decide and report; create no worktree and comment nowhere.",
    )
    parser.add_argument(
        "--reopen", type=int, metavar="ISSUE", default=None,
        help="Clear the in-flight and escalated flags for one issue, then exit.",
    )
    parser.add_argument("--verbose", action="store_true", help="Log at DEBUG.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """Entry point.

    Args:
        argv: Arguments, or ``None`` to read ``sys.argv``.

    Returns:
        ``0`` on a clean run or a clean shutdown, ``1`` on a configuration error.
    """
    args = parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(message)s",
        datefmt="%H:%M:%S",
    )

    try:
        config = load_config(args.config)
    except DispatchError as exc:
        log.critical("%s", exc)
        return 1

    state = State.load(args.state)

    if args.reopen is not None:
        record = state.issues.get(args.reopen)
        if record is None:
            log.error("issue #%s is not in the state file", args.reopen)
            return 1
        record.open = False
        record.escalated = False
        state.save(args.state)
        log.info("issue #%s reopened for dispatch (%s attempts so far)", args.reopen, record.cycles)
        return 0

    install_signal_handlers()
    log.info(
        "dispatcher up: base=%s ready=%s max_cycles=%s%s",
        config.base_branch, config.ready_label, config.max_cycles,
        " (DRY RUN)" if args.dry_run else "",
    )

    while not _stop.is_set():
        try:
            tick(config, state, args)
        except DispatchError as exc:
            log.error("tick failed: %s", exc)
        # A poller that dies on one bad tick is worse than one that logs and retries.
        except Exception:
            log.exception("unexpected error in tick")

        if args.once:
            break
        # Interruptible sleep: Ctrl+C is answered at once, not after the whole interval.
        _stop.wait(args.interval)

    log.info("dispatcher stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

**Running it** — two supported shapes:

```bash
# Foreground, one terminal. Ctrl+C to stop.
python .orca/dispatcher/dispatch.py

# One tick and exit — for Windows Task Scheduler / cron / Orca automation.
python .orca/dispatcher/dispatch.py --once
```

Never leave both a foreground poller AND a scheduled `--once` running against the same
repo: `state.json` is not locked between processes and you will double-dispatch.

**Circuit breaker (see also section 11).** `max_cycles: 3` in `dispatch.yml`. The
Dispatcher increments a cycle count each time it opens a worktree for an issue; on the
fourth request it does not create a worktree. Instead it posts
`Circuit breaker tripped: 3 failed attempts.` on the issue, adds the `escalated` label,
and stops touching that issue. Do not raise `max_cycles`.

Once a human has dealt with an escalated issue, put it back in play with
`python .orca/dispatcher/dispatch.py --reopen <n>` and remove its `escalated` label. That
clears the flags but **keeps the cycle count**, so an issue that fails again trips the
breaker at once instead of getting three fresh attempts.

---

## 9. Step 6 — Running the workflow, A to G

### A — Initialise

```bash
orca init --base-branch dev
orca --version && ls .orca/skills
```

### B — Onboarding interview

Send the Dispatcher: `@dispatcher start onboarding`.

It wakes the **PO & Analyst Agent** with `grill-with-docs` and `domain-modeling`. That agent
interviews you in rounds and populates `docs/CORE_DOCUMENT.md`. It does not proceed until you
agree the document is right.

It then derives `docs/specs/CORE_SPEC.md` and the first ADRs from it.

> If the project already has a core document, **port it rather than re-interviewing**. An
> interview that rediscovers decided things spends the one resource the setup cannot
> manufacture: your attention.

### C — Contracts frozen

The **Architect Agent** reads the core document and the specs, then writes the interface
types, schemas and API contracts on `dev` and freezes them. Feature work does not open until
this is done.

### D — Issues and dispatch

The PO & Analyst writes issues with `role:*` and skill labels. A webhook notifies the
Dispatcher, which creates the worktree:

```bash
orca worktree create feature/issue-42 --base dev
```

and invokes the **Developer Agent** with the skills the labels earned.

### E — Testing

The Dispatcher hands the worktree to the **Testing Agent** using `handoff`. It applies the
hybrid strategy — direct tests for standard code, the FMEA interview for large components
(see [section 11](#11-the-fmea-protocol)). Tests execute inside the container.

Pass → the Reviewer. Fail → back to the Developer, and the cycle count increments.

### F — Review and merge to `dev`

The **Reviewer Agent** checks the specification, the code standard, the passing tests and the
evidence gate, then merges into `dev` and deletes the worktree.

### G — Promotion to `main`

**You** inspect `dev` and open the pull request into `main` yourself. No agent ever does.

---

## 10. The circuit breaker

Three round trips between Developer and Tester/Reviewer. On the fourth the Dispatcher:

1. halts and hides the worktree,
2. posts `Circuit breaker tripped: 3 failed attempts.` on the issue,
3. reassigns the issue to the human with the diff and the error logs,
4. does **not** loop back to the Developer.

Do not raise the limit to force something through. An issue that has failed three times is
saying the specification is wrong, and a fourth attempt at the wrong thing costs more than
asking.

---

## 11. The FMEA protocol

For **large components** — data fetching, storage, core frontend — the Testing Agent stops
before writing anything and produces a failure matrix:

| Proposed Component | Failure Mode | Default Recovery Action | Human Confirmation Needed? |
| --- | --- | --- | --- |
| *component under test* | *how it can fail* | *what the system should do* | *yes / no* |

It presents the table to you in chat and asks for missing edge cases and custom recovery
actions. **Only after you confirm** does it write the suite, and the confirmed table becomes
the test list.

Why the interview exists: for a large component the interesting failures are the ones the
implementer did not think of, and the person who knows which failures matter is the owner, not
the agent. The matrix is the artefact of that conversation — commit it beside the tests.

Two standing warnings for the Testing Agent:

- **A fake that behaves better than the real thing proves nothing.** A test double that is
  more reliable, more ordered or more complete than the system it stands for will pass code
  that fails in production.
- **A test can pin a mistake.** If a test asserts the old behaviour and the change is right,
  the test is what changes — deliberately, in its own commit, with the reason stated.

---

## 12. Evidence before merge

A circuit breaker catches thrashing. It does not catch the failure that costs most: a run that
**reports success while being wrong**. Green tests prove the code does what the tests already
assumed, which is a different claim from "the code is right".

So the Reviewer may not merge on green tests alone. For changes touching data or a pipeline,
`evidence_gates` in `dispatch.yml` require a claim checked against something the change did
not produce:

- **counts reconciled** — a number from the run against a number from an independent source,
  both quoted in the pull request;
- **output sampled** — enough real rows pasted in that a wrong one would be visible;
- **cost measured** — against its budget, where one is documented.

The principle: **an artefact describes itself by observation, not by declaration.** A process
that reports on itself will report what it believes.

---

## 13. The daily digest

The digest is **derived from commit and pull-request history**, not from logs the agents
write. Agent-written logs get forgotten; two agents in parallel worktrees appending to one
daily file conflict on every merge; and a report assembled from self-description tells you
what each agent believed, which is the thing you are checking. Commits and pull requests exist
anyway and cannot be skipped.

### Contract for `mail/daily_digest.py`

| Aspect | Behaviour |
| --- | --- |
| Sources | `git log <branch> --since='<day> 00:00:00 +0000' --until='<day+1> 00:00:00 +0000' --no-merges`, and `gh pr list --base <branch> --state merged --json number,title,author,labels,mergedAt,url` |
| Date bounds | Explicit UTC midnight, never a bare `YYYY-MM-DD`. Git's approxidate parser fills omitted fields from the current clock, so `--since=2026-08-22` means that day *at the time the job runs*. An evening schedule then reports only what landed after it: nothing, every day, exiting green. |
| Grouping | Merged pull requests first, then commits grouped by author |
| Quiet day | Prints "nothing landed" and **sends no mail** |
| No credentials | Prints the rendered HTML instead of sending — always safe to run |
| Failure | Any missing tool, branch or auth degrades to "nothing from this source"; **never** exits non-zero |
| Escaping | Every value from a commit message, title or label is HTML-escaped |
| Encoding | `MIMEText(body, "html", "utf-8")` — the default is us-ascii and mangles punctuation |
| Fields split | Use control characters (`\x1f`, `\x1e`) in `--pretty=format`; a commit subject can contain any printable delimiter you might otherwise pick |

```bash
python mail/daily_digest.py --branch dev --print
python mail/daily_digest.py --branch dev --day 2026-01-15 --print
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
          # Dispatch inputs reach the shell as environment values, never spliced into the
          # script: an expression expanded inline is substituted before bash parses the line,
          # so a crafted input would be read as shell rather than as an argument.
          DAY: ${{ inputs.day }}
          PRINT_ONLY: ${{ inputs.print_only }}
        run: |
          args=(--branch dev)
          if [ -n "$DAY" ]; then args+=(--day "$DAY"); fi
          if [ "$PRINT_ONLY" = "true" ]; then args+=(--print); fi
          python mail/daily_digest.py "${args[@]}"
```

Leave the SMTP secrets unset at first: the scheduled run renders into the workflow log, which
is how you find out whether the content is worth reading before it starts arriving.

---

## 14. Branch protection

The human gate is a rule in the prompts **and** a rule on the repository. Set both — a prompt
is an instruction, a branch rule is an enforcement.

Prefer a **ruleset** over the legacy branch-protection API. Create it in
Settings -> Rules -> Rulesets, targeting `main`, enforcement **active**, with:

- Require a pull request before merging
- Require status checks to pass
- Require linear history
- Restrict deletions
- **Bypass list: empty**

An empty bypass list is the part that matters. `enforce_admins=false` on the legacy endpoint
lets every admin push straight to `main`, which leaves the gate open for exactly the accounts
most likely to be automated.

Verify what is actually enforced. Reading the ruleset needs admin, so a `404` from the agent's
account means "you cannot see it", not "there is none":

```bash
gh api repos/:owner/:repo/rules/branches/main     # what applies, readable by any collaborator
gh api repos/:owner/:repo/rulesets/<id>           # enforcement level and bypass actors
```

Separately, give the agent's GitHub account **write, not admin**. A rule the account cannot
edit is worth more than a prompt telling it not to.

---

## 15. Known tooling pitfalls

Verify each of these on your own machine. They commonly differ from what documentation and
plans claim, and every one of them fails the setup for everybody rather than for one person.

| Check | What is actually true |
| --- | --- |
| Orca via npm | Not published to npm. It is a host desktop application and cannot be containerised. |
| `skills install ... --target <dir>` | The verb is `add`, the flag is `--skill`; `--target` does not exist. |
| `--agent` has one spelling | It has two, on two different CLIs. The **skills** CLI takes `--agent claude-code`. **Orca** takes `--agent claude` - its own help says `--agent defaults to claude`. Copying one into the other breaks either skill installation or every dispatch. |
| Base tag `1-22` clears the Node floor | It ships 22.16; the CLI declares 22.20.0. Pin upward with `n`. |
| Chaining after `n` | `n` swaps the node binary underneath the shell; a later `npm` in the same `RUN` resolves to the replaced binary. |
| Shell scripts from a Windows host | CRLF reaches bash unchanged because the container mounts the working tree, not a fresh checkout. Fails as `set: pipefail: invalid option name`. |
| `npm ci` over a bind mount | Replaces the host's `node_modules` with Linux binaries and breaks the host's tooling. Shadow it with a volume. |
| Skill names taken from a plan | Read them back with `--list` before committing them. |
| `~/.orca/roles/dispatcher.md` alone is a runtime | It is not. The runtime is `.orca/dispatcher/dispatch.py`. Without that script running, no issues get routed and the whole pipeline stalls silently. |
| "A webhook notifies the Dispatcher" | Orca is a host desktop app; webhooks have nowhere to arrive. The Dispatcher POLLS `gh`. Any wording implying event-driven dispatch is aspirational. |
| Dispatcher counted as one of the seven roles | It is not an agent. The seven files in `~/.orca/roles/` include one policy file (`dispatcher.md`) and six agent prompts (`po-analyst`, `architect`, `researcher`, `developer`, `tester`, `reviewer`). Only the six are dispatched by Orca. |
| Role prompts pasted per project (`.orca/system_prompts/`) | Do not. Roles are HOST-LEVEL at `~/.orca/roles/` and shared across every project this workflow drives. A per-project copy is exactly the drift trap this convention exists to prevent. |
| `ready` alone dispatches an issue | It does not. Every ready issue also needs exactly one `role:*` label (`role:developer`, `role:architect`, `role:researcher`, …). Without one the poller comments and skips. `role:*` is what drives which role prompt gets loaded — the default pipeline `[developer, tester, reviewer]` only applies to `role:developer`. |

---

## 16. Checklist

```
[ ] docs/CORE_DOCUMENT.md exists and the onboarding interview has populated it
[ ] docker info succeeds
[ ] Reopen in Container succeeds; node >= 22.20.0 and python answer
[ ] npx skills list shows every skill in the matrix
[ ] Large data mounts read-only; a write probe inside the container is refused
[ ] The project's own test suite passes inside the container
[ ] ~/.orca/roles/ on this host holds all seven role files (six agents + dispatcher.md policy) — populated ONCE per machine, never per project
[ ] .orca/dispatch.yml names your labels, pipelines and gates
[ ] Every agent prompt forbids merging to main; only the Reviewer may merge to dev
[ ] gh labels created: ui seo scraper bug data trivial ready escalated role:developer role:architect role:researcher state:tested state:blocked
[ ] .orca/dispatcher/dispatch.py exists and `python .orca/dispatcher/dispatch.py --once` exits 0
[ ] The Dispatcher process is actually running — a foreground terminal, a scheduled --once, or an Orca automation — not just present in the repo
[ ] Only ONE dispatcher process is running against this repo (state.json is not locked between processes)
[ ] python mail/daily_digest.py --print renders a real day
[ ] Branch protection on main: no direct pushes, pull request required
[ ] SMTP secrets set, or deliberately unset so the digest prints to the log
```
