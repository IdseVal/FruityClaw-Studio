# ADR-0001 — LLM provider support for the Assistant

- **Status:** Proposed. Awaits the owner's acceptance — this closes open question 4 of
  `docs/CORE_DOCUMENT.md` §7, which the owner delegated to research but did not delegate
  the final pick of.
- **Date:** 2026-08-23
- **Issue:** #4
- **Decider:** project owner (Idse Val)
- **Evidence:** [`docs/research/issue-4-llm-tool-calling-evidence.md`](../research/issue-4-llm-tool-calling-evidence.md)

---

## Context

The Assistant reaches the whole musical content of a Project (§3.8) but acts only through
single-purpose **Functions**, each of which "does exactly one thing and touches nothing
else." §9.2 restates this as an absolute: *the Assistant must never perform a broad
mutation.* The user supplies their own key (§2, NON-scope 2); the Studio is a complete DAW
with no AI configured at all (§1.1a).

Appendix B.4 recorded a promising finding: the Claude API's strict tool use turns §3.8 from
a convention into an API-enforced constraint. This ADR tests that finding against the
vendors' shipped artefacts and documentation, and reaches a more qualified conclusion.

### What the research established

Full evidence, with quotes and URLs, is in the linked evidence log. Three findings drive
this decision.

**1. Anthropic's guarantee is real, mechanical, and the most clearly stated.**
`strict: true` constrains token sampling to schema-valid outputs (grammar-constrained
sampling). The published SDK type says it "guarantees schema validation on tool names and
inputs" — covering the Function *name* as well as its arguments. The name guarantee matters
here specifically: §3.10 rebuilds the Function file from the switchboard toggles, and a
guarantee that the model cannot name a Function outside the offered set is what makes a
disabled Function genuinely unreachable rather than merely unlisted.

**2. Strict mode is not enough, on any provider — and Anthropic is weakest exactly where
this domain is strongest.** Anthropic's grammar-constrained sampling does **not** support
`minimum`, `maximum`, `multipleOf`, `minLength` or `maxLength`, and supports array
`minItems` only for the values 0 and 1. Nearly every Function argument in a DAW is a bounded
integer — MIDI note 0–127, velocity 0–127, step index, bar number, Track index, tempo. Under
strict mode a Function will receive a schema-valid `velocity` of `9000` unless the range is
written out as a 128-value enum. OpenAI's strict mode, by contrast, *does* support numeric
bounds. Whichever provider is chosen, **§9.2 is not satisfied by the API alone.**

**3. The providers' schema subsets differ in incompatible directions.** They are not a
ladder with a weakest rung. Anthropic has no numeric constraints but an explicit tool-name
guarantee; OpenAI has numeric constraints but silently degrades to non-strict on the
Responses API when a schema is incompatible; Gemini has no per-tool `strict` flag at all —
its guarantee, whatever it is, attaches to the *request* rather than to the Function, and
its own SDK docstring and documentation disagree about whether arguments are even covered.

### The floor problem, stated explicitly

The issue asks for this to be faced rather than glossed, so: **if the Studio supports
several providers, the weakest provider's guarantees set what the Studio can promise about
Function behaviour.** A user running the Assistant on Gemini would get whatever Gemini
actually enforces, while the Studio's settings tab and documentation would be making one
promise for everybody.

The research shows the problem is worse than a lowest-common-denominator floor. Because the
subsets differ in *kind*, multi-provider support forces a choice between two bad options:

- **Intersect the subsets.** Every Function is expressed in the vocabulary all providers
  share. OpenAI's numeric bounds are discarded and nothing is gained; the floor is now the
  worst of each dimension rather than the worst provider.
- **Keep a per-provider schema variant of each Function.** Then the "Function file" is no
  longer one manifest. §3.10 says the Function file is rebuilt from the toggles; §8.4's
  emergent privacy property depends on a user being able to look at one labelled list and
  know that disabling every Rework Function means their music never leaves the machine.
  Three dialects of that list is three things to keep correct and three things to audit,
  and the guarantee the user is being offered is the one they can *verify*.

Neither is acceptable for an MVP whose §9 is written in absolutes.

