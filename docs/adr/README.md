# Architecture Decision Records

One file per decision, numbered in the order they were accepted:
`NNNN-short-slug.md`.

An ADR records a decision that would otherwise be re-argued: what was decided, why, what was
rejected and on what grounds. It does not restate the core document — it cites it.
`docs/CORE_DOCUMENT.md` stays the single source of truth for intent; an ADR is where a
choice *within* that intent is fixed. No ADR may contradict the core document silently.

Status values: **Proposed** (awaiting the owner) → **Accepted** → **Superseded by ADR-NNNN**.
A superseded ADR is kept, never deleted; the reasoning is the point.

Where a decision rests on research, the evidence lives beside it in `docs/research/` and the
ADR links to it, so a reader can check the ground rather than take the conclusion on trust.

| ADR | Title | Status |
| --- | --- | --- |
| [0001](0001-llm-provider-for-the-assistant.md) | LLM provider support for the Assistant | Proposed |
