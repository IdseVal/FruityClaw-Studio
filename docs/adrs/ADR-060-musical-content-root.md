# ADR-060 — The `MusicalContent` root: the reach rule as a shape of the type graph

- **Status:** Proposed. Freezes on merge to `dev`.
- **Date:** 2026-08-26
- **Issue:** #60 — O-5.1 undischarged: Functions receive the whole Project
- **Decided by:** Architect agent
- **Amends:** [`ADR-005`](ADR-005-project-data-model.md) (D1, D5) and [`ADR-006`](ADR-006-function-surface-and-function-file.md) (§1's picture of the Project). Supersedes neither.
- **Contracts changed:** [`docs/specs/project-data-model.md`](../specs/project-data-model.md) §3.1, §5.1, §6; [`docs/specs/function-surface.md`](../specs/function-surface.md) §1, §5.1, §8

This ADR records *why*. The specs record *what*, and are normative.

## Context

`docs/specs/function-surface.md` §1 makes the strongest structural claim in the project:

> **This is enforced by construction, not by a check.** Every Function is handed exactly one
> object, `MusicalContent`, a read-only view rooted at the musical subtree of the open Project.
> […] §9.1 is not a rule the Assistant is asked to respect; it is a shape of the type graph.

Invariant I1 (§5.1 of the same spec) is written on that basis, and obligation **O-5.1** puts the
work where it belongs:

> Expose a `MusicalContent` root containing patterns, channels, arrangement, effect chains and
> frame — and **nothing else**. View state, file path and save metadata must sit outside it. §1
> depends on this being a real structural split, not a naming convention.

Issue #5 closed and ADR-005 merged without discharging it. On `dev` at `95f01a8` — where this
audit was taken, and still so at `b05dfdb`, where this change lands — `MusicalContent` was a word
`docs/` used fifteen times and `src/` used never. Every Function took the whole document:
`create_track(const Project&, …)` and ten more like it.

**The rule held anyway — by coincidence.** `core::Project` happened to contain only musical
content, so there was nothing in reach for a Function to misuse. Two facts made that a matter of
timing rather than design:

1. Nothing in the code said the coincidence was load-bearing. A reader of
   `arrangement_functions.h` saw a Function that takes the Project and had to read the body, and
   every future body, to know it did not reach further.
2. Issue #13 (save and load) and #55 (audio device settings) both have obvious reasons to put a
   file path, a dirty flag or a preference on `Project`. The moment one did, §9.1 would be false
   with no commit saying so, no test failing and no reviewer prompted.

Credit where it is due: PR #31 kept the open file's path in `MainWindow::current_file_` and
dirtiness in `ProjectHistory`, which is exactly right. It was right by the author's judgement,
not by anything the type system would have insisted on.

## Decision

Make the split real, in `src/core/entities.h`.

### D1 — `MusicalContent` is a type, and it is the whole of what a Function can see

```cpp
struct MusicalContent {
    double tempo;                        // the frame
    std::pair<int, int> time_signature;
    OrderedMap<Sample> samples;
    OrderedMap<Instrument> instruments;
    OrderedMap<Pattern> patterns;
    OrderedMap<Arrangement> arrangements;
    std::vector<Effect> master_chain;
};
```

Exactly O-5.1's list. Every Function in `src/core` now takes `const MusicalContent&`; there is no
overload taking a `Project`.

**Membership is decidable, not a matter of taste.** A field belongs in `MusicalContent` **iff
changing it changes what `engine::bake` produces**. That is not a slogan chosen to sound firm: the
renderer's input type and the Assistant's reach are now the same type, so the test is mechanical
and anyone can run it. Tempo passes. A file path does not. §5.8's musical frame passes — which is
the answer the function-surface spec already gives for why `set_tempo` is a Function and
`set_buffer_size` is not.

### D2 — `ProjectMeta` is the other half, and it is where the file path goes

```cpp
struct ProjectMeta { Id id; int format_version; std::string title; };
struct Project     { ProjectMeta meta; MusicalContent musical; };
```

`ProjectMeta` is unreachable from a Function because no reference to it exists in
`MusicalContent` — the *"nothing to reach through"* §1 describes. Issue #13 may put the Project's
file path and save metadata here without changing a single Function signature and without widening
anything the Assistant can see. It may equally keep them in `ProjectHistory` and the window, as it
does today; the point of this ADR is that the safe place now exists and is named.

The Project's `title` sits in `ProjectMeta`, not in `MusicalContent`. It is not in O-5.1's list, it
does not change what the Project sounds like, and §5.10 promises that a Rework `Excerpt` carries
"no Project name" — a promise that is now structural rather than remembered.

### D3 — A `Delta` is typed on `MusicalContent`, so the write side is as narrow as the read side

`Op::run` was `std::function<std::optional<Op>(Project&)>`. It is now
`std::function<std::optional<Op>(MusicalContent&)>`, and `apply_delta` takes a `MusicalContent&`.

This is the half that is easy to miss. A Function does not perform a change, it returns one — so
narrowing only its parameter would leave every returned `Op` holding a write path to the file path
its author could not read. With both sides narrowed, *a reader can see that no Function reaches
the document's metadata without reading any function body*, which is precisely what the issue asks
for. `ProjectHistory` still owns the whole `Project`; it applies Deltas to `project_.musical`.

### D4 — View state is in neither half, and the function-surface diagram is corrected

`function-surface.md` §1 drew `Project ▸ view/` as an unreachable subtree. `project-data-model.md`
§3.11 is stricter and older: transient view state is **not in the Project at all**. §3.11 wins —
it is the stronger statement, and it is what the code does. The diagram is amended rather than the
model: zoom, scroll and selection live in the widgets that own them, so there is no `view/` for
anything to reach.

The same paragraph amends `meta/`'s "provenance index": rule P5 forbids storing one. It is
computed by walking the reference graph.

### D5 — Three mechanical checks, so this cannot rot the way O-5.1 did

An obligation nobody checks becomes an obligation nobody discharged, which is how this issue got
filed. The answer to *who checks* is CI:

| Check | Where | Catches |
|---|---|---|
| The census — a structured binding naming every member of `MusicalContent`, `ProjectMeta` and `Project` | `tests/test_reach_rule.cpp` | A field added to the Assistant's reach. Stops compiling until the person adding it says so in the test. |
| `static_assert` per Function that its first parameter is `const MusicalContent&`, and that `Op` and `apply_delta` are typed on `MusicalContent` | same file | A signature widened to unblock one caller. |
| `reach_rule_lint.py` — no `Project`-rooted identifier may appear in any `*_functions.h` / `*_functions.cpp` | `tests/`, run by CTest | Functions **nobody has written yet**. It does not need to know their names. |

Plus the run-time proof the issue asks for: a test that drives a creation, a set, a move and a
deletion through `ProjectHistory` and asserts `read().meta` is unchanged — stated over the whole
of `ProjectMeta` rather than over a list of fields, so it keeps holding as #13 and #55 add to it.

## Worked example — issue #13 adds a file path

```cpp
struct ProjectMeta {
    Id id;
    int format_version;
    std::string title;
    std::filesystem::path file;   // #13
    bool autosave_pending;        // #13
};
```

What changes: `tests/test_reach_rule.cpp`'s `project_meta_is_exactly_this` stops compiling until
the two new names are added to its structured binding — a five-second edit whose only purpose is
that a human typed the names into the file that documents the split.

What does not change: no Function signature, no `Delta`, no schema, no invariant. `test_reach_rule`'s
run-time case passes untouched, and it is now asserting something that was not true before the
fields existed. The Assistant cannot read `file`, cannot write it, and cannot reach anything that
refers to it.

Contrast with `dev` today: the same two fields land on `Project`, and eleven Functions silently
acquire the ability to read the user's home directory out of `project.file.parent_path()`. Nothing
fails. That is the defect this ADR closes.

## Alternatives rejected

| Rejected | Why |
|---|---|
| **Leave `Project` whole and enforce §9.1 by review** (the issue's second option) | It asks reviewers to notice an *absence* — that a Function did not reach through a reference it was handed. Absences are what review is worst at, which is why §1 chose construction over a check in the first place. Taking this option also means rewriting §1 and I1 to claim less, at the exact moment the claim is cheapest to make true: no `MusicalContent` field is contested, and the whole change is one mechanical pass. |
| **Keep the whole `Project` as the parameter, add a lint that forbids touching non-musical fields** | The lint can only forbid names it knows. Once `project.file` exists on the type a Function holds, every new field is a new lint rule, and the guarantee is only as current as the rule list. |
| **Narrow the Function parameter but leave `Op` over `Project&`** | Cheaper, and wrong: a Function returns the change rather than performing it, so its Deltas would keep the reach its signature disclaims. The reader would still have to audit every closure body. See D3. |
| **Make `MusicalContent` a view class with accessors rather than a struct member** | A second interface to keep in step with the data, whose narrowness is only as good as its method list — and the list grows every time a caller needs one more thing. A member is checked by reading fifteen lines. `ProjectHistory` also has to own something concrete to apply Deltas to. |
| **Put `title` in `MusicalContent`** | Not in O-5.1's list, does not change what the Project sounds like, and would put the Project's name inside the object a Rework `Excerpt` is collected from — §5.10 says it does not leave the machine. |
| **Rename the document (`Document`, `Session`) and keep `Project` for the musical half** | §5 binds **Project** to the user's whole work, and every contract, issue and prompt says "Project" in that sense. Renaming the outer thing would ripple through documents this ADR has no mandate to touch, to save one level of `.musical`. |
| **Model `MusicalContent` as `channels/` per function-surface §1's diagram** | That is issue #61's question — whether the catalogue's **Channel** is the data model's `Instrument`, its `Part`, or neither. This ADR fixes the *boundary* of what a Function receives; #61 fixes what is inside it. Deciding both at once would let a vocabulary argument hold a reach guarantee hostage. |

## Consequences

- **§9.1 and I1 stop being promises.** A reader of `arrangement_functions.h` can see the whole of
  what a Function may touch without reading a function body, which is the standard the issue set.
- **`ProjectHistory::apply` is the only door to the musical content** — the same guarantee as
  before, stated precisely. `ProjectMeta` is not delta-managed: it is written when a document is
  created or loaded, by the module that owns save and load. `title` therefore cannot currently be
  changed after load, which is honest — there is no `rename_project` Function (§5.11) and no UI for
  it. **If undoable Project-metadata editing is ever wanted, it is a superseding decision** — a
  second Op kind over `ProjectMeta`, deliberately added — and never a widening of `Op`.
- **`read()` still returns the document.** Callers that want the musical content say
  `history.read().musical`; `engine::publish` and `engine::bake` take `MusicalContent` directly.
  Persistence keeps receiving the whole `Project`, which is what it should serialise.
- **#54's Project key lands in `MusicalContent`**, in the frame beside tempo and time signature.
  It passes the D1 membership test only if it changes what `bake` produces; if it does not, #54's
  own decision has to say what it is for. The two issues touch different sections and rebase
  cleanly either way.
- **In-flight work needs a mechanical rebase, and it is small.** Every open PR that defines
  Functions is affected: #38 and #39 (`pattern_functions`), #40/#64 (`effect_functions`), and #31
  (`persistence`, which reads `project.format_version` and the musical maps). In each case the
  change is `const Project&` → `const MusicalContent&` in the signature and `project.` →
  `content.` in the body, or `project.musical.` / `project.meta.` at a whole-document caller. No
  logic moves. Whoever rebases second reruns `ctest --preset dev-headless`; a missed site is a
  compile error, not a silent behaviour change.

  This is not a prediction: it has been done once already. #14's recording path merged while this
  decision was being written, bringing a twelfth Function (`add_sample`) and a caller in
  `RecordBar`. Carrying the split over it was a signature, two field paths and their tests — the
  size this bullet claims — and `add_sample` joins the census in `tests/test_reach_rule.cpp` like
  the rest. A Function merged *after* this ADR and left on `const Project&` would not compile,
  because there is no such overload; one written in a new `*_functions.h` fails
  `reach_rule_lint` without anyone adding a rule for it.
- **The obligation is recorded as discharged** in function-surface §8, so the next audit does not
  refile this issue.

## Open items — the project owner may want to answer, but nothing is blocked

1. **Should the user be able to rename their Project, undoably?** Today `title` is set once, is not
   delta-managed, and no Function or menu item changes it. If renaming should be an ordinary
   undoable edit, that is a small deliberate addition (see *Consequences*), not a change to this
   split.
2. **Does the Project key belong to the frame?** Named here only because #54 is deciding it in
   parallel and this ADR fixes the box it will go in.
