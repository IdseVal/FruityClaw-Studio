# ADR-007 — Linear delta history for undo and redo

- **Status:** Proposed
- **Date:** 2026-08-23
- **Issue:** #7 — Design the delta history for undo and redo
- **Decided by:** Architect agent; requires project-owner ratification, see *Open items*
- **Supersedes:** nothing
- **Contract:** [`docs/specs/history-contract.md`](../specs/history-contract.md) — normative;
  this ADR is the reasoning, the contract is the thing that binds.

A bare `§x` below refers to `docs/CORE_DOCUMENT.md`; `Contract §x` refers to the contract above.

## Context

Core document §3.9 fixes three things about history and leaves the rest to design: changes are
stored as **deltas** rather than snapshots, undo and redo move through them, and continuing from an
earlier delta discards everything after it — the Word model, not a tree.

§9 item 7 turns that from a convenience into a hard requirement: **no Assistant action may be
irreversible.** The reason is NON-scope item 6 — the user is never shown which Function ran. They
cannot inspect what the Assistant did, only reverse it. Undo is not one remedy among several. It is
the only one.

Two facts about this codebase shape the design more than the undo semantics do:

1. **A real-time audio thread reads the Project while the edit thread writes it.** Undo is not a
   pure data-structure problem. A half-applied change is not merely an inconsistent model; it is
   audible.
2. **Three sibling architecture issues are open in parallel** — #5 (data model), #6 (Function
   surface), #8 (stack and audio engine). This design must constrain all three without
   pre-empting any of them, and must not need to change when they land.

## Decision

A **linear delta history** built on two seams and one closed set of primitives. Full normative
detail is in the contract; the four decisions that matter are below.

### 1. Applying and recording are one call, and it is the only way a Project changes

Nothing mutates an open Project except by applying a Delta through the History. `apply` mutates
*and* records, inseparably.

This is the load-bearing choice. It makes *"every mutation in the data model is expressible as a
reversible delta"* — the issue's first acceptance criterion — true **by construction**. If a second
mutation path existed, the property would be a promise kept by code review, and §9.7 would rest on
nobody forgetting.

### 2. The History knows nothing about what a Delta means

The History MUST NOT name, import, or branch on any Project type, any Function, or any Delta kind.
It stores things that can apply themselves and hand back their own inverse.

The acceptance criterion for the seam: **adding a Function or a new entity to the data model must
not change the History interface and must not add a case to any conditional inside it.** Seven
interface members stand over every mutation the Studio will ever make. Contrast the shallow version
— a `Delta` union enumerating every mutation kind, with a `switch` in the History — which would
grow every time #5 or #6 grew, and would make the History untestable without the whole model.

There is deliberately **no `ProjectAbstraction` interface** between them. Nothing varies across that
seam; one adapter is a hypothetical seam, not a real one. A Delta closes over its target instead, so
the History is generic over nothing and can be tested with a counter-based fake Delta and no Project.

### 3. `apply` returns its own inverse

Rather than a Delta carrying a predicted before-image, applying one **returns** the exact inverse,
built from the state actually observed at that moment.

- The before-image can never be stale.
- Undo and redo become the same mechanism: undo runs the stored inverse, which returns the forward
  Delta again. No separate redo path, no special case, nothing to keep in sync.

### 4. Four primitives, closed under inversion

A Delta is an ordered list of Operations, of exactly four kinds — `Set`, `Insert`, `Remove`, `Move`
— each with an obvious inverse. **The inverse of a Delta is the reversed list of the inverted
Operations.**

That sentence is the reversibility proof, and it costs nothing to check. What it buys is a precise
contract on #5 in place of a hopeful one: the data model must be a tree of identified entities with
typed fields and ordered child collections, addressed by stable never-reused ids, with everything
else declared derived state and Sample audio held by reference in a content-addressed store.

### Rules that fall out, and are not obvious

Four consequences that the issue does not ask for and that an implementation would otherwise get
wrong:

- **A Delta records outcomes, never intentions.** It may not store "invoke Function *f* again".
  Therefore **redo never re-invokes a Function**, and so a redo of a Rework action never
  re-transmits Project content off the machine (§8.4). Redo is also then deterministic and does no
  network I/O.
- **A Delta's label is generated from its content, never from the Function's identity.** NON-scope 6
  forbids showing which Function ran; deriving the label from content makes that leak
  *unrepresentable* rather than a rule to remember at N call sites. The tooltip reads "Undo
  Assistant change to Pattern 3", and cannot read "Undo rework_pattern".
- **One Function is one Delta** — for audio consistency as much as for undo. A `rework_pattern` that
  landed as 32 Deltas could be *heard* half-applied.
- **The History bounds live in the settings tab**, which §9.1 puts out of the Assistant's reach.
  The Assistant cannot enlarge, shrink or clear the History, so it cannot arrange for its own
  actions to fall off the end.

### Memory: two bounds, not one

Bounded by entry count **and** retained bytes, whichever binds first (defaults 512 / 64 MB, both
user-configurable). A count alone is wrong because one `rework_pattern` Delta retains thousands of
times what a knob turn retains — a count-only limit is either uselessly small or unbounded in bytes.
Each Delta reports its own size; the History only sums, so it stays ignorant of kinds. Eviction is
from the oldest end only and never touches the redo stack.

### Save and load: the History does not survive closing a Project

