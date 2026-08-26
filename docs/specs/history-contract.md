# Contract — Delta History (undo and redo)

- **Status:** Proposed. **Freezes on merge to `dev`.**
- **Owning ADR:** [ADR-007](../adrs/ADR-007-delta-history.md)
- **Issue:** #7
- **Derived from:** `docs/CORE_DOCUMENT.md` — CD §3.9, §3.8, §8.4, §9.2, §9.6, §9.7
- **Binds:** #5 (Project data model), #6 (Function surface), #8 (stack and audio engine),
  #13 (save and load), #16 (Assistant panel), #19 (provenance marks)

Normative words: **MUST**, **MUST NOT**, **MAY** carry their RFC 2119 sense. Everything marked
MUST is a frozen contract: changing it stops the work that depends on it (architect role rule).

**Section references:** `CD §x` means a section of `docs/CORE_DOCUMENT.md`. A bare `§x` means a
section of this contract.

---

## 1. Vocabulary

Two terms come from the owner's own words in CD §3.9 and are used here as written. Two are new and
require ratification into the binding CD §5 table before this contract leaves *Proposed*.

| Term | Meaning | Source |
|---|---|---|
| **Delta** | One reversible change to an open Project. **One Delta is one press of undo.** | CD §3.9, owner |
| **History** | The linear sequence of Deltas for one open Project, plus a cursor into it. | CD §3.9, owner |
| **Origin** | Who caused a Delta: `User` or `Assistant`. Not the Function's name — see §7.3. | **new** |
| **Gesture** | A scope in which several Deltas collapse into one History entry (a knob drag). | **new** |

`Operation` (§4) is implementation vocabulary. It never appears at any interface a caller sees
and it is deliberately **not** proposed for CD §5.

---

## 2. The two seams

### 2.1 The Edit seam — the only way a Project changes

> **MUST.** Nothing mutates an open Project except by applying a Delta through the History.

The Project's mutating surface is internal to the Project module and reachable only from the four
Operation primitives in §4. There is no second path. This is what makes the issue's first
acceptance criterion — *every mutation is expressible as a reversible delta* — true **by
construction** rather than by review discipline.

`apply` both mutates and records, in one call. They are not separable, because a caller that could
mutate without recording is a caller that can break CD §9.7.

### 2.2 The Delta seam — what a change is

> **MUST.** The History MUST NOT name, import, or branch on any Project type, any Function, or any
> Delta kind. It stores things that can apply themselves and hand back their own inverse.

**Depth test, and the acceptance criterion for this design:** adding a Function, an Effect
parameter, or a whole new entity to the data model MUST NOT change the History interface and MUST
NOT add a case to any conditional inside the History. If it does, the seam is in the wrong place.

There is deliberately **no `ProjectAbstraction` interface**. Nothing varies across that seam — one
adapter is a hypothetical seam, not a real one — so a Delta closes over its target at construction
and `apply` takes no arguments. The History is therefore generic over nothing, and testable with a
counter-based fake Delta and no Project at all.

---

## 3. Interfaces

### 3.1 Delta

```
Delta
  apply()      -> Delta       # mutate the target; RETURN the exact inverse
  label()      -> Text        # user-facing, describes the EFFECT (see §7.3)
  origin()     -> User | Assistant
  size_hint()  -> Bytes       # this Delta's retained cost, for the memory bound (§9)
  is_empty()   -> Bool        # true when application observed no change
```

`apply` returning its own inverse is load-bearing:

- The inverse is built from the **state actually observed at the moment of application**, never
  from a before-image predicted earlier and possibly stale.
- Undo and redo are then the same mechanism: undo runs the stored inverse, which returns the
  forward Delta again. There is no separate redo path and no special case.

### 3.2 History

```
History                                    # one per open Project
  apply(delta)         -> Applied | NoChange | Failed
  undo()               -> Bool             # false when there is nothing to undo
  redo()               -> Bool             # false when there is nothing to redo
  begin_gesture(label) -> GestureToken
  end_gesture(token)                       # collapse the run into one entry (§6)
  state()              -> HistoryState
  observe(listener)    -> Subscription

HistoryState
  can_undo, can_redo     : Bool
  undo_label, redo_label : Text?
  is_dirty               : Bool            # §8.3
```

Seven members hide: every mutation kind in the Studio, the discard-on-branch rule, gesture
collapsing, the memory bound, dirty tracking, and the publication contract with the audio thread.
None of those leak, and none of them grows the interface when the Studio does.

---

## 4. Operations — the closure argument

A Delta is an **ordered list of Operations**. There are exactly four kinds, and they are closed
under inversion:

