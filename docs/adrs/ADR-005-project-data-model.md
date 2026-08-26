# ADR-005 — The Project data model

- **Status:** Proposed. Freezes on merge to `dev`.
- **Date:** 2026-08-23
- **Issue:** #5 — Design the Project data model
- **Decided by:** Architect agent; requires project-owner ratification, see *Open items*
- **Supersedes:** nothing
- **Superseded in part by:** [`ADR-062`](ADR-062-provenance-two-flags.md), 2026-08-26 — Provenance
  only. Contract §4 now has three authorship states, so *Worked example 2* below reads
  `Generated { source, at }` where the model now says
  `{ authorship: GenerativeModel, source, at }`. Every case in that example still holds; only the
  shape of the value changed. Nothing else in this ADR is affected.
- **Contract:** [`docs/specs/project-data-model.md`](../specs/project-data-model.md)

This ADR records *why*. The spec records *what*, and is normative. Where they disagree, the spec
is wrong and needs a superseding ADR — not a quiet edit.

## Context

`docs/CORE_DOCUMENT.md` §5 fixes the vocabulary. Five requirements shape the model, and four of
them are stated in the core document as things that must never happen:

1. A **Pattern** covers drum steps and melody notes. One concept (§5).
2. An **Arrangement** assembles Patterns onto **Tracks** (§3.7, §5).
3. Every **Sample** and every **Pattern** carries AI provenance (§6.3, §9.4).
4. Every Assistant mutation touches exactly one thing (§3.8, §9.2).
5. Changes are reversible **deltas**, not snapshots (§3.9, §9.7).

Requirements 4 and 5 are the load-bearing pair. The owner's own sentence is the test:

> "if a user asks the agent to swap the instrument on a melody, the agent should have a mutation
> call that does only that, without otherwise touching the datastructure or pattern."

A model that cannot *demonstrate* what a mutation did not touch has not satisfied that sentence,
it has only promised to.

## Decision

The full contract is in the spec. Six decisions carry it.

### D1 — The Project is Id-addressed, not a nested document

Every entity has an opaque, stable `Id`. Ownership is a tree; cross-tree references are by `Id`.
Position in a collection is never an address.

This is the decision everything else falls out of. If a mutation had to be addressed by path
(`arrangement.tracks[2].placements[5].start`), then "touches exactly one thing" would be a claim
about a path that reordering invalidates, and a delta would have to encode tree structure. With
`Id` addressing, a mutation *is* `(entity, field, before, after)`. Requirements 4 and 5 stop being
two problems and become the same one.

### D2 — Part: the addressable thing a "melody" is

A **Pattern** owns ordered **Parts**; a Part owns **Events** and holds one `instrument` reference.