---

## Decision

**1. One provider for the MVP: Anthropic (Claude API), with the user supplying their own
key.**

Chosen because it is the only candidate whose per-Function guarantee covers **both** the
argument object and the Function name, stated unambiguously and with the enforcement
mechanism named. The name guarantee is what makes the switchboard (§3.10) a real boundary
rather than a listing convention.

Supported models are those the provider lists for structured outputs; `claude-opus-5` is the
sensible default for the Assistant, with a cheaper model available for Sub-agents. The
Studio must not hard-code a single model id — the list changes, and §8.1's
"disposable-by-design" instinct applies to vendor identifiers too.

**2. Every Function call is validated locally, at the dispatch boundary, before it touches
the Project — unconditionally, and not as a provider fallback.**

This is part of the decision, not a downstream implementation note, because it is the reason
the decision is safe. Finding 2 shows the API cannot express range constraints; therefore
§9.2 is satisfied by the Studio or not at all. The validator checks the full contract —
including the bounds strict mode cannot carry — and rejects the call rather than clamping
it. A rejected call is a no-op, so §9.7 (no irreversible Assistant action) holds trivially:
nothing entered the delta history.

This inverts the usual reasoning about provider lock-in. Because the Studio is the
enforcement point, the provider is *not* load-bearing for correctness; it is load-bearing
for how often a call is rejected. That makes adding a provider later a measurable quality
question, not a re-litigation of §3.8.

**3. Additional providers are deferred, not refused, and the bar for adding one is written
down now** — see "Criteria for adding a provider" below, so a future contributor is arguing
against a stated standard rather than a preference.

---

## Consequences

### Accepted costs

- **A user with only an OpenAI or Google key cannot run the Assistant.** This is a real cost
  and it is survivable only because of §1.1a: the Studio is a fully functional DAW with no
  AI configured, and declining the key prompt is a first-class path. The user is not shut out
  of the product, only of the Assistant. If §1.1a ever weakens, this ADR must be revisited.
- **A single-vendor dependency in a GPLv3 project reads badly**, and forks will add providers
  regardless. Decision 2 is what makes that harmless: a fork adding a provider inherits the
  validator and cannot accidentally weaken §9.2 by doing so.
