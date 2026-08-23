# ADR-002 — The Function surface and Function file generation

- **Status:** Proposed — frozen contract on merge, pending owner ratification of the items in *Open items*
- **Date:** 2026-08-23
- **Issue:** #6 — Design the Function surface and Function file generation
- **Decided by:** Architect agent
- **Supersedes:** nothing
- **Specification:** [`docs/specs/function-surface.md`](../specs/function-surface.md) — the 43-Function catalogue, the builder contract and the two worked traces live there
- **Constrains:** issues #4, #5, #7, #16, #17, #19 (obligations table, spec §8)

## Context

`docs/CORE_DOCUMENT.md` §3.8 sets the governing principle and then, unusually for a requirement,
warns about how it will be misread:

> "'The agent should not have many permissions' means *no blunt tools*, not *a small territory*. A
> single broad `mutate_project(json)` Function would violate this even though it is one permission.
> Breadth of reach comes from having many precise Functions."

Four further constraints bear on the same surface:

- §3.10 / §9.3 — a disabled Function is **removed from what the Assistant is offered**, not refused
  when called.
- §8.4 — two Function classes. **Directive** transmits no Project content; **Rework** transmits it,
  and the *choice of Function* is what decides.
- §8.4 round 7 — because of those two facts together, disabling every Rework Function is a
  **structural** guarantee that no Project content leaves the machine. The switchboard is a privacy
  control.
- §9.1, §9.2, §9.7 — never settings, never a broad mutation, never irreversible.

The design problem is not "which operations should the AI have". It is: **how do you make those
four properties true by construction, so that no reviewer, test or runtime check has to keep them
true?** A design where each is a rule someone must remember is a design where each will eventually
be broken by a well-meaning contributor unblocking a caller.

## Decision

Six decisions. Each turns one of those properties from a convention into a shape.

### D1 — 43 single-purpose Functions: 40 Directive, 3 Rework

Enumerated in spec §5, grouped by territory: Pattern lifecycle (5), step events (3), note events
(6), Channels and Instruments (8), Effects (5), Arrangement (8), musical frame (3), generation (2,
gated), Rework (3). Every Function states its single effect and what it provably does not touch;
six invariants (spec §5.1) hold across all of them so the per-Function column carries only the
near-miss information.

The grain rule: **the unit of a Function's effect is the smallest musical object a user would
name** — a lane, a note, a clip — not the smallest field. Narrowness is blast radius, not field
size.

### D2 — Settings are unreachable by construction, not by check

Every Function receives exactly one object: `MusicalContent`, a read-only view rooted at the
musical subtree of the Project. It has no reference to Studio settings, provider credentials, the
toggle store, view state, the Project's file path, or the filesystem. **There is nothing to reach
through.**

The reach test is two clauses: an object is addressable iff it is persisted in the Project
document **and** it is part of the Project's musical content. Tempo passes (it is saved, and it
changes what the Project sounds like); buffer size fails the first clause; window zoom fails the
second.

The settings valve is one-way: settings flow *into* the Function file, deciding what is built.
They never flow *to* a Function.

### D3 — Class is a type distinction, not a metadata flag

```
DirectiveFn = fn(Args, MusicalContent) -> Result<Delta, FunctionError>
ReworkFn    = { collect: fn(Args, MusicalContent) -> Excerpt,
                apply:   fn(Args, ModelReply, MusicalContent) -> Result<Delta, FunctionError> }
```

A Directive Function does not have a `collect` member. `Excerpt` has exactly one constructor and it
is `ReworkFn::collect`. The outbound payload is a sum type whose transmitting variant `Turn2Rework`
cannot be constructed without an `Excerpt`.

A label can be wrong. A missing member cannot. This is what makes the §8.4 round-7 guarantee a
proof rather than an assurance — the chain is set out link by link in spec §7.

**Rework's flow is two turns, and turn 1 never carries Project content for either class.** The user
prompt goes out alone; the model selects `rework_pattern`; the Studio then calls `collect` and makes
a *second* request carrying that one Pattern. Transmission is a distinct, later, attributable act
by one named Function — reached from exactly one place in the Studio.

**Rework Functions are rationed.** Three in the MVP. Adding one requires amending this ADR; adding
a Directive Function does not. Directive breadth is the point of §3.8; Rework breadth is a cost.

### D4 — Functions address by Selector, never by identifier