Session-scoped. Not written into the Project file, not written beside it. Saving mid-session does
not touch the History — a user can undo back past a save, as in Word — and the dirty flag is derived
as `cursor ≠ saved_cursor`, so undoing back to the saved state correctly clears it.

The gap this leaves is real and named: an unwanted Assistant change noticed after a save-and-quit
has no undo. It is closed by **#13 retaining the previous good version on every save** — #13 already
owes atomic save for §9.6; this adds only that the prior version is kept. Undo is the in-session
remedy; retained versions are the cross-session one. That is a cheaper and stronger guarantee than
persisted deltas, which would have to survive every future format migration to be worth anything.

## Alternatives rejected

1. **Snapshot history.** Rejected by §3.9, but it deserves a real answer in its strongest form —
   *persistent immutable data structures with structural sharing*, which make a "snapshot" cheap in
   memory. Rejected anyway on two grounds. It forces the entire model to be persistent and
   immutable, which fights a real-time audio engine that wants stable, in-place, cache-friendly
   buffers. And it makes §3.8's *"one Function touches exactly one thing"* unverifiable: a snapshot
   diff shows what changed, but nothing constrains what a Function was *able* to change. Explicit
   Operations do.

2. **Tree / branching history** (undo-tree, Vim, Emacs). Rejected by §3.9 explicitly — "the
   Microsoft Word model, not a tree". Independently right: a tree is only useful with UI to navigate
   it, which no one asked for, and §1.5's guiding default is FL Studio, whose history is linear.

3. **Command-replay history** — store the intent, re-execute it on redo. Rejected on three counts.
   Assistant commands are non-deterministic, so redo could produce a different result from the one
   being redone. Redo of a Rework command would silently re-transmit Project content off the machine,
   against the spirit of §8.4. And every command would have to remain replayable against a model that
   has since changed.

4. **Persisting the History in the Project file.** Rejected. It bloats the file with data larger than
   the document; it forces every future format migration to migrate the history too, forever; and a
   corrupt history entry could make a Project unloadable, which is exactly what §9.6 forbids.

5. **A sidecar history file next to the Project.** Rejected, though it is the strongest persistence
   option — it keeps the Project file clean and a corrupt sidecar can simply be dropped. It still
   needs a save-state fingerprint to detect the copied-to-a-USB-stick desync, it doubles the number
   of files a user can lose track of, and it buys a cross-session remedy that retained save versions
   (#13) already provide more simply. Reconsider only if #13's retention turns out to be inadequate.

6. **A separate Assistant undo stack.** Rejected. It makes "undo" ambiguous at the moment the user
   most needs it to be obvious, and it is unsound: a user edit can depend on a preceding Assistant
   edit, so two independent stacks can be popped into a state neither produced.

7. **Time-based coalescing** — merge everything within ~300 ms. Rejected in favour of explicit
   gesture scopes. A time window *guesses* where a gesture ended; a pointer press and release know.
   Guessing is the usual reason a DAW's undo feels unreliable.

8. **Spilling old deltas to disk for unbounded history.** Rejected for the MVP. It turns undo into
   I/O, and it buys depth of history that no one asked for.

## Consequences

- **#5 is constrained, not decided.** The data model must be a tree of identified entities with
  ordered child collections and stable never-reused ids; derived state must be declared as such;
  Sample audio must live in a content-addressed store. Contract §4.1.
- **#6 is constrained, not decided.** Every Function produces exactly one Delta or none, rolls back
  its own partial work on failure, sets provenance inside the same Delta as the content it marks,
  and never contributes its name to a label. Contract §7.
- **#8 inherits a real-time contract**: Deltas applied only on the edit thread, publication atomic
  per Delta, no allocation or deallocation on the audio thread. The mechanism is #8's choice; the
  guarantee is not. Contract §10.
- **#13 inherits one addition**: retain the previous good version on save. Contract §8.
- The `history` module depends on the Delta interface and nothing else; `project` does not depend on
  `history`. Each is testable without the other.
- Thirteen falsifiable checks are specified in contract §12, all testable before any UI exists. Two
  are worth naming here because they guard properties that are otherwise invisible: **redo issues no
  network call**, and **no label anywhere contains a Function name**.

## Open items — require the project owner

1. **Two vocabulary additions.** §5 of the core document is binding and says synonyms are defects.
   *Delta* and *History* are the owner's own words from §3.9 and are used as written. **Origin**
   (`User` | `Assistant`) and **Gesture** are new and need ratification into the §5 table. This ADR
   does not edit the AGREED core document from a feature branch.

2. **The cross-session gap is a deliberate acceptance, not an oversight.** History does not survive
   closing a Project, so an unwanted Assistant change noticed after a save-and-quit is remedied by a
   retained prior version rather than by undo. Every DAW behaves this way, but §9.7's wording is
   absolute and the owner should confirm that it means *within a session*.

3. **The default bounds — 512 entries, 64 MB — are engineering defaults, not measured ones.** No
   profiling exists to size them against, because no data model exists yet. They should be revisited
   once #5 lands and a `rework_pattern` Delta has a real size.

4. **ADR numbering.** This ADR is numbered after its issue (`ADR-007` for #7), matching the merged
   `ADR-001` for #1. Three open PRs (#22, #23, #24) each number their ADR `0001` or `0002`
   independently and collide with each other. A human should settle the scheme; issue-numbered ADRs
   are collision-free across parallel worktrees without coordination and are the suggestion here.
