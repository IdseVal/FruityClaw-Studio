# Spec — The Function surface and Function file generation

- **Status:** Frozen contract, pending owner ratification of the two items in *Open items*.
- **Date:** 2026-08-23
- **Issue:** #6
- **Decision record:** [ADR-006](../adrs/ADR-006-function-surface-and-function-file.md)
- **Derived from:** `docs/CORE_DOCUMENT.md` §3.8, §3.10, §4, §5, §8.4, §9

This spec defines what the Assistant may do, the shape of the instruments it does it with, and
the machinery that decides which instruments it is handed. It is the contract that issues #16
(Assistant panel and Function execution) and #17 (Function switchboard) build against.

---

## 1. The reach rule

> Complete reach over the musical content. No blunt tools. No settings. — §3.8

A Function may address an object **iff** both hold:

1. the object is persisted in the **Project** document, and
2. the object is part of the Project's **musical content** — the material that determines what
   the Project sounds like.

Everything else is out of reach: Studio settings, provider credentials, the Function toggle store,
view state (zoom, scroll, window layout), the Project's own file path, and the filesystem.

**This is enforced by construction, not by a check.** Every Function is handed exactly one object,
`MusicalContent`, a read-only view rooted at the musical subtree of the open Project. Nothing else
is in scope. There is no reference from `MusicalContent` to settings, to credentials, to the toggle
store, or to a filesystem path — so there is nothing for a Function to reach *through*. §9.1 is not
a rule the Assistant is asked to respect; it is a shape of the type graph.

```
Project
├── musical/            ← MusicalContent: the ONLY thing a Function ever receives
│   ├── patterns/           note and step events
│   ├── channels/           instrument, sample reference, gain, pan, mute
│   ├── arrangement/        tracks, clips
│   ├── effect_chains/      the six stock Effects (§3.4) and their parameters
│   └── frame/              tempo, time signature, key
├── view/               ← unreachable: zoom, scroll, selection, panel layout
└── meta/               ← unreachable: file path, save state, provenance index

StudioSettings          ← unreachable: audio device, provider keys, model choice,
                          generation enablement, and the Function toggles themselves
```

**The settings valve is one-way.** Settings flow *into* the Function file — the toggles decide what
is built (§4). Settings never flow *to* a Function. The switchboard reads its own state; no Function
can read the switchboard.

---

## 2. Function anatomy

### 2.1 Naming grammar

`verb_object`, snake_case. The verb comes from a closed set: `create`, `delete`, `rename`,
`duplicate`, `add`, `remove`, `clear`, `set`, `move`, `place`, `resize`, `transpose`, `quantize`,
`generate`, `rework`, `continue`, `describe`. A name that needs a verb outside this set is a
signal that the Function is doing more than one thing.

### 2.2 Argument schemas are closed

Every Function's schema is emitted with `strict: true`, `additionalProperties: false`, and every
property in `required` (Appendix B.4). Optionality is expressed as a nullable union, never as an
absent key.

**Forbidden argument shapes**, because each is `mutate_project(json)` smuggled in through a
parameter:

- an `object` argument with open or unenumerated properties;
- a `string` argument the Studio parses as JSON, a patch, a script or an expression;
- any argument named `json`, `patch`, `data`, `payload`, `ops` or `body`;
- an `op`/`action`/`kind` enum that switches the Function between unrelated effects.

Enumerated domains are closed at schema level. `add_effect` takes an enum of exactly the six
Effects of §3.4 — the model cannot name a seventh, and cannot name a **Plugin** (§5: third-party,
post-MVP), because the string is not in the schema.

### 2.3 Addressing: Selectors, not identifiers

A Function argument that designates a Project entity is a **Selector**, never an id.

```
Selector =
  | Focused                    the entity the user currently has open or selected
  | Ordinal(kind, n)           "track 3", "the second pattern"
  | Named(text)                the user's own words, echoed back from their prompt
  | LastCreated(kind)          the entity the previous Function in this turn created
```

