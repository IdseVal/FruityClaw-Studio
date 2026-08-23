# ADR-001 — Stock Effect implementations for the MVP

- **Status:** Proposed
- **Date:** 2026-08-23
- **Issue:** #1 — Select the six stock Effect implementations
- **Decided by:** Researcher agent (selection); requires project-owner ratification, see *Open items*
- **Supersedes:** nothing
- **Evidence:** [`docs/research/issue-1-effect-licence-evidence.md`](../research/issue-1-effect-licence-evidence.md)

## Context

The MVP ships six stock Effects: EQ, Compressor, Limiter, Reverb, Delay, Distortion. Each needs
one open-source implementation whose licence is (a) compatible with the project licence and
(b) satisfies the licenseability rule — a user must be able to commercially release music made
with it.

Issue #1 suggested Airwindows for all six, with x42's limiter as a substitute "if the Airwindows
limiter disappoints in listening tests".

Two premises in the issue did not survive verification. Both are recorded in the evidence log and
both change the decision:

1. **Airwindows has no limiter to listening-test.** It has no brickwall or look-ahead limiter at
   all. The substitution is structural, not a matter of taste.
2. **The build-time licence note is inverted for current versions.** The VST3 SDK no longer pulls
   in GPLv3 (it is MIT since 3.8.0). JUCE pulls in *AGPLv3*, not GPLv3 — a stronger copyleft than
   the project licence recorded in the issue.

## Decision