- **The first-open key prompt (§1.1a, Issue #18) must name the provider**, since a key from
  elsewhere will not work. "Paste your API key" is not sufficient copy.

### Consequences other Issues must absorb

- **Issue #6 (Function surface and Function file generation).** The Function file has one
  target dialect. Bounded integers cannot be expressed as `minimum`/`maximum`; the choice is
  an enum enumerating the range or a plain `integer` plus the local validator. Prefer the
  plain type and let the validator own bounds, until the enum approach is measured — see
  the verification task.
- **Issue #16 (Assistant panel and Function execution).** The validator is the single
  dispatch gate. Nothing calls a Function except through it.
- **Issue #17 (Function switchboard).** Rebuilding the Function file on a toggle plausibly
  invalidates both the provider's compiled-grammar cache and the prompt cache. Unmeasured;
  budget for it.
- **§8.4, what leaves the machine.** Under strict mode the provider compiles and caches
  tool schemas server-side for up to 24 hours, separately from message content. The Function
  file's *shape* therefore leaves the machine and persists briefly, even for a user who has
  disabled every Rework Function. Project content still does not. §8.4 is not wrong, but a
  user promised "my music never leaves this machine" should be told precisely that — their
  music, not the manifest.

### Verification task before Issue #16 is built

The highest-value unknown: **does a 128-value integer enum work in practice under strict
mode** — compile time, token cost, and effect on the model's accuracy? Nothing in the
documentation answers it, and no live call was possible while producing this ADR. One
experiment with a real key settles both this and whether the enum approach in Issue #6 is
viable at all.

---

## Rejected alternatives

### A. Support several providers from day one (Anthropic + OpenAI + Gemini)

Rejected. This is the option the issue asks to be argued against explicitly, so: it fails
not on effort but on what the Studio could then honestly claim. The subsets differ in kind
(see the floor problem), so the choice is between a floor worse than any single provider and
three Function-file dialects. The second breaks the single auditable manifest that §3.10 and
§8.4 both rest on. The reach it would buy is worth less than it appears, because §1.1a
already means a user without the right key still has the whole DAW.

### B. OpenAI as the single provider

Rejected, though it is the closest call, and it deserves the credit the evidence gives it:
OpenAI's strict mode **does** support the numeric bounds this domain needs, which Anthropic's
does not. Against it: no comparable explicit guarantee on the *tool name*, which is what
makes a disabled Function unreachable rather than merely unlisted; and the Responses API
**silently falls back to non-strict when a schema is incompatible**. A silent downgrade is
the worst possible failure mode for a §9 absolute — the Studio would keep making its promise
while the enforcement had quietly stopped. Under decision 2 the numeric-bounds advantage
largely evaporates, since the validator owns bounds either way.

### C. Google Gemini as the single provider

Rejected. There is no per-tool `strict` flag; the guarantee attaches to the request, so it
cannot be reasoned about per Function, which is the unit §3.8 is written in. Its own SDK
docstring and documentation disagree about whether `VALIDATED` covers arguments at all. A
guarantee that cannot be stated precisely cannot be the basis of a §9 absolute. This is
"not established", not "known to be bad" — if Google states an explicit per-tool guarantee
later, criterion 1 below is met and it can be reconsidered.

### D. Provider-agnostic via an OpenAI-compatible shim, or a local model (Ollama, llama.cpp)

Rejected, and settled by the core document rather than by this research: §1.1a records that
a key is required for the Assistant to work. The attraction is obvious — a local Assistant
would make §8.4's local-only configuration total rather than structural. But an
OpenAI-compatible endpoint is a *wire format*, not a guarantee: what a given local runtime
enforces depends on that runtime's grammar support and the weights behind it, and the Studio
would be promising §9.2 on behalf of software it has never seen. Worth revisiting as a
deliberate, separately-decided feature; not something to acquire by accident through a
compatibility shim.

### E. No strict mode — validate every call locally and retry on mismatch

Rejected as the *whole* answer, adopted as *half* of it. Local validation is decision 2 and
is mandatory. But declining strict mode as well would discard a free, decode-time reduction
in rejected calls and, more importantly, the tool-name guarantee that the validator cannot
reproduce: the validator can reject a call to a disabled Function, but only the API
constraint prevents the model from producing one. Belt and braces, with the braces load-bearing.

---

## Criteria for adding a provider later

A second provider is added when, and only when, all four hold:

1. It offers a **per-Function** schema guarantee, stated by the vendor, with the enforcement
   mechanism named — not a request-level mode, and not "best effort".
2. It guarantees the **called Function name** is one of those offered, so the switchboard
   remains a boundary.
3. It **never silently degrades**: an incompatible schema must be an error, not a quiet
   downgrade to unconstrained sampling.
4. The Function file compiles to that provider **without weakening any other provider's**
   schemas — one manifest, no per-provider variants.

Anything falling short is not thereby excluded from the project; it is excluded from being
supported *silently*. A provider that fails criterion 3 could in principle ship behind an
explicit warning — but that is a different decision, and it belongs to whoever proposes it.

---

## Follow-up: this ADR cannot yet be reflected in the core document

`docs/CORE_DOCUMENT.md` on `dev` is still the **empty stub**. The populated document — the
one this ADR cites throughout — exists only on the unmerged branch `DeKnecht/onboarding`
(commit `e58d207`, "Populate core document from owner interview").

Nothing was edited in the stub, because writing into a file that is about to be replaced
wholesale would either be lost or cause a conflict. When `DeKnecht/onboarding` lands on
`dev`, three edits are owed, and they are small:

1. **§7** — replace `**OPEN — Idse.** Which specific LLM providers are supported.` with the
   decision and a link to this ADR.
2. **§10, row 4** — change the disposition from "Research Issue" to "Decided — ADR-0001".
3. **Appendix B.4** — keep the strict-tool-use finding, and add the qualification this
   research produced: strict mode does not support numeric constraints, so §3.8 is enforced
   by the API *and* by a local validator, not by the API alone.