**Why this matters more than it looks.** An id-based argument would require the model to have been
*told* the Project's identifiers — which is Project content leaving the machine, on every request,
for every user, including users who disabled every Rework Function. Selectors remove that
requirement entirely. `Named` works because the user already said the name in their own prompt
("make the bassline jazzier"); the model echoes the user's words, and the **Studio** matches them
against the real Project, locally.

Resolution is a deep module with a narrow interface:

```
SelectorResolver::resolve(Selector, MusicalContent) -> Result<EntityRef, Unresolved>
```

`Unresolved` is `NotFound` or `Ambiguous(candidates)`. **Neither is answered by asking the model.**
The Studio renders a local disambiguation control in the Assistant panel — the candidate names are
shown to the *user*, who already knows them — and re-runs the Function with the resolved `EntityRef`.
No second request is made, and no candidate list is transmitted. See obligation O-16.2.

### 2.4 Every Function returns a Delta

```
DirectiveFn = fn(Args, MusicalContent) -> Result<Delta, FunctionError>
```

A Function **computes** a change; it does not **perform** one. The returned `Delta` is applied by
the history module (issue #7), which is the only writer. §9.7 — "No Assistant action may be
irreversible" — therefore holds by construction: there is no write path that bypasses the undo
history, because Functions have no write path at all.

```
Delta {
  ops:    [Op],
  origin: Human | Assistant { function: FunctionName },
}
```

`origin` is what feeds provenance (§6.3). See obligation O-19.1.

---

## 3. The two classes

§8.4 fixes two classes. **The class is not a label on a Function; it is a difference in type.**

```
DirectiveFn = fn(Args, MusicalContent) -> Result<Delta, FunctionError>

ReworkFn = {
  collect: fn(Args, MusicalContent) -> Excerpt,
  apply:   fn(Args, ModelReply, MusicalContent) -> Result<Delta, FunctionError>,
}
```

A Directive Function **has no `collect` member**. Not an empty one, not one that returns nothing —
it does not have the member. There is no code, anywhere, that can produce Project content on behalf
of a Directive Function, so no review, no test and no runtime check is needed to establish that one
never transmits. A label can be wrong; a missing member cannot.

### 3.1 What goes out

```
OutboundRequest =
  | Turn1       { prompt: UserPrompt, tools: FunctionFile }
  | Turn2Rework { excerpt: Excerpt, tools: FunctionFile, source: FunctionName }
```

`Excerpt` has exactly one constructor and it is private to the rework module: `ReworkFn::collect`.
`Turn2Rework` cannot be built without an `Excerpt`, and an `Excerpt` cannot be built without a
Rework Function having been selected.

### 3.2 The Rework flow is two turns

This is the part that is easy to get wrong, so it is stated explicitly:

```
turn 1   OUT  { prompt: "make the bassline jazzier", tools: <function file> }
              ── no Project content ──
         IN   tool_use: rework_pattern { target: Named("bassline"), instruction: "jazzier" }

         LOCAL  SelectorResolver resolves Named("bassline") -> pattern:7
         LOCAL  rework_pattern.collect(pattern:7) -> Excerpt { 34 note events, 4 bars }

turn 2   OUT  { excerpt: <the 34 note events>, tools: <function file> }
              ── Project content leaves the machine HERE, and only here ──
         IN   replacement note events

         LOCAL  rework_pattern.apply(...) -> Delta -> history
```

**Turn 1 never carries Project content, for either class.** Transmission is a distinct, later,
attributable step taken by one named Function. That is what makes §8.4's promise checkable rather
than aspirational: there is exactly one place in the Studio that can put Project content on the
wire, and it is reached only from `Turn2Rework`.

### 3.3 Rework Functions are rationed

Every Rework Function is a hole in the privacy floor of §8.4. The MVP has **three**, and this is a
budget, not a coincidence.

**Governance rule:** adding a Rework Function requires an amendment to ADR-006. Adding a Directive
Function does not. The asymmetry is deliberate — Directive breadth is the point of §3.8, Rework
breadth is a cost.

### 3.4 Sub-agents are not Functions

§1.1 permits the Assistant to delegate to **Sub-agents**. A Sub-agent is an *implementation detail
behind* a Function — `generate_sample` may run one — and is **never a Function of its own**. There
is no `delegate_to_subagent`, because a Function whose effect is "whatever the Sub-agent does" is
exactly the blunt tool §3.8 forbids.

Where a Sub-agent is itself given a Function file, that file is built by the same builder with a
narrower audience filter (§4.2). **A Sub-agent's Function file is always a subset of the
Assistant's, never a superset.** Without this rule the switchboard would be bypassable by
delegation, and §9.3 would be a lie.

---

## 4. Building the Function file

### 4.1 The registry

All Function metadata lives in **one** place: a compile-time `Registry` of `FunctionDescriptor`.

```
FunctionDescriptor {
  name:        FunctionName,
  class:       Directive | Rework,
  audience:    { assistant: bool, subagents: [SubagentKind] },
  requires:    [Capability],       // e.g. MusicGenerationEnabled
  schema:      ClosedSchema,
  instruction: str,                // the §8.4 "instruction appended by the selected Function"
  effect:      str,                // the single thing it does — rendered in the switchboard
  impl:        DirectiveFn | ReworkFn,
}
```

There is no second list. The switchboard UI, the outbound tool definitions, the dispatch table and
this spec's catalogue are all projections of the registry. A Function that exists but is missing
from the switchboard is not possible, because they are the same list read twice.

### 4.2 The builder

```
build(registry, toggles, audience, capabilities, dialect) -> FunctionFile

FunctionFile {
  entries:       [ToolDefinition],   // in registry declaration order
  manifest_hash: Hash,
  rework_count:  usize,
  audience:      Audience,
}
```

Pure: no I/O, no clock, no globals. Same inputs, same bytes — so the file is cacheable, diffable,
and testable without a provider, a Project or a UI.

The filter, applied in this order:

| # | Predicate | Drops |
|---|---|---|
| 1 | `audience_allows(entry, audience)` | Functions this audience never gets (§3.4 subset rule) |
| 2 | `capabilities.satisfied(entry.requires)` | Functions whose prerequisite is unconfigured — e.g. both generation Functions when §6.4 generation is off |
| 3 | `toggles.enabled(entry.name)` | Functions the user switched off (§3.10) |

**Removal, not refusal.** A dropped Function is absent from `entries`. There is no "disabled"
marker, no stub, no error string — the model is never informed that a capability exists and is
withheld, so it cannot argue with the user about it. §9.3 satisfied.

The one call-time name check that does exist is an **integrity** check, not a permission check: if
an inbound tool-use names something not in the file that was sent, the reply is malformed, and the
turn is ended with an internal error. It is a backstop against a corrupted or hallucinated response,
and it never fires as part of normal toggle behaviour — nothing routes through it, because nothing
disabled was ever offered.

### 4.3 Defaults in the toggle store

- A Function name absent from the store defaults to **enabled**.
- **Exception:** a *Rework* Function absent from the store defaults to **disabled** whenever the
  store already has any Rework Function disabled. A Studio update must never silently re-open a
  privacy floor the user closed. A fresh install has no disabled Rework Functions, so a fresh
  install gets all three.

### 4.4 Turn binding

The Function file is **bound at the start of a turn and immutable for that turn**; its
`manifest_hash` is recorded on the turn. A toggle flipped mid-turn takes effect on the next turn.

Without this the model could be offered a Function in turn 1 and have it vanish before turn 2 of a
Rework flow — which would surface to the user as the Assistant failing at something it had just
said it would do.

A toggle write bumps the store version, which invalidates the cached file. The rebuild happens at
the next turn start, not at the moment of the click.

### 4.5 The provider dialect seam

The registry holds a provider-neutral `ClosedSchema`. A thin `ToolDialect` adapter renders it into
the wire format.

Today there is one adapter (Claude strict tool use, Appendix B.4), which by the two-adapter rule is
a hypothetical seam. It is declared anyway because open question 4 (§7, issue #4) may add adapters,
and the seam is where the floor-setting risk B.4 names becomes visible: **a dialect that cannot
guarantee schema-exact arguments lowers what the Studio can promise about every Function.** The
adapter interface therefore requires each dialect to declare whether it enforces closed schemas, so
the answer is a property of the code rather than a footnote. See obligation O-4.1.

---

## 5. The Function catalogue

**43 Functions: 40 Directive, 3 Rework.**

### 5.1 Invariants that hold for every Function

These are not repeated per row. The *Locally does not touch* column names only what is nearby and
might plausibly have been included — that is where the information is.

| # | Invariant |
|---|---|
| I1 | Cannot reach Studio settings, provider credentials, the toggle store, view state, the Project's file path, or the filesystem. No reference exists in `MusicalContent` (§1). |
| I2 | Returns a `Delta`; never writes. Every effect is undoable (§9.7). |
| I3 | Touches exactly the entities its resolved Selectors name — and no sibling, parent or child not named. |
| I4 | Never creates or deletes an entity, unless creation or deletion is its stated single effect. |
| I5 | Never changes an entity's identity. A rename changes a display name; it does not re-key anything. |
| I6 | If Directive: has no `collect`, and therefore no ability to transmit Project content (§3). |

### 5.2 Pattern lifecycle

| Function | Class | Single effect | Locally does not touch |
|---|---|---|---|
| `create_pattern` | D | Creates one empty **Pattern** of a given kind (step or note) and length | Does not place it in the **Arrangement**, assign it a **Channel**, or write any event into it |
| `delete_pattern` | D | Removes one Pattern | Does not remove the clips that referenced it — those are orphaned and reported, so a deletion cannot silently rewrite the Arrangement |
| `rename_pattern` | D | Changes one Pattern's display name | No events, no identity, no clip |
| `duplicate_pattern` | D | Creates one copy of one Pattern | Does not place the copy anywhere; does not modify the original |
| `set_pattern_length` | D | Changes one Pattern's length in bars | Does not move, scale or quantise the events inside it; events beyond the new length are retained and reported, not dropped |

### 5.3 Step events — drum Patterns (§3.7)

| Function | Class | Single effect | Locally does not touch |
|---|---|---|---|
| `set_channel_steps` | D | Writes the on/off step grid for **one Channel** in **one Pattern** | No other Channel's lane, no other Pattern, no velocity values |
| `set_channel_step_velocities` | D | Writes velocities for one Channel's lane in one Pattern | Does not turn steps on or off |
| `clear_channel_steps` | D | Empties one Channel's lane in one Pattern | No other lane; does not delete the Channel |

**On grain.** The unit of a Function's effect is the smallest musical object a user would *name* —
a lane, a note, a clip — not the smallest field. A per-step Function would make an ordinary
sixteen-step lane cost sixteen calls, which is not narrowness, only friction. A lane is one thing:
it is what "give the hi-hats a sixteenth pattern" means.

### 5.4 Note events — melody Patterns (§3.7)

| Function | Class | Single effect | Locally does not touch |
|---|---|---|---|
| `add_notes` | D | Adds note events to one Pattern | Does not remove or alter existing notes; does not change Pattern length |
| `remove_notes` | D | Removes named note events from one Pattern | Does not alter surviving notes |
| `clear_pattern_notes` | D | Empties one Pattern of notes | Does not delete the Pattern, its length, or its Channel binding |
| `set_note_velocities` | D | Sets velocity on named notes in one Pattern | No pitch, no timing, no duration |
| `transpose_pattern` | D | Shifts every note in one Pattern by a semitone interval | No timing, no velocity, no other Pattern — including copies made by `duplicate_pattern` |
| `quantize_pattern` | D | Snaps note start times in one Pattern to a grid | No pitch, no velocity, no duration |

### 5.5 Channels and Instruments

| Function | Class | Single effect | Locally does not touch |
|---|---|---|---|
| `create_channel` | D | Adds one Channel | Does not assign an **Instrument** or **Sample**, and writes nothing into any Pattern |
| `delete_channel` | D | Removes one Channel | Does not delete the Patterns that used it; their lanes for that Channel are orphaned and reported |
| `rename_channel` | D | Changes one Channel's display name | Nothing else |
| `set_channel_instrument` | D | Points one Channel at a different **Instrument** | **No note or step data anywhere.** This is §3.8's worked example: "a mutation call that does only that, without otherwise touching the datastructure or pattern" |
| `set_channel_sample` | D | Points one Channel at a **Sample** already loaded in the Project | Does not import, download, or read a file; does not touch the Sample library or any Pattern |
| `set_channel_volume` | D | Sets one Channel's gain | No pan, no mute, no **Effect** parameter |
| `set_channel_pan` | D | Sets one Channel's pan | No gain, no mute |
| `set_channel_muted` | D | Mutes or unmutes one Channel | No gain — an unmute restores what was there, it does not reset a level |

### 5.6 Effects (§3.4 — the six built-ins only)

| Function | Class | Single effect | Locally does not touch |
|---|---|---|---|
| `add_effect` | D | Appends one of the six stock Effects to one chain | Cannot name a **Plugin** or a seventh Effect — the schema enum is closed at six |
| `remove_effect` | D | Removes one Effect from one chain | Does not reorder the survivors beyond closing the gap |
| `move_effect` | D | Changes one Effect's position in its chain | No parameter values, no other chain |
| `set_effect_parameter` | D | Sets one named parameter on one Effect instance | No other parameter on the same Effect; no bypass state |
| `set_effect_bypassed` | D | Bypasses or re-enables one Effect instance | Does not remove it, and does not reset its parameters |

### 5.7 Arrangement (§3.7)

| Function | Class | Single effect | Locally does not touch |
|---|---|---|---|
| `create_track` | D | Adds one **Track** lane | Places no clip on it |
| `delete_track` | D | Removes one Track and the clips on it | Does not delete the Patterns those clips referenced |
| `rename_track` | D | Changes one Track's display name | Nothing else |
| `set_track_muted` | D | Mutes or unmutes one Track | No Channel-level mute; the two are independent |
| `place_clip` | D | Places one clip referencing one Pattern on one Track at one bar | Does not modify the Pattern; a clip is a reference, not a copy |
| `move_clip` | D | Moves one clip in time, or to another Track | Does not resize it or alter the referenced Pattern |
| `resize_clip` | D | Changes one clip's length | Does not change the referenced Pattern's length |
| `remove_clip` | D | Removes one clip from the Arrangement | Does not delete the referenced Pattern |

**The Arrangement group is where NON-scope 1 is nearest.** These Functions place and move clips the
user's Patterns already define. There is deliberately **no** Function that composes an Arrangement,
generates a song structure, or fills empty bars. See §5.10.

### 5.8 Musical frame

| Function | Class | Single effect | Locally does not touch |
|---|---|---|---|
| `set_tempo` | D | Sets the Project tempo | No event timing is rewritten; no Pattern is re-quantised |
| `set_time_signature` | D | Sets the Project time signature | No Pattern length, no event position |
| `set_project_key` | D | Sets the Project's declared key | **Transposes nothing.** It records the frame; `transpose_pattern` changes the notes |

These three are musical content and persist in the Project — they are not Studio settings. The test
is §1: they change what the Project sounds like, and they are saved in the document. Audio device,
buffer size, provider key and model choice fail that test and are unreachable.

### 5.9 Generation (§6.4 — present but gated)

Both are absent from the Function file entirely until the user configures generation. This is
builder filter #2, not a runtime refusal — on a default install the Assistant is not aware these
exist.

**The capability is `MusicGenerationEnabled` — the §6.4 opt-in — not the presence of a generation
model.** `generate_sample` genuinely needs the configured model; `generate_pattern` does not, because
an AI Pattern is note data emitted by the LLM itself (Appendix B.3), and no audio model is involved.
Both are still gated on the same capability: §6.4 turns *music generation* off by default, and a
Pattern composed by the model is music generation whichever component produces it. The capability is
named for the user's act rather than for a model, so it does not claim a prerequisite
`generate_pattern` does not have.

| Function | Class | Single effect | Locally does not touch |
|---|---|---|---|
| `generate_pattern` | **D** | Fills one Pattern with note or step events generated from the user's prompt | Sends **no Project content** — the model composes from the prompt alone (Appendix B.3: AI **Patterns** come from the LLM, not an audio model). Does not place the Pattern, choose an Instrument, or touch any other Pattern |
| `generate_sample` | **D** | Produces one new **Sample** from the user's prompt via the configured generation model (§6.4) | Sends the user's prompt to the generation provider and **no Project content**. Does not assign the Sample to a Channel or write it into a Pattern |

**Generation is Directive, and that is a real result, not a technicality.** Because generation
composes from the prompt rather than from the Project, a user who has disabled every Rework Function
still keeps AI generation — and still keeps the §8.4 guarantee. The privacy floor and the generative
feature are independent, which is only true because of where the class line falls.

Honest accounting: `generate_sample` transmits the user's prompt to a *second* provider (§6.4). The
§8.4 guarantee is about **Project content**, and it holds exactly as stated. The switchboard must
say which provider a Function talks to, so this is visible rather than inferred (O-17.2).

### 5.10 Rework — the three Functions that transmit

| Function | Class | Single effect | What leaves the machine |
|---|---|---|---|
| `rework_pattern` | **R** | Replaces the events of **one** Pattern with a transformation of them | The events of that one Pattern, and nothing else — no other Pattern, no Arrangement, no Channel list, no Project name |
| `continue_pattern` | **R** | Appends events to one Pattern, continuing its material | The events of that one Pattern |
| `describe_pattern` | **R** | Returns prose about one Pattern to the user | The events of that one Pattern. **Produces no `Delta`** — it is the one read-only Function, and I2 is satisfied vacuously because it changes nothing |

`Excerpt` is bounded by construction: it can hold the events of **one** Pattern. There is no
`Excerpt` variant carrying two Patterns, a Track, or a Project. A Rework Function that wanted more
would need a new `Excerpt` variant, which is a visible, reviewable, ADR-amendable act.

**Deliberately absent:** `rework_arrangement`, `rework_project`, `restructure_song`. Each would put
the whole Arrangement on the wire and each sits directly on NON-scope 1 ("no AI whole-song
generation"). They are named here so that their absence is a recorded decision rather than an
oversight for a later contributor to "fix".

### 5.11 Deliberately absent, and why

| Not a Function | Why |
|---|---|
| `mutate_project` / `apply_patch` / `run_script` | §3.8, §9.2. Unbounded blast radius behind one name |
| `delegate_to_subagent` | §3.4 of this spec — its effect is "whatever the Sub-agent does" |
| anything touching settings | NON-scope 4, §9.1 — and unreachable by construction (§1) |
| `import_sample`, `download_pattern` | §3.6 puts Sample import beyond the MVP; the Function is added when the feature is |
| `save_project`, `open_project`, `export_audio` | Not musical content; §9.6 makes saving the user's act |
| `undo`, `redo` | The Assistant operating the user's remedy would remove the remedy (§3.9) |
| `set_selection`, `scroll_to`, `zoom` | View state, not Project content (§1) |
| a per-field Function such as `set_note_pitch` | Narrowness is blast radius, not field size (§5.3, *On grain*) |

---

## 6. Worked trace — the toggle mechanism (§3.10, §9.3)

The user opens Settings ▸ Assistant Functions and switches **`set_channel_instrument`** off.

**Step 1 — the click.** The switchboard writes `toggles["set_channel_instrument"] = false` and
bumps the store version `v41 -> v42`. Nothing else happens; no request is in flight.

**Step 2 — invalidation.** The cached `FunctionFile` is keyed on
`(registry_version, store_version, audience, capabilities_version)`. The bump invalidates it. The
file is *not* rebuilt yet — rebuilds happen at turn start (§4.4).

**Step 3 — the next turn starts.** `build()` runs over the 43-entry registry:

| Filter | Drops | Remaining |
|---|---|---|
| audience = Assistant | 0 | 43 |
| capabilities (generation unconfigured) | `generate_pattern`, `generate_sample` | 41 |
| toggles | `set_channel_instrument` | **40** |

`manifest_hash` changes. `rework_count` is 3.

**Step 4 — what goes on the wire.**

```diff
  "tools": [
    { "name": "create_pattern",          ... },
    ...
    { "name": "rename_channel",          ... },
-   { "name": "set_channel_instrument",
-     "description": "Point one Channel at a different Instrument.",
-     "input_schema": { "type": "object", "additionalProperties": false,
-                       "required": ["channel", "instrument"], ... } },
    { "name": "set_channel_sample",      ... },
    ...
  ]
```

**Step 5 — the user asks for it anyway.** "Swap the melody to a piano."

The model has no `set_channel_instrument` to select. It is not refused — **it is not there.** The
model answers in prose that it cannot change instruments, or selects nothing. The Studio's call-time
integrity check (§4.2) does not fire, because no call arrives to check. There is no code path in
the Studio that says "this Function is disabled", because that state does not exist at runtime: the
toggle was consumed at build time and left nothing behind except an absence.

**Step 6 — the user re-enables it.** Version bumps to `v43`, the cache invalidates, the next turn
is built with 41 entries, and the Function is offered again. The user's own instrument-swapping
menu was never affected at any point — the toggle is Assistant-only (§3.10).

---

## 7. Worked trace — the structural privacy guarantee (§8.4)

The user switches off `rework_pattern`, `continue_pattern` and `describe_pattern` — the three rows
the switchboard labels **Rework**.

**What the builder produces:** 40 entries, `rework_count == 0`.

**The chain, each link a fact about the code rather than a promise about behaviour:**

1. The file contains no Rework Function. *(Builder filter 3 — §4.2.)*
2. The model can only select a Function that is in the file. *(Closed tool schema, Appendix B.4.
   A name outside the file fails the integrity check and ends the turn — §4.2.)*
3. Therefore no `ReworkFn::collect` is ever called.
4. `Excerpt` has exactly one constructor and it is `ReworkFn::collect`. *(§3.1.)*
5. Therefore no `Excerpt` value exists.
6. `Turn2Rework` cannot be constructed without an `Excerpt`, and `Turn1` has no field that could
   hold Project content — its fields are the user's prompt and the tool list. *(§3.1.)*
7. Therefore every outbound request body is `{ prompt, tools }`.
8. Selectors mean the tool list carries no Project identifiers, names or structure either. *(§2.3 —
   this is the link an id-based design would have broken.)*

**No Project content can leave the machine.** Not "does not", not "should not" — there is no value
of the required type in the program.

**Surfacing it.** The switchboard shows a live banner when the condition holds:

```
┌──────────────────────────────────────────────────────────────┐
│  ● Local only — no Project content can leave this machine.   │
│    All Rework Functions are off. 40 Directive Functions are  │
│    available; they send your prompt only.                    │
└──────────────────────────────────────────────────────────────┘
```

**The banner is derived from the built `FunctionFile`, not from the toggle store** — the predicate
is literally `file.rework_count == 0`. It is computed from the artefact that gets sent, so it
cannot disagree with what is actually sent. A banner derived from user *intent* could drift from
the file; one derived from the file cannot. See obligation O-17.3.

---

## 8. Obligations on adjacent contracts

Freezing this surface constrains issues that are not yet designed. These are the seams; work
against them.

| ID | Issue | Obligation |
|---|---|---|
| O-5.1 | #5 Project data model | Expose a `MusicalContent` root containing patterns, channels, arrangement, effect chains and frame — and **nothing else**. View state, file path and save metadata must sit outside it. §1 depends on this being a real structural split, not a naming convention |
| O-5.2 | #5 | Every addressable musical object needs a stable identity that survives rename. Selectors resolve *to* these; they are never sent |
| O-5.3 | #5 | Model a Project key, or say there is none — `set_project_key` is dropped if not |
| O-7.1 | #7 delta history | Accept `Delta` as the unit of change and be the only writer. Record `origin: Human \| Assistant{function}` |
| O-16.1 | #16 Assistant panel | Bind the Function file at turn start; record `manifest_hash` on the turn (§4.4) |
| O-16.2 | #16 | Implement local Selector disambiguation. An `Unresolved` result is answered by the user, never by an extra request (§2.3) |
| O-17.1 | #17 switchboard | Render one row per registry entry, labelled Directive or Rework (§8.4, round 7). The list is a projection of the registry — there is no second list to maintain |
| O-17.2 | #17 | State per row which provider a Function talks to. `generate_sample` reaches a different provider than the Assistant (§5.9) |
| O-17.3 | #17 | Derive the local-only banner from `file.rework_count == 0`, not from toggle state (§7) |
| O-19.1 | #19 provenance | Two distinct flags, both set from the `Delta`: **`ai_origin`** (§6.3, the visual mark) on any entity created or materially rewritten by an `origin = Assistant` Delta; **`generative_output`** (§6.2, the licence caveat) only on entities from `generate_pattern` or `generate_sample`. An Assistant-built drum lane is AI-authored and must be marked; it is not generative-model output and must not carry the licence warning |
| O-4.1 | #4 LLM provider | Each `ToolDialect` declares whether it enforces closed schemas. Adding a dialect that does not lowers the floor for every Function (Appendix B.4) |

---

## 9. Open items — require the project owner

1. **`set_channel_instrument` vs `change_instrument`, and two terms §5 does not define.** §3.8 names
   the owner's example `change_instrument`. This spec calls it `set_channel_instrument` for
   consistency with the naming grammar (§2.1) and because the object it acts on is a **Channel**,
   which §5's binding vocabulary does not yet contain. Same Function, different label. **Clip** is in
   the same position: §5.7's `place_clip`, `move_clip`, `resize_clip` and `remove_clip` all name one,
   and §5 defines Arrangement and Track but not the object placed on a Track. Confirm the rename, and
   confirm **Channel** and **Clip** as §5 vocabulary terms — otherwise those Functions name things the
   binding vocabulary does not define. This is the one place this spec touches §5.
2. **Selectors resolve §8.4's silence, and the owner should see how.** §8.4 says what leaves the
   machine is "the user's prompt, and an instruction appended by the selected Function". It does not
   say how the model names *which* Pattern. The obvious answer — send a list of the Project's
   Patterns — would put Project content on every request from every user, including one who
   disabled all Rework, and would break §7's guarantee outright. Selectors avoid it (§2.3). The cost
   is that the Assistant sometimes cannot tell two similarly-named Patterns apart and the Studio has
   to ask. That trade is recommended, not assumed.