| # | Effect | Implementation | Project | Licence | Source |
|---|--------|----------------|---------|---------|--------|
| 1 | EQ | `Parametric` | Airwindows | MIT | [src](https://github.com/airwindows/airwindows/tree/master/plugins/LinuxVST/src/Parametric) |
| 2 | Compressor | `Pressure6` | Airwindows | MIT | [src](https://github.com/airwindows/airwindows/tree/master/plugins/LinuxVST/src/Pressure6) |
| 3 | **Limiter** | **`peaklim`** | **x42 `dpl.lv2`** | **GPL-3.0-or-later** | [src](https://github.com/x42/dpl.lv2/blob/master/src/peaklim.cc) |
| 4 | Reverb | `Verbity2` | Airwindows | MIT | [src](https://github.com/airwindows/airwindows/tree/master/plugins/LinuxVST/src/Verbity2) |
| 5 | Delay | `TapeDelay2` | Airwindows | MIT | [src](https://github.com/airwindows/airwindows/tree/master/plugins/LinuxVST/src/TapeDelay2) |
| 6 | Distortion | `Distortion` | Airwindows | MIT | [src](https://github.com/airwindows/airwindows/tree/master/plugins/LinuxVST/src/Distortion) |

Five of six from Airwindows (MIT, one licence, one upstream, one idiom). The Limiter comes from
x42 because Airwindows does not contain the required class of processor.

### Why the limiter is not Airwindows

Airwindows' own documentation rules it out. Its category index has no *Limiter* category; the
nearest entries are clippers and one saturating compressor, `BlockParty`, whose entry states:

> "It's not at all about lookahead (in fact it doesn't have any) or preserving tones pristinely."
> — `Airwindopedia.txt`

x42 `dpl.lv2` is the required processor, by its own description:

> "dpl.lv2 is a look-ahead digital peak limiter intended but not limited to the final step of
> mastering or mixing." — `README.md`

Its DSP is confined to `src/peaklim.{cc,h}` — two files, no LV2 or GUI coupling — so adopting it
does not drag in a plugin framework. Its file header licences it "either version 3 of the License,
or (at your option) any later version", i.e. GPL-3.0-or-later.

### Per-effect selection notes

- **EQ — `Parametric`** over `PearEQ` and `CStrip2`. `PearEQ` is a six-band *graphic* EQ and
  `CStrip2` is a whole channel strip; a stock DAW EQ wants parametric bands. `Parametric` is
  described as "three bands of ConsoleX EQ".
- **Compressor — `Pressure6`**, the current refinement of the Pressure line, over `Dynamics3`
  (a vari-mu/expander morph — a character device, not a neutral stock compressor).
- **Reverb — `Verbity2`** over `Galactic3`. `Galactic3` is a special-purpose long/huge reverb;
  `Verbity2` is the general-purpose room/hall.
- **Delay — `TapeDelay2`** over `PurestEcho`. `PurestEcho` is a fixed four-tap echo; `TapeDelay2`
  is the flexible echo a stock delay slot needs.
- **Distortion — `Distortion`**, which bundles several Airwindows distortion algorithms behind
  presets, giving one slot several voices.

## Licence analysis

### Compatibility with GPLv3

| Component | Licence (observed) | GPLv3-compatible | Basis |
|---|---|---|---|
| Airwindows | MIT | Yes | Permissive; absorbs into GPLv3 |
| x42 `peaklim` | GPL-3.0-or-later | Yes | Same licence |
| VST3 SDK >= 3.8.0 | MIT | Yes | Permissive |
| JUCE modules | **AGPLv3** or commercial | **Combination permitted, but see below** | GPLv3 §13 |

GPLv3 §13 permits the combination but does not make the result plain GPLv3:

> "you have permission to link or combine any covered work with a work licensed under version 3
> of the GNU Affero General Public License into a single combined work … but the special
> requirements of the GNU Affero General Public License, section 13, concerning interaction
> through a network will apply to the combination as such."

So a JUCE-based build under the open-source option cannot be described as a GPLv3 work. In
practice AGPLv3 §13's obligation binds only "if your version supports such interaction" over a
network, which a locally-installed DAW does not — the practical burden is close to nil, but the
*stated licence* must be correct. See *Open items*.

### The licenseability rule

Satisfied by all six, and the reasoning is the same for MIT and for GPL: neither licence reaches
the program's output. Per the FSF's own FAQ:

> "The output of a program is not, in general, covered by the copyright on the code of the
> program. So the license of the code of the program does not apply to the output, whether you
> pipe it into a file, make a screenshot, screencast, or video."

and

> "In general this is legally impossible; copyright law does not give you any say in the use of
> the output people make from their data using your program."

No candidate imposes a royalty, a registration step, or a field-of-use limit on rendered audio.
A user may commercially release music made with any of the six.

## Alternatives rejected

- **Airwindows for the Limiter** — rejected on capability, not licence. No look-ahead or brickwall
  limiter exists in the library; its own docs say so.
- **Chowdhury DSP** — rejected. `chowdsp_utils` states "Each module in this repository has its own
  unique license", so it cannot supply a single licence across six categories and would need a
  per-module audit. Its `BYOD` plugin is GPL-3.0 (compatible) but is a guitar-pedal chain, not a
  set of stock mixer Effects.
- **Calf Studio Gear** — rejected. Licence is fine (headers read LGPL "version 2 … or any later
  version", so it upgrades to LGPLv3/GPLv3). Rejected because it is a GTK/LV2 Linux desktop suite
  whose DSP is entangled with its host framework, against a single MIT library that is
  framework-free by design.
- **LSP Plugins** — rejected. Large, high-quality, licence workable (repo `COPYING` is GPLv3 while
  GitHub reports LGPL-3.0 — an inconsistency that would need resolving before use). Rejected for
  the same reason as Calf: heavier integration surface, plus that unresolved licence ambiguity.
- **Mixing several upstreams for best-in-class per category** — rejected. Each extra upstream is
  another licence, another idiom and another update cadence. One upstream for five of six, and a
  second only where capability forces it, is the cheaper shape.

## Consequences

- One licence (MIT) covers five of six Effects; the Limiter adds GPL-3.0-or-later, already
  implied by the project's copyleft licence.
- Airwindows source files are plain C++ against a thin VST2-era shim, so each pick needs its
  processing block lifted into the project's own Effect interface. That is integration work and
  belongs to the architecture issues, not here.
- `peaklim.{cc,h}` is self-contained and portable as-is.
- Attribution: MIT requires the Airwindows copyright notice to ship with binaries. A
  `THIRD_PARTY_NOTICES` file will be needed.

## Open items — require the project owner

1. **`docs/CORE_DOCUMENT.md` is an empty template.** Sections 3.2 (project licence), 3.4 (open
   question 1), 6.1 (licenseability rule) and Appendix B.1 (shortlist) referenced by issue #1 do
   not exist in the repository. This ADR takes the constraints from the issue text itself. Every
   licence conclusion above was verified against primary sources and stands on its own; what could
   *not* be verified is that these are the project's actual stated constraints.

   > **Resolved, 2026-08-23.** `docs/CORE_DOCUMENT.md` is now the full agreed document on `dev`.
   > Sections 3.2 (GPLv3), 3.4 (the six Effects), 6.1 (the licenseability rule) and Appendix B.1
   > (the shortlist) all exist and match the constraints this ADR took from the issue text. No
   > conclusion above changes.
2. **Project licence vs JUCE.** If the project is GPLv3 *and* uses JUCE under its open-source
   option, the stated licence is wrong — it must be AGPLv3, or JUCE needs a commercial licence.
   This ADR does not decide the project licence. Flagged for the architecture track.

   > **Resolved by [ADR-002](ADR-002-technology-stack-and-audio-engine.md), 2026-08-23.** The
   > architecture track took the other branch: the project licence stays GPLv3 as core document 3.2
   > states, and **JUCE is not used**. The selected stack contains no AGPL-licensed dependency, so
   > the Studio ships as a plain GPLv3 work. The concern raised here was correct and decided the
   > stack.
3. **No listening tests were performed.** The five Airwindows picks are argued from first-party
   documentation and category ranking, not from audio. They are defensible defaults, not
   measured winners.