A Function argument that designates an entity is one of `Focused`, `Ordinal(kind, n)`,
`Named(text)`, `LastCreated(kind)`. `Named` carries **the user's own words, echoed back from their
own prompt**; the Studio resolves them against the real Project locally. Ambiguity is answered by
asking the *user* in the Assistant panel, never by sending the model a candidate list.

This is the decision that makes D3's guarantee survive contact with reality. Id-based arguments
would require the model to have been told the Project's identifiers — Project content leaving the
machine on every request, for every user, including one who disabled all three Rework Functions.
The §8.4 guarantee would be false in the very configuration that is supposed to establish it.

§8.4 is silent on how the model names which Pattern. This resolves the silence in the direction
that keeps §8.4's own words literally true. See *Open items* 2.

### D5 — Functions return Deltas; they never write

A Function computes a change and hands it back. The history module (#7) applies it and is the only
writer. §9.7 holds because there is no write path that bypasses the undo history — Functions have
no write path at all. `Delta.origin` records `Assistant { function }`, which is what feeds
provenance (§6.3).

### D6 — The Function file is a pure projection of one registry

One compile-time `Registry` of `FunctionDescriptor` is the single source of truth. The switchboard
UI, the outbound tool definitions, the dispatch table and the spec catalogue are all readings of
that same list — a Function that exists but is missing from the switchboard is not possible.

```
build(registry, toggles, audience, capabilities, dialect) -> FunctionFile
```

Pure. No I/O, no clock, no globals — same inputs, same bytes, testable without a provider, a
Project or a UI. Three filters in order: audience, then capabilities, then user toggles. A dropped
Function is **absent**: no stub, no disabled marker, no error string, so the model is never informed
that a capability exists and is being withheld.

The file is bound at turn start and immutable for that turn; a toggle flipped mid-turn takes effect
next turn. A Sub-agent's file is built by the same builder with a narrower audience filter and is
**always a subset** of the Assistant's — otherwise the switchboard would be bypassable by
delegation. Sub-agents are an implementation detail behind a Function, never a Function of their
own.

Schemas are emitted `strict: true`, `additionalProperties: false`, all properties required
(Appendix B.4). Forbidden argument shapes — open objects, JSON-in-a-string, anything named `patch`
or `payload`, an `op` enum switching between unrelated effects — are listed in spec §2.2, because
each is `mutate_project(json)` smuggled through a parameter.

## Alternatives rejected

- **A single `mutate_project(json)`, or a small set of coarse Functions.** Rejected by §3.8 and
  §9.2 by name. Recording *why*, because the pull toward it is real and will recur: it cannot be
  classed Directive or Rework, since whether it transmits depends on the argument rather than on
  the Function; it cannot be toggled meaningfully, since switching it off removes everything; its
  schema cannot be closed, so B.4's strict-argument guarantee is forfeited; and its blast radius is
  the whole Project on every call, which forfeits the entire "what it does not touch" column.

- **A per-area Function with an `op` enum** — `edit_pattern(op, ...)`, `edit_channel(op, ...)`.
  Rejected. This is the previous alternative wearing a narrow coat. The enum becomes the union of
  every caller's convenience; per-op fields cannot be expressed under a strict closed schema
  (different ops need different required keys); and the user cannot switch off *transposing* while
  keeping *quantising*, so §3.10 degrades to an all-or-nothing switch per area.

- **A per-field Function** — `set_note_pitch`, `set_step_on`. Rejected as the opposite error.
  Narrowness is blast radius, not field size. A sixteen-step hi-hat lane would cost sixteen calls
  and sixteen deltas, and the undo history would become unusable at exactly the moment the
  Assistant did something the user wanted to undo (§3.9). Note that this failure mode makes the
  case *for* the grain rule, not merely against this option.

- **Enforcing §9.1 with an allowlist check at call time** — validating that a Function only touched
  permitted paths. Rejected in favour of D2. A check must be written correctly, kept in sync as the
  data model grows, and covered by tests; a type graph with no reference to settings needs none of
  that. The check would also be the kind of code that gets a `// TODO: relax this for X` comment
  the first time it blocks a legitimate feature.

- **Refusing disabled Functions at call time instead of rebuilding the file.** Rejected by §9.3
  explicitly. Beyond compliance: a refusal path teaches the model that the capability exists and is
  being withheld, which produces an Assistant that argues with the user about their own settings —
  and, since the user is never shown which Function ran (NON-scope 6), argues about something the
  user cannot see. Removal leaves nothing to argue about.

- **Class as a runtime metadata flag** — `descriptor.class == Rework`, checked before transmitting.
  Rejected in favour of D3. Functionally equivalent on the happy path and strictly weaker off it: a
  mis-set flag on a new Function silently transmits Project content, and nothing in the type system
  notices. The failure is silent, privacy-affecting, and exactly the kind a busy contributor
  introduces.

- **Sending a Project index — Pattern and Track names and ids — as addressing context.** Rejected
  by D4. It is the conventional design and it is the one that quietly breaks the project's most
  distinctive promise: it puts user-authored names and Project structure on every request, so
  "disable Rework and nothing leaves" becomes false for every user, and the issue's own acceptance
  criterion could not be met. The design cost is real (occasional local disambiguation); the
  alternative's cost is the guarantee itself.

- **Ids in Function arguments, obtained from an earlier turn.** Rejected for the same reason, plus
  a second: it makes every Function's correctness depend on conversation state, so a stale id from
  three turns ago mutates the wrong Pattern.

- **Functions that mutate the Project directly, with the history module observing.** Rejected in
  favour of D5. Observation can miss a write; a Delta cannot be missed because it *is* the change.
  §9.7 is the only remedy the user has (NON-scope 6 hides which Function ran), so it must not
  depend on complete instrumentation.

- **A separate registry for Sub-agents.** Rejected by D6. Two registries means the switchboard
  governs one of them, and §9.3 becomes false by delegation rather than by intent.

- **`rework_arrangement` / a whole-Project Rework Function.** Rejected: it puts the entire
  Arrangement on the wire and sits on NON-scope 1. Named in spec §5.10 so its absence is a recorded
  decision rather than a gap a later contributor closes helpfully.

## Consequences

- **`generate_pattern` and `generate_sample` are Directive.** They compose from the user's prompt,
  not from the Project. So a user who disables all three Rework Functions keeps AI generation *and*
  keeps the §8.4 guarantee — the privacy floor and the generative feature are independent. That
  falls out of where the class line sits; it was not designed for.
- **The switchboard's local-only banner is derived from the built file** (`rework_count == 0`), not
  from toggle state, so it cannot disagree with what is actually sent.
- **Honest accounting:** `generate_sample` sends the user's prompt to a *second* provider (§6.4).
  The §8.4 guarantee is about Project content and holds exactly as worded, but the switchboard must
  name the provider per row (O-17.2) so this is visible rather than inferred.
- **Provenance needs two flags, not one** (O-19.1). `ai_origin` (§6.3, the visual mark) applies to
  anything an Assistant Delta created; `generative_output` (§6.2, the licence caveat) only to
  generative-model output. An Assistant-built drum lane is AI-authored but is not model-generated
  audio, and must not carry the licence warning.
- **`Channel` is load-bearing and is not in the §5 vocabulary.** Eight Functions name it. See *Open
  items* 1.
- **43 Functions is a lot of schema to emit per request.** Deliberate — it is what §3.8 asks for —
  but it is a real token cost per turn, and one worth measuring once #4 picks a provider.
- Six issues acquire obligations (spec §8). #5 in particular must make the `MusicalContent` split
  structural rather than a naming convention, or D2 is not true.
- No implementation exists yet, so `improve-codebase-architecture` had nothing to scan. Its
  vocabulary — depth, seam, adapter, the deletion test — is what the design above is argued in.

## Open items — require the project owner

1. **`set_channel_instrument` vs `change_instrument`, and `Channel` as vocabulary.** §3.8's worked
   example is called `change_instrument`. This spec renames it for grammar consistency (spec §2.1)
   and because its object is a **Channel** — a term §5's *binding* vocabulary does not define, while
   eight Functions depend on it. Confirm the rename and add `Channel` to §5, or the surface names
   something the binding vocabulary does not.
2. **The Selector decision (D4) is the one place this design answers a question §8.4 left open.**
   The owner should see the trade rather than inherit it: Selectors keep §8.4 literally true and
   make the round-7 guarantee real, at the cost of the Assistant occasionally being unable to tell
   two similarly-named Patterns apart, where the Studio asks the user locally. Recommended, not
   assumed.
3. **This design is frozen on merge and three architecture issues depend on it** (#5, #7, #16). Per
   the architect's rule, if D2 or D4 turns out to be wrong, the dependent work stops and this ADR is
   amended deliberately — it does not drift.