| Operation | Inverse |
|---|---|
| `Set(entity_id, field, value)` | `Set(entity_id, field, observed_prior_value)` |
| `Insert(parent_id, collection, position, entity)` | `Remove(parent_id, collection, position)` |
| `Remove(parent_id, collection, position)` | `Insert(parent_id, collection, position, removed_entity)` |
| `Move(from_parent, from_pos, to_parent, to_pos)` | the reverse `Move` |

> **The inverse of a Delta is the reversed list of the inverted Operations.**

That single sentence discharges *"every mutation in the data model is expressible as a reversible
delta"* — for any data model built only out of constructs these four cover. Which is the contract
this places on #5.

### 4.1 Obligations on the Project data model (#5)

1. **MUST** be a tree of identified entities with typed fields and *ordered* child collections;
   cross-references between entities are fields holding an id.
2. Every addressable entity **MUST** carry a stable, opaque id that is unique within the Project,
   assigned once, and **never reused** — not even after the entity is removed and its removal
   undone. Deltas address entities by id, **never** by index or by path: an index is invalidated
   by any other Delta, and an inverse computed against a stale index corrupts data. Positions
   inside ordered collections are recorded *as payload* by `Insert`/`Remove`/`Move`, which is a
   different thing from addressing by them.
3. Anything not expressible in (1) **MUST** be declared **derived state** — recomputed after a
   Delta from the model, never recorded in one, never restored by an inverse. Waveform peak
   caches, DSP coefficients and playback lookup tables are derived state.
4. **Sample audio is never carried in a Delta.** Audio lives in a content-addressed store,
   referenced by id and reference-counted from both the Project and the History. Undoing the
   deletion of a 40 MB Sample restores it; the History holds a handle, not 40 MB.
5. CD §8.1 applies to ids: **no id, field name or type tag may contain the
   project's name.** The name is deliberately disposable and must stay cheap to dispose of.

---

## 5. Precise semantics

Let the History be an ordered list `D[0 .. n-1]` and a cursor `c` in `0 .. n`.
**Invariant:** `D[0 .. c-1]` are applied to the Project; `D[c .. n-1]` are not.

### 5.1 undo

If `c = 0`, do nothing and return `false`. Otherwise apply the inverse held at `D[c-1]`, replace
`D[c-1]` with the Delta that application returned, set `c := c - 1`, return `true`.

### 5.2 redo

If `c = n`, do nothing and return `false`. Otherwise apply `D[c]`, replace it with the returned
inverse, set `c := c + 1`, return `true`.

### 5.3 apply — the discard-on-branch rule (CD §3.9, the Word model)

1. Apply the Delta and take its inverse.
2. **If the Delta reports `is_empty`, stop here.** It is not recorded, and **the future is not
   discarded.** A Delta that changed nothing must not cost the user their redo stack, and must not
   put a do-nothing entry on the History that makes undo appear broken.
3. **If application failed, stop here.** The History is unchanged and the future survives. A failed
   edit MUST NOT destroy the redo stack.
4. Otherwise: **truncate `D` to length `c`**, discarding `D[c .. n-1]` permanently and releasing
   them. Append the new entry. `c := n := c + 1`.

Discarding is unconditional and irreversible. There is no path that restores a discarded future —
that is precisely the Word model the owner asked for, and not a tree.

**What happens to a redo stack mid-session**, stated exhaustively:

| Event mid-session | Effect on the redo stack |
|---|---|
| User undoes, then edits anything | Discarded, permanently |
| User undoes, then the **Assistant** edits anything | Discarded, permanently — Assistant Deltas are not privileged |
| User undoes, then redoes | Preserved; the cursor moves forward |
| User undoes, then saves the Project | **Preserved.** Saving is not an edit (§8.1) |
| User undoes, then an edit **fails** | Preserved (rule 3) |
| User undoes, then an edit is a **no-op** | Preserved (rule 2) |
| Oldest entries evicted by the memory bound (§9) | Unaffected — eviction only ever touches the oldest end |
| Project closed | Discarded with the whole History (§8) |

### 5.4 Serialisation of edits

Applying, undoing and redoing all run on the single **edit thread** and are serialised against one
another. An Assistant Function that is mid-flight completes as one Delta or not at all; an undo
requested while it runs is queued behind it, never interleaved.

---

## 6. Gestures and coalescing

A knob drag produces hundreds of `Set` Operations. Each one **MUST** still go through `apply` —
otherwise §2.1 is violated and the audio thread would not hear the drag. They must not each become
a press of undo.

- `begin_gesture(label)` opens a scope. Deltas applied inside it are applied and published
  normally.