A Pattern must span several Instruments — the step sequencer shows kick, snare and hat in one
Pattern (§3.7, and §1.5's "copy what FL Studio does"). So something inside a Pattern has to carry
the Instrument. Putting that reference on each Event instead would make `change_instrument` an
N-Event rewrite with no single object to point at, which fails requirement 4 outright.

Deletion test: delete Part and the complexity does not vanish, it spreads — Instrument identity
smears across every Event, `change_instrument` becomes O(N), and "the melody" stops being a thing
you can name. Part earns its keep.

### D3 — Everything on a Track is a Placement of a Pattern

There is no second placement type for audio. A Sample reaches audio only through a `Sampler`
Instrument, so a recorded take becomes Sample → Instrument → Part → Pattern → Placement.

This keeps the Arrangement's interface one concept wide. Move, trim, loop, duplicate, delete and
their deltas are written once. The alternative — a union of pattern-clips and audio-clips on a
Track — widens the interface to suit one caller, and every consumer branches on it forever.

### D4 — Provenance is a value, and it only ever accumulates

`Provenance = Human | Generated { source, at }`, on `Sample` and `Pattern` only.

It is never silently cleared, and the enforcement is *omission*: an ordinary edit's Delta contains
no Edit to the `provenance` field, so a human touching a generated Pattern leaves it generated
without any code deciding to. §9.4 is satisfied by the shape of the data rather than by a rule
somebody has to remember.

Project-level "does this contain AI content?" is computed from the reference graph, never stored.
A stored rollup would go stale, and — worse — would force a one-thing mutation to touch two things,
breaking requirement 4 in order to serve requirement 3.

### D5 — `ProjectHistory` is the only door

```
ProjectHistory: read() | apply(Delta) | undo() | redo()
```

Entities expose no public mutators. Four operations hide identity allocation, referential
integrity, atomicity, delta inversion and redo-tail truncation.

**History sits above the Project, not inside it.** That placement is deliberate: the Assistant has
complete reach over musical content (§3.8) and must still be unable to touch the record of what it
did. §9.7 — *no Assistant action may be irreversible* — stops being a policy and becomes a
consequence of where the seam is.

This is a real seam by the two-adapter test: user edits from the editing surfaces and Assistant
edits from Functions both arrive as Deltas, through the same door. And the interface is the test
surface — every musical mutation is testable by building a Delta and asserting the state plus the
round-trip, with no audio engine in the loop.

### D6 — One Function, one Delta

```
Delta = { id, actor: User | Assistant, at, edits: [Edit] }
Edit  = Set{target, field, before, after} | Insert{...} | Remove{...}
```

Three Edit kinds, each self-inverting. `inverse(Delta)` reverses the list and inverts each Edit.
Atomicity is what lets a compound operation still be one Delta: deleting an Instrument carries its
`Remove` *and* the Edits that resolve every reference to it, so the inverse restores all of them
together and I8 (no dangling references) holds after undo as well as after do.

### Frozen quantities

Integer `Ticks` at PPQ 960; `Pitch` and `Velocity` integers 0–127. Floating-point time would break
invariant I4 (`undo(apply(p, d)) == p`, bit-for-bit) the first time a delta round-tripped. 960
factors as 2⁶·3·5, so triplets and quintuplets are exact.

## Worked example 1 — `change_instrument`, the owner's case

The user has an 8-bar melody: Pattern `P` containing Part `B` (32 Events) targeting Instrument
`piano`, plus Part `D` (drums) targeting Instrument `kit`. `P` is placed four times across two
Tracks. The user says *"swap the melody to the bass"*.

```
change_instrument(part = B, instrument = bass)

Delta {
  actor: Assistant,
  edits: [ Set { target: B, field: "instrument", before: piano, after: bass } ]
}
```

**One Edit. One field. One entity.**

What it demonstrably does not touch — and "demonstrably" is the point, because the Delta is the
complete record of the change:

| Not touched | |
|---|---|
| Any of the 32 Events | no pitch, timing, duration or velocity moved |
| Part `B`'s own identity, mute state, Event ordering | |
| Part `D` | the drums in the same Pattern are untouched |
| Pattern `P` | name, length, provenance, Part order all unchanged |
| All four Placements | they still reference `P`; the Arrangement is not rewritten |
| Instruments `piano` and `bass` | their own definitions, params and effect chains are unchanged |
| Any Sample, any Effect, any setting | |

Three properties worth naming:

- **The change is heard in all four Placements from one Edit.** Because the Arrangement references
  Patterns rather than baking audio, a single field write propagates everywhere the Pattern is
  used. That is the leverage the reference model buys.
- **This is a Directive Function (§8.4).** The call is two `Id`s. There is nothing in it for a
  model to read, so no Project content leaves the machine — structurally, not by promise.
- **Undo is the same Edit, swapped.** `Set { target: B, field: "instrument", before: bass, after: piano }`.

Contrast: a `mutate_project(json)` Function would produce a Delta whose Edit list is the whole
Project. It would be *one permission* and still violate §9.2, which is exactly the trap §3.8's note
warns about.

## Worked example 2 — provenance through a mutation

Pattern `P` is `Human`: one Part, 8 Events. The user says *"make this jazzier"* — a **Rework**
Function, so the Pattern's Events are sent to the model (§8.4). It returns 11 Events.

```
rework_pattern(pattern = P, prompt = "jazzier")

Delta {
  actor: Assistant,
  edits: [
    Remove { owner: B, collection: "events", index: 0..7,  entity: <snapshots of e1..e8> },
    Insert { owner: B, collection: "events", index: 0..10, entity: <snapshots of n1..n11> },
    Set    { target: P, field: "provenance",
             before: Human,
             after:  Generated { source: "<model id>", at: <t> } }
  ]
}
```

Still **one Delta**, so it is atomic: there is no state in which the Pattern has generated Events
but still claims to be `Human`. The provenance mark and the content it describes cannot separate.

Then the four cases that decide whether the model is right:

| Case | Result | Why |
|---|---|---|
| **Undo the rework** | `P` is `Human` again, with its 8 original Events | Provenance is ordinary data in the Delta, so it is restored like any other field. It is *not* recomputed — recomputing would leave the Pattern permanently marked after its generated content was removed. |
| **User then hand-edits two notes of the jazzier version** | `P` stays `Generated` | The edit's Delta contains Events only. Nothing writes `provenance`, so nothing clears it (P2). §9.4 holds without anyone remembering it. |
| **User duplicates `P`** | The copy is `Generated` | P3. Copying does not launder. |
| **A `Human` Pattern's Part points at an Instrument playing a `Generated` Sample** | Pattern stays `Human`, Sample stays `Generated`; the interface shows both marks | P6. Provenance is per-entity; §6.3 asks for a mark on the Sample *and* on the Pattern, not for one to infect the other. |

And "does this Project contain AI content?" is answered by walking the reference graph — which is
why it must not be stored: in the last row the answer is *yes* even though no Pattern is marked.

## Alternatives rejected

**Model shape**

| Rejected | Why |
|---|---|
| Separate `DrumPattern` and `MelodyPattern` types | Requirement 1 forbids it, and the reason is structural: the difference is a *view* concern (step grid versus pitch axis), not a data one. Two types would make every consumer — Arrangement, Functions, undo, provenance, both editors — branch forever. |
| Pattern holds Events directly; `instrument` on each Event | `change_instrument` becomes an N-Event rewrite and "the melody" has no address. Fails requirement 4 and the owner's sentence. |
| Pattern is single-Instrument (a Part *is* a Pattern) | A kick/snare/hat beat would need three Patterns and three Placements kept in sync by hand. Contradicts §3.7's step sequencer and §1.5. |
| Naming the intra-Pattern group **Lane** | §5 defines Track *as* "one lane in the Arrangement". Reusing "lane" inside a Pattern manufactures the exact synonym defect §5 exists to prevent. **Part** is a standard musical term with no §5 collision. |
| Naming a Placement **Clip** | FL's word, and §1.5 says copy FL — but "clip" is overloaded with *audio clip*, and D3 exists precisely to deny that there is such a thing. |
| A union placement type on Track (pattern clips + audio clips) | Widens the Arrangement's interface to unblock one caller. **Cost of the choice made instead, stated honestly:** a plain recording carries an Instrument + Part + Event wrapper it does not conceptually need. That wrapper is created by the recorder, not by the user, and it buys one placement concept everywhere else. |
| Part references `Sample` **or** `Instrument` (a union) | Two reference kinds means two code paths at every consumer. A Sample reaches audio through a `Sampler` Instrument; the union buys nothing. |
| Track-level effect chains in the MVP | That is the Mixer, which is post-MVP (§3.6). The effect-chain concept is defined so the Mixer becomes a third holder rather than a contract change. |

**Mutation and history**

| Rejected | Why |
|---|---|
| Nested document with path-addressed mutations | Paths are invalidated by reordering, and a path-shaped delta cannot demonstrate that it touched one thing. See D1. |
| Snapshot-based undo | §3.9 says deltas explicitly. Snapshots also make *"what did the Assistant just change?"* unanswerable, and since §8.4 hides the tool-call log, that question has no other answer. |
| Tree / branching undo history | §3.9 is explicit: the Word model, discard the future. |
| Deltas without stored `before` values | Undo would have to replay from the origin. Storing `before` costs memory proportional to what was edited and makes every Edit self-inverting. |
| History stored inside the Project | Would put the record of the Assistant's actions inside the Assistant's territory. See D5. |
| Float seconds or float beats for time | Breaks invariant I4 on the first round-trip. |

**Provenance**

| Rejected | Why |
|---|---|
| A plain boolean | §6.2's warning and §6.4's handpicked-model list both depend on *what* generated the content. A tagged value keeps the boolean derivable at no cost. |
| A stored Project-level AI rollup | Goes stale, and forces one-thing mutations to touch two things. Compute it. |
| Provenance on Part, Event, Track or Project | §6.3 names Sample and Pattern. Anything more is interface widening with no requirement behind it. |
| Clearing provenance after substantial human editing | There is no defensible threshold, and §9.4 is unconditional. |

**Naming and process**

| Rejected | Why |
|---|---|
| Branded type names, `Id` prefixes, or a magic number containing the product name | §8.1. The name is deliberately disposable and must stay cheap to dispose of. `format_version` is a number. |
| A separate `CONTEXT.md` domain glossary | §5 is already binding. A second copy is a place for the vocabulary to drift, which is the defect §5 exists to prevent. The spec extends §5 in one table and points back to it. |
| Typed per-Effect parameter structs in the data model | Would couple the Project contract to DSP details and make ADR-001's implementation picks contract changes. The model freezes the container; the Effect spec owns the parameter schemas. |

## Consequences

- **Requirements 4 and 5 are the same mechanism.** `Id` addressing plus `Set/Insert/Remove` means a
  Function that touches one thing produces a Delta that *proves* it, and the proof inverts for free.
- **§9.7 and §9.2 are structural, not procedural.** No reviewer has to check that an Assistant
  action is undoable; there is no way to write one that is not.
- **Referential integrity needs enforcing.** Normalisation is what buys D1, and the price is that
  `apply` must reject or resolve dangling references (I8). That work lives in `ProjectHistory`,
  which is the right place for it: one implementation, every caller.
- **The MVP has no synthesiser.** `Sampler` is the only `Instrument.kind`, so every MVP sound comes
  from a Sample, including melodies played in the piano roll. Consistent with §3.5 and Appendix
  A.2, but it is a product implication the owner should see stated — see *Open items*.
- **Serialisation is constrained but not decided.** §9.6 pressures `Sample.source` toward embedding
  audio rather than linking it. Recorded in the spec so the serialisation issue inherits it.
- **The Function surface is constrained but not decided.** One Function, one Delta; Directive
  Functions take `Id`s and scalars only.
- **The Mixer and Plugin support do not require a contract change.** Both have named attachment
  points: another effect-chain holder, another `Instrument.kind` / `Effect.kind`.
- **This contract is frozen on merge.** Feature worktrees may open against it. If it turns out to be
  wrong, the work that depends on it stops and a superseding ADR changes it deliberately.

## Open items — require the project owner

1. **No synthesiser in the MVP.** The consequence of `Sampler` being the only Instrument kind is
   that piano-roll melodies play pitched Samples. This follows from §3.5 as written, but §3.5 never
   says it out loud. If the owner expects a synthesiser in the MVP, that is a core-document change,
   not a model change — a second `Instrument.kind` slots in without touching this contract.
2. **Recording ceremony.** D3 routes recorded audio through an Instrument and a Pattern. Confirm the
   uniformity is worth the wrapper, or accept the union type with its permanent cost at every
   consumer.
3. **One Arrangement per Project in the MVP.** §3.7 and §5 both read singular. Stored as a map with
   one entry so multiple Arrangements are later an addition, not a break. Confirm singular is
   intended.
4. **Automation is not in this contract.** §3.5 does not list it, so it is named as an extension
   point and not designed. Confirm it is genuinely post-MVP; if not, it belongs in this model rather
   than bolted on later.

## Note on ADR-001

ADR-001's open item 1 — *"`docs/CORE_DOCUMENT.md` is an empty template"* — no longer holds. The
document is populated on `dev` at 642 lines, and §3.2, §3.4, §6.1 and Appendix B.1 all exist as
ADR-001 assumed. Its licence conclusions were taken from the issue text and match the core document
as written. ADR-001's open items 2 and 3 are untouched by this ADR.
