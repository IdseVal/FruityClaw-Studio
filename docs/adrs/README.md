# Architecture Decision Records

One file per decision, named for the **Issue that produced it**, not for the order it was
accepted in: `ADR-<issue#>-<slug>.md`. So `ADR-004-…` is the decision taken for Issue #4.
Numbering by issue keeps the ADR, the issue and the branch pointing at each other, and it
means two ADRs written in parallel cannot collide on a number.

An ADR records a decision that would otherwise be re-argued: what was decided, why, what was
rejected and on what grounds. It does not restate the core document — it cites it.
`docs/CORE_DOCUMENT.md` stays the single source of truth for intent; an ADR is where a
choice *within* that intent is fixed. No ADR may contradict the core document silently.

Status values: **Proposed** (awaiting the owner) → **Accepted** → **Superseded by ADR-NNN**.
A superseded ADR is kept, never deleted; the reasoning is the point.

Where a decision rests on research, the evidence lives beside it in `docs/research/` and the
ADR links to it, so a reader can check the ground rather than take the conclusion on trust.

One decision per ADR, with one test for bundling several: they may share a file only when
adopting one without the others would be unsafe. If a reader could act on decision 1 and skip
decision 2 and end up somewhere the ADR argues against, they belong together.

| ADR | Issue | Title | Status |
| --- | ----- | ----- | ------ |
| [ADR-001](ADR-001-stock-effect-implementations.md) | #1 | Stock Effect implementations for the MVP | Proposed |
| [ADR-004](ADR-004-llm-provider-for-the-assistant.md) | #4 | LLM provider support for the Assistant | Proposed |
