# Evidence log — issue #1, stock Effect implementations

Supports [ADR-001](../adrs/ADR-001-stock-effect-implementations.md). Every claim below is paired
with the request that produced it. Observations were made on **2026-08-23**; licences change, so
re-run before relying on this for a release.

**Instrument note.** The role calls for `just-scrape`. It is installed (`npm i -g just-scrape@latest`,
v1.1.0) but has no credential — no `SGAI_API_KEY`, no `~/.scrapegraphai/config.json` — and
`just-scrape validate` drops into an interactive key prompt that cannot be driven headlessly. For
licence text this is not a downgrade: `curl` against `raw.githubusercontent.com` and the GitHub API
return byte-exact primary text rather than a model's summary of a rendered page, which is what a
licence claim needs. Two non-licence pages (the FSF FAQ, JUCE's rendered licence) were read with
WebFetch and then re-verified against raw text where it mattered. **Someone should still provision
`SGAI_API_KEY`** for future research tasks where a rendered page is the only source.

---

## 1. Airwindows — MIT

**Request**

```
curl -sSL https://raw.githubusercontent.com/airwindows/Airwindows/master/LICENSE
```

**Response** — `HTTP 200`:

```
MIT License

Copyright (c) 2018 Chris Johnson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
...
```

**Corroborated at file level** — the repo LICENSE alone would not prove individual files are MIT:

```
curl -sSL .../plugins/LinuxVST/src/Pressure6/Pressure6.cpp | head -14
```

```c
/* ========================================
 *  Pressure6 - Pressure6.h
 *  Copyright (c) airwindows, Airwindows uses the MIT license
 * ======================================== */
```

**Repo metadata** — `gh api repos/airwindows/Airwindows`:

```json
{"archived":false,"default_branch":"master","full_name":"airwindows/airwindows",
 "license":"MIT","license_name":"MIT License","pushed_at":"2026-08-23T08:58:45Z","stars":1205}
```

Actively maintained — last push the same day as this research.

**Scale** — `gh api repos/airwindows/airwindows/contents/plugins/LinuxVST/src` returns **520**
plugin directories. The issue's "300+ processors" understates it.

---

## 2. Airwindows category coverage — five of six, not six

`Airwindopedia.txt` (1,223,548 bytes) is the author's own consolidated documentation and opens with
a category index. Verbatim, the categories relevant to the six Effects:

| Effect wanted | Airwindows category | Entries (head of list, author's own "order of goodness") |
|---|---|---|
| EQ | **Filter** | BezEQ3, Suzan, SmoothEQ3, PearEQ, PearLiteEQ, RetroBass, FatEQ, … Parametric, … |
| Compressor | **Dynamics** | Dynamics3, Pressure6, Dynamics2, BeziComp, Pop3, … |
| Limiter | *(no such category)* | — |
| Reverb | **Reverb** | kRockstar, kCyberCity, kWoodRoom, … Verbity2, Galactic, Galactic2, … |
| Delay | **Ambience** | ClearCoat, TapeDelay2, Doublelay, PitchDelay, SampleDelay, … PurestEcho, … |
| Distortion | **Distortion** | Distortion, Edge, Dirt, Mackity, Density3, ZOutputStage, … |

There is **no Limiter category**. Searching every entry heading for "limit" returns only:

```
55:   Acceleration is an acceleration limiter that tames edge, leaves brightness.
169:  Air4 extends Air3 with controllable high frequency limiting.
662:  BlockParty is like a moderately saturated analog limiter.
4661: PowerSag is for emulating power supply limitations in analog modeling.
5272: Slew2 works like a de-esser or acceleration limiter: controls extreme highs.
```

An acceleration limiter and a high-frequency limiter are different processors from a peak limiter.
`BlockParty` is the only near-miss, and its own entry disqualifies it:

> "BlockParty acts like a somewhat distorty limiter. **It's not at all about lookahead (in fact it
> doesn't have any)** or preserving tones pristinely. … It's called BlockParty because heavily
> limited stuff sounds like blocks of loudness: it'll get you some of those sounds, but **not as
> cleanly as your classic 'loudness war' limiters**."

**Finding.** The issue frames x42 as a fallback "if the Airwindows limiter disappoints in listening
tests". There is no Airwindows limiter to test. The substitution is forced by capability.

**Existence check** — every plugin named in ADR-001 was confirmed present in the source tree:

```
PRESENT  Parametric   PRESENT  Pressure6   PRESENT  Verbity2
PRESENT  TapeDelay2   PRESENT  Distortion  PRESENT  PearEQ
PRESENT  Galactic3    PRESENT  PurestEcho  PRESENT  BlockParty
```

---

## 3. x42 `dpl.lv2` — GPL-3.0-or-later, and genuinely look-ahead

**Repo metadata** — `gh api repos/x42/dpl.lv2`:

```json
{"archived":false,"default_branch":"master","full_name":"x42/dpl.lv2",
 "license":"GPL-3.0","license_name":"GNU General Public License v3.0",
 "pushed_at":"2026-04-19T22:59:14Z","stars":26}
```

**README** confirms the processor class:

> "dpl.lv2 is a **look-ahead digital peak limiter** intended but not limited to the final step of
> mastering or mixing."
>
> "Threshold. The maximum sample value at the output. -10 to 0 dB in steps of 0.1 dB. **dpl.lv2
> will not allow a single sample above this level.**"
>
> "dpl.lv2 is based on zita-jacktools-1.0.0 by Fons Adriaensen."

**Why the file header was checked.** The README credits derived zita code, and derived code can
carry a different (e.g. GPLv2-only) grant than the repo badge suggests — which would be
*incompatible* with GPLv3. So the header governs, not the badge:

```
curl -sSL https://raw.githubusercontent.com/x42/dpl.lv2/master/src/peaklim.cc | head -25
```

```c
/*
 * Copyright (C) 2010-2018 Fons Adriaensen <fons@linuxaudio.org>
 * Copyright (C) 2021 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */
```

"version 3 … or (at your option) any later version" = **GPL-3.0-or-later**. Compatible. Had it read
"version 2" only, it would not have been.

**Integration surface** — `gh api repos/x42/dpl.lv2/contents/src`:

```
lv2.cc  peaklim.cc  peaklim.h  uris.h
```

The DSP is `peaklim.{cc,h}`; `lv2.cc`/`uris.h` are the LV2 wrapper and are not needed. Adopting the
limiter therefore costs two files, not a plugin framework.

---

## 4. Build-chain licences — the issue's note is inverted for current versions

The issue warns: *"Building with JUCE plus the VST3 SDK can pull GPL3 dependencies into the result
even when the source is MIT."* Both halves were checked. Both have moved.

### 4a. VST3 SDK is now MIT, not GPLv3

```
curl -sSL https://raw.githubusercontent.com/steinbergmedia/vst3sdk/master/LICENSE.txt
```

```
MIT License

Copyright (c) 2026, Steinberg Media Technologies GmbH
```

`gh api repos/steinbergmedia/vst3sdk` → `{"license":"MIT","name":"MIT License"}`.

**When it changed** — `LICENSE.txt` commit history:

```
2026-08-11  VST3 SDK 3.8.1     <- current, MIT
2025-10-20  VST SDK 3.8.0      <- relicensed here
2025-02-28  VST SDK 3.7.13
```

The same file at the 3.7.13 commit (`8b59557`) reads:

> "This Software Development Kit is licensed under the terms of the Steinberg VST3 License, **or
> alternatively under the terms of the General Public License (GPL) Version 3.**"

So the GPLv3-pulling behaviour the issue describes was real **up to 3.7.13 (Feb 2025)** and ended at
**3.8.0 (Oct 2025)**. On any current SDK the concern no longer applies.

### 4b. JUCE is AGPLv3, not GPLv3

```
curl -sSL https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md
```

```
The JUCE Framework modules are dual-licensed under the
[AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html) and the commercial [JUCE
licence](https://juce.com/legal/juce-9-licence/).
```

`gh api repos/juce-framework/JUCE` → `{"license":"NOASSERTION","name":"Other"}` (GitHub cannot
classify the dual grant, which is itself a reason to read the file).

**This is stronger copyleft than the issue assumes, not weaker.** GPLv3 §13, fetched verbatim from
`https://www.gnu.org/licenses/gpl-3.0.txt` (lines 552–561):

```
  13. Use with the GNU Affero General Public License.

  Notwithstanding any other provision of this License, you have
permission to link or combine any covered work with a work licensed
under version 3 of the GNU Affero General Public License into a single
combined work, and to convey the resulting work.  The terms of this
License will continue to apply to the part which is the covered work,
but the special requirements of the GNU Affero General Public License,
section 13, concerning interaction through a network will apply to the
combination as such.
```

And AGPLv3 §13, from `https://www.gnu.org/licenses/agpl-3.0.txt` (lines 540–551):

```
  13. Remote Network Interaction; Use with the GNU General Public License.

  Notwithstanding any other provision of this License, if you modify the
Program, your modified version must prominently offer all users
interacting with it remotely through a computer network (if your version
supports such interaction) an opportunity to receive the Corresponding
Source of your version ...
```

**Reading.** The combination is *permitted*, so nothing is blocked. But the result cannot be
described as a plain GPLv3 work — AGPL terms attach "to the combination as such". The network
obligation is conditional on "if your version supports such interaction"; a locally-installed DAW
does not, so the practical burden is near zero. The exposure is a **mislabelled project licence**,
not a compliance trap. Recorded as open item 2 in ADR-001.

---

## 5. Licenseability — no candidate reaches the audio

Both MIT and GPL stop at the code. FSF FAQ, `https://www.gnu.org/licenses/gpl-faq.html`:

> **"In what cases is the output of a GPL program covered by the GPL too?"**
> "The output of a program is not, in general, covered by the copyright on the code of the program.
> So the license of the code of the program does not apply to the output, whether you pipe it into
> a file, make a screenshot, screencast, or video."

> **"Is there some way that I can GPL the output people get from use of my program?"**
> "In general this is legally impossible; copyright law does not give you any say in the use of the
> output people make from their data using your program."

MIT needs no equivalent argument — it grants unrestricted use outright ("to deal in the Software
without restriction").

**Conclusion.** All six satisfy the licenseability rule. A user can commercially release music made
with them. No candidate carries a royalty, registration, or field-of-use term on rendered audio.

**Scope limit, stated honestly.** This is a reading of licence text, not legal advice, and it covers
only the six selected implementations plus the build chain in section 4. If the project later
bundles impulse responses, samples, or presets, those are separately licensed assets and this
analysis does not extend to them.

---

## 6. Rejected candidates — what was actually observed

| Repo | GitHub licence field | Observed | Note |
|---|---|---|---|
| `calf-studio-gear/calf` | LGPL-2.1 | `COPYING` is LGPL 2.1; file headers say LGPL "version 2 … or (at your option) any later version" | "or later" upgrades cleanly to LGPLv3 → GPLv3. **Compatible.** Rejected on integration cost, not licence. |
| `lsp-plugins/lsp-plugins` | LGPL-3.0 | `COPYING` is **GPLv3**; `COPYING.LESSER` also present | Repo metadata and repo contents disagree. Workable but ambiguous — would need upstream clarification. |
| `Chowdhury-DSP/chowdsp_utils` | NOASSERTION | See below | No single licence. |
| `Chowdhury-DSP/BYOD` | GPL-3.0 | GPL-3.0 | Compatible, but it is a guitar-pedal chain, not stock mixer Effects. |

`chowdsp_utils/LICENSE.md`, verbatim:

> "Each module in this repository has its own unique license. If you would like to use code from
> one of the modules, please check the license of that particular module. … All non-module code in
> this repository (tests, examples, benchmarks, etc.) is licensed under the GPLv3."

This is the decisive fact against Chowdhury DSP for this issue: the property that makes Airwindows
attractive — *one licence over all six categories* — is exactly what `chowdsp_utils` cannot offer.

---

## 7. Stated unknowns

Recorded as unknowns rather than guessed, per the evidence rules:

1. **`docs/CORE_DOCUMENT.md` is an empty template** — every heading present, every section blank,
   marked "Status: EMPTY — run the onboarding interview". Sections 3.2, 3.4, 6.1 and Appendix B.1
   cited by issue #1 **do not exist in this repository**. `grep -rniE "gplv3|airwindows|licenseab"`
   across all repo markdown (excluding `agent-workflow-setup.md`) returns nothing, and there is no
   `LICENSE` file at the repo root. The constraints used here come from the issue text alone.
   *I could not verify that GPLv3 is the project licence.* The compatibility analysis is stated
   against GPLv3 as the issue asserts it, and would need redoing if the real licence differs.
2. **No audio was auditioned.** Zero listening tests. The five Airwindows picks rest on the author's
   own category rankings and descriptions. Ranking a reverb by its documentation is not the same as
   hearing it.
3. **Latency and CPU cost unmeasured.** `peaklim`'s look-ahead implies latency requiring delay
   compensation, and no Airwindows plugin's CPU cost was benchmarked. Both matter to the
   architecture track; neither was in scope here.
4. **Airwindows sources target a VST2-era shim.** Observed from `Pressure6.cpp`
   (`AudioEffectX`, `audioMasterCallback`). The DSP is separable, but "separable" is a hypothesis
   about effort until someone does it — I did not attempt an extraction.
