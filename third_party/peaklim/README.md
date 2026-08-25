# peaklim — vendored source

The Limiter of ADR-001: the look-ahead peak limiter of x42's `dpl.lv2`,
confined upstream to `src/peaklim.{cc,h}` with no LV2 or GUI coupling, kept
**verbatim**.

- **Upstream:** https://github.com/x42/dpl.lv2
- **Commit:** `92c43844160f8f88445a99e6a23e9833456ce05b` (master, 2026-04-19)
- **Licence:** GPL-3.0-or-later (file headers); `COPYING` is the repository's
  licence file, unchanged. It ships in every artefact through
  `THIRD_PARTY_NOTICES`.

Only `src/effects/` may include anything under this directory
(architecture-seams rule 3).