- `end_gesture(token)` collapses the run into **one** History entry: consecutive Operations
  targeting the same `(entity_id, field)` collapse to the first one's prior value and the last
  one's final value; Operations on anything else are kept, in order, inside the same single Delta.
- A gesture with no net change is not recorded and does not truncate the future (§5.3 rule 2).
- Gestures do not nest. Opening one inside another is a programming error, not a runtime mode.

> **MUST NOT: coalesce by elapsed time.** A 300 ms window guesses where a gesture ends, and that
> guess is the usual reason a DAW's undo feels unreliable. A pointer press and release know
> exactly.

---

## 7. Assistant actions

### 7.1 One Function, one Delta

> **MUST.** Every Function invocation produces exactly one Delta, or none.

This is not only undo ergonomics. It is also the audio-consistency rule of §10.2: a
`rework_pattern` that landed as 32 separate Deltas could be **heard** half-applied. A Function that
fails part-way MUST roll back the Operations it already performed and record nothing.

Assistant Deltas sit in the same list as the user's, interleaved in time order. There is no
separate Assistant stack (ADR-007, rejected alternative 5).

### 7.2 A Delta records outcomes, never intentions

> **MUST NOT.** A Delta may not store "invoke Function *f* again". It stores the resulting values.

Three consequences, each independently sufficient:

- **Redo MUST NOT re-invoke a Function**, so redo never re-transmits Project content off the
  machine. A Rework Function (CD §8.4) sends Project content once, when the user asked for it — never
  again silently on a redo.
- Redo is deterministic. Re-invocation would let redo produce a different result from the one being
  redone.
- Redo performs no network I/O and cannot fail on an expired key or a rate limit.

### 7.3 Labels must not leak which Function ran

NON-scope item 6 says the user is never shown which Function was invoked. Therefore:

> **MUST.** A Delta's `label()` is generated from the Delta's **content**, in CD §5 domain vocabulary,
> and MUST NOT be derived from the Function's identity.

So the undo tooltip reads *"Undo Assistant change to Pattern 3"* — built from `origin()` and the
entities the Operations touched — and can never read *"Undo rework_pattern"*. Generating the label
from content rather than from the Function makes the leak **unrepresentable**, rather than a rule
someone has to remember at each of N call sites.

### 7.4 Provenance is carried by the same Delta

AI provenance (CD §6.3) is a field in the data model, so setting it is a `Set` Operation. The Function
**MUST** set it inside the same Delta as the content it marks. Undo then removes the mark with the
content, and redo restores both, with no extra machinery. A provenance mark that outlived an undo
would be a false accusation; content that outlived its mark would be a false promise (CD §9.4).

This is also how obligation **O-19.1** is discharged without §7.3 being weakened. O-19.1 asks for
two provenance flags "set from the `Delta`", and a Delta cannot name the Function that produced it.
It does not have to: the generating Function knows what it is and writes the flag itself, in this
same Delta. See [`ADR-062`](../adrs/ADR-062-provenance-two-flags.md).

### 7.5 Why this is the whole remedy

The user cannot inspect what the Assistant did (NON-scope 6). Undo is therefore the only recourse,
which is why CD §9.7 is absolute. Every rule in this section exists to keep that one remedy sound.

---

## 8. Save, load and close

> **The History does not survive closing a Project.** It is session-scoped: not written into the
> Project file, not written beside it. On load the History is empty, `n = 0`, `c = 0`.

Reasoning and rejected alternatives are in ADR-007. The gap this leaves — an unwanted Assistant
change noticed after a save-and-quit — is covered by #13, not by persisting deltas:

> **Obligation on #13.** Save MUST be atomic and MUST **retain** the previous good version. Undo is
> the in-session remedy; retained prior versions are the cross-session one. #13 already owes the
> atomicity for CD §9.6; this contract adds only that the previous version is *kept*, not merely
> not-destroyed.

### 8.1 Saving does not touch the History

Saving is not an edit. It does not truncate, does not append, does not move the cursor. A user may
undo back past the point at which they saved. Word does this too.

### 8.2 Loading

`History` is constructed empty. Because a Delta closes over its target (§2.2), a Delta from one
open Project is meaningless against another — the same fact that makes persisting the History
unattractive, seen from the other side.

### 8.3 The dirty flag is derived from the cursor

The History records `saved_cursor`, set to `c` at each explicit user save.

> `is_dirty` ⟺ `c ≠ saved_cursor`

So undoing back to the state that was last saved correctly clears the dirty flag, and redoing away
from it sets it again. Two refinements:

- Only an **explicit user save** moves `saved_cursor`. A crash-recovery autosave (if #13 has one)
  does not — the user did not choose that state.
- If `saved_cursor` is evicted by the memory bound (§9), it becomes unreachable and `is_dirty` is
  thereafter permanently true. Conservative: it may prompt for a save that was not needed; it will
  never skip one that was.

---

## 9. Memory under a long session

Deltas accumulate for as long as the Studio is open. The History is bounded by **two limits,
whichever binds first**:

| Limit | Default | Configurable |
|---|---|---|
| Entry count | 512 Deltas | Settings tab |
| Retained bytes (sum of `size_hint`) | 64 MB | Settings tab |

A count alone is the wrong bound: one `rework_pattern` Delta can retain thousands of times what a
knob turn retains, so a count-only limit is either uselessly small or unboundedly large in bytes.

- Eviction is from the **oldest end only** — the only end that can be dropped without breaking the
  linear model. The user then cannot undo past that point.
- Eviction never touches entries at or after the cursor, so it never disturbs the redo stack.
- Each Delta reports its own `size_hint()`; the History only sums. What a Delta costs stays
  knowledge of the Delta, so the History stays ignorant of kinds (§2.2).
- Releasing an evicted Delta releases its reference into the content-addressed store (§4.1 item 4),
  which is when a deleted Sample's audio actually becomes reclaimable.

These bounds live in the settings tab, so — by CD §9.1 — **the Assistant can
never enlarge, shrink or clear the History.** It cannot arrange for its own actions to fall off the
end.

---

## 10. Real-time audio contract

The audio thread reads the Project while the edit thread writes it. #8 chooses the mechanism; this
contract fixes what the mechanism must guarantee.

1. **Deltas are applied only on the edit thread.** Undo and redo are Deltas and get the same rule.
2. **Publication is atomic per Delta.** The audio thread observes either the whole pre-Delta state
   or the whole post-Delta state, never an intermediate. This is the second reason for §7.1.
3. **No allocation or deallocation on the audio thread.** Objects displaced by a Delta go to a
   reclamation queue owned by the edit thread, and objects still retained by the History for an
   inverse are not reclaimable until evicted (§9).
4. During a gesture each intermediate Delta publishes normally, so the drag is audible; only the
   History entry is collapsed (§6).

---

## 11. Module boundary and dependency direction

Concrete paths land with #8. The dependency rule is frozen now:

```
   assistant/functions ──┐
                         ├──> project  (model + Operations)
   ui/editing ───────────┤
                         └──> history

   history  ──> the Delta interface only. Depends on nothing else.
   project  ──> does NOT depend on history.
```

- `history` is a **deep module**: seven interface members over every mutation the Studio can make.
- `project` not depending on `history` is what allows the model to be tested without a History, and
  the History to be tested without a model.
- The Delta seam has as many adapters as the Studio has mutation kinds — a real seam, not a
  hypothetical one.

---

## 12. Verification — falsifiable checks

Each maps to a claim above. All are testable before any UI exists.

| # | Check | Guards |
|---|---|---|
| 1 | For any random sequence of Deltas, apply-all then undo-all yields a canonical serialisation identical to the start state | §4 closure |
| 2 | apply → undo → redo yields the same state as apply alone | §3.1, §5.2 |
| 3 | After an `apply` following an undo, `can_redo` is false and the discarded Deltas are released | §5.3 |
| 4 | A Delta that fails leaves `D`, `c` and the Project unchanged | §5.3 rule 3 |
| 5 | A no-op Delta leaves `D`, `c` and the redo stack unchanged | §5.3 rule 2 |
| 6 | No code path outside an Operation mutates the Project — enforced by visibility, checked by a build-time rule | §2.1 |
| 7 | A knob drag of 500 samples produces exactly one History entry and 500 audible publications | §6, §10.4 |
| 8 | Undoing an Assistant Delta clears the provenance mark it set; redo restores it | §7.4 |
| 9 | **Redo issues no network call.** Assert the LLM client is untouched across a redo of a Rework Delta | §7.2 |
| 10 | No `label()` anywhere in the Studio contains a Function name — checked against the Function file (#6) | §7.3 |
| 11 | Undoing back to `saved_cursor` clears `is_dirty`; redoing sets it | §8.3 |
| 12 | Under a scripted long session, retained bytes stay under the bound and the oldest entries are the ones evicted | §9 |
| 13 | Every Function in the Function file (#6) produces exactly one Delta or none, including on its failure path | §7.1 |

---

## 13. What this contract does not decide

- The Project data model itself — #5, constrained by §4.1.
- Which Functions exist — #6, constrained by §7.
- Language, threading primitive, publication mechanism — #8, constrained by §10.
- The on-disk format and the save algorithm — #13, constrained by §8.
- The undo and redo buttons — a UI issue, explicitly out of scope for #7.
