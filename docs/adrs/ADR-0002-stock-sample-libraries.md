# ADR-0002 — Stock Sample libraries for drums and guitars

- **Status:** Proposed
- **Date:** 2026-08-23
- **Closes:** Open question 2 (`docs/CORE_DOCUMENT.md` section 6.5), issue #2
- **Governed by:** section 6.1 (licenseability rule), section 3.2 (GPLv3), section 3.5 (MVP)
- **Supersedes:** the shortlist in Appendix B.2, which this ADR shows to be partly wrong

> Numbered to match open question 2 rather than by creation order, so the four sourcing ADRs map
> 1:1 onto the four open questions and parallel worktrees cannot collide on a number.

---

## Context

Section 3.5 puts "stock Samples" in the MVP. Section 6.1 is absolute: everything bundled must let
the user commercially release the music they make with it. The owner named **drums** and
**guitars** specifically.

Appendix B.2 proposed *Stargate DAW sample pack* or *Meadowlark factory library* for drums, and
*VCSL* for guitars. Appendix B.2 was a shortlist gathered from documentation, not from
measurement. **Two of its three claims do not survive contact with the actual repositories.**

---

## Decision

Bundle two sources, taking a **curated subset** of each, not the whole library.

### Drums — Stargate DAW sample pack (drum subset)

`https://github.com/stargatedaw/stargate-sample-pack` — CC0 1.0.

Bundle only the drum and percussion folders:

| Folder | Size | Files |
|---|---:|---:|
| `fugue-state-audio/drums` | 11.65 MB | 58 |
| `karoryfer/cymbals` | 12.81 MB | 4 |
| `karoryfer/percussion` | 9.12 MB | 14 |
| `freesound/drums` | 8.63 MB | 37 |
| `karoryfer/snares` | 2.75 MB | 5 |
| `karoryfer/toms` | 2.66 MB | 4 |
| `karoryfer/hihats` | 2.17 MB | 3 |
| `karoryfer/kicks` | 1.65 MB | 5 |
| `stargate/kicks` | 1.13 MB | 5 |
| `stargate/hats` | 0.37 MB | 2 |
| **Total** | **52.9 MB** | **137** |

The remaining ~218 MB of the pack (loops, synth FX, VCSL/VSCO teasers) is **not** bundled.

### Guitars — FreePats CC0 guitar banks

`https://freepats.zenvoid.org/` — the three banks that are CC0 1.0, in SFZ+FLAC:

| Bank | Licence | Measured download |
|---|---|---:|
| Spanish Classical Guitar (nylon-string acoustic) | CC0 1.0 | 4,713,892 B (4.71 MB) |
| FSBS Electric Guitar, clean — bridge, small | CC0 1.0 | 3,096,261 B (3.10 MB) |
| Finger Bass YR (electric bass) | CC0 1.0 | 3,252,347 B (3.25 MB) |
| **Total** | | **11.06 MB** |

### Total added to the default install: **~64 MB**

Comfortably acceptable — smaller than the installer of most DAWs, and an order of magnitude below
the 6.16 GB that bundling VCSL wholesale would have cost.

---

## Evidence

Every claim below was measured against the live source on 2026-08-23, not read from a description.
Method: GitHub / Codeberg REST APIs (`git/trees?recursive=1` gives per-blob byte counts), `curl`
against the published download URLs, and direct reads of `LICENSE` files.

### Licences — read, not assumed

**Stargate sample pack `LICENSE`** — `raw.githubusercontent.com/stargatedaw/stargate-sample-pack/main/LICENSE`
is the verbatim CC0 1.0 Universal text (6,832 bytes), opening `CC0 1.0 Universal`. GitHub's licence
detector reports `NOASSERTION` for this repo; that is a false negative caused by the file's
reflowed whitespace, not a licensing gap.

Its README states the intent plainly:

> "A properly open source, crowd sourced, free, royalty-free, attribution-free sample pack… nearly
> all sample packs either do not allow redistributing samples, or have vague licensing that makes
> distributing samples questionable or difficult."

Redistribution inside a shipped product is the pack's stated purpose, not merely a permitted use.

**Per-sub-pack provenance**, from each sub-pack's own README:

| Sub-pack | Stated basis | Assessment |
|---|---|---|
| `freesound` | "All of the provided samples are licensed under the `Creative Commons Zero`" | Clear |
| `fugue-state-audio` | "completely free, public domain sounds from Fugue State Audio" | Clear |
| `stargate` | "Samples synthesized in Stargate DAW, by the Stargate DAW team" | First-party |
| `karoryfer` | "Drum samples selected from various Karoryfer Samples releases: Big Rusty Drums, Unruly Drums and Swirly Drums" | Resolved below |
| `sgossner` | CC0 teaser from VCSL / VSCO-2-CE | Clear (not bundled) |
| `microlag` | **No licence statement** — a linktree only | **Not bundled** |

**The Karoryfer question, resolved.** Big Rusty Drums and Unruly Drums are commercial Karoryfer
products, and the sub-pack README grants no licence of its own — so the top-level CC0 covering them
needed verification rather than assumption. The commit history settles it: every Karoryfer file was
uploaded by GitHub user `DSmolken` in November–December 2021, with messages such as *"Adding crash
and ride cymbals from Big Rusty Drums and Unruly Drums"* (`6d7d10ce`) and *"Uploading kick samples
from Big Rusty, Unruly and Swirly drums"* (`bdeb8819`). That account's profile reads
`name: D. Smolken`, `company: Karoryfer Samples`, `blog: https://shop.karoryfer.com`.

The rights holder placed his own material into a CC0 repository himself. That is a first-party
dedication, not a third-party redistribution — the strongest form this evidence could take short of
a signed grant.

**FreePats guitar banks.** Each product page carries the sentence *"Published under the terms of the
Creative Commons CC0 1.0 public domain dedication."* — verified individually on the nylon acoustic,
clean electric, distorted electric, and electric bass pages. The electric bass adds its chain of
title: *"Sound samples created by Andrea Biasior from a Yamaha RBX bass guitar. It was sent for
inclusion in FreePats on September 2019, under the terms of the Creative Commons CC0 1.0 public
domain dedication."*

All four download URLs were fetched end-to-end: HTTP 200, byte counts as tabled above. The clean
electric guitar release is dated **2026-08-07** — the project is actively maintained.

### Coverage — counted, not eyeballed

Drum roles across the pack's 358 `.wav` files: **34 kicks, 45 snares, 23 hi-hats, 19 toms,
18 cymbals (crash/ride/china/splash), 10 claps, 46 other percussion.**

`fugue-state-audio/drums` alone supplies **five complete, consistently-named electronic kits** —
`distkit`, `sdbkit`, `synthkit`, `x0xproc1`, `x0xproc2` — each with kick, snare, closed and open
hat, hi/mid/lo toms, crash, ride, clap and a percussion extra. That maps directly onto an
FL-Studio-style step sequencer (section 3.7): one kit fills a channel rack with no gaps. The
Karoryfer folders add an acoustic kit (5 kicks, 5 snares, 3 hats, 4 toms, 4 cymbals) alongside the
electronic ones.

**Enough drums to build a beat: yes**, verified by role count and by kit completeness.

For guitars, the three banks give nylon acoustic, clean electric and electric bass, each
multi-sampled across the neck in SFZ with FLAC compression. **Enough guitar to write a line: yes** —
melody, chords and bass are all playable from the piano roll (section 3.7).

---

## Alternatives rejected

### VCSL for guitars — rejected, the premise is false

Appendix B.2 lists VCSL as *"Acoustic instruments, including guitars."* **VCSL contains no guitar of
any kind.**

Enumerating the full tree of `sgossner/VCSL` at `master` (4,282 blobs, not truncated) yields
**107 instrument folders**. A case-insensitive search across all of them for
`guitar|bass|uke|ukul|lute|banjo|mandol|sitar|charango|balalaika` returns four hits, and every one is
a false positive: *Baroque **Bass** Recorder*, and *Bass Drum 1/2/3*.

The Chordophones section — where a guitar would live — is:

```
Composite Chordophones : Concert Harp, Folk Harp, Strumstick
Zithers                : Grand Piano (Kawai, Kawai Legacy, Steinway B), Upright Piano
                         (Knight, Yamaha), Harpsichord (English, Flemish, French, Italian,
                         Unk), Dan Tranh, Psaltery
```

Harps, pianos, harpsichords and a strumstick. No guitar. The same holds for its sibling
`sgossner/VSCO-2-CE`, whose `Strings` folder is Cello Section, Harp, Solo Contrabass, Solo Violin,
Viola Section, Violin Section — an orchestral library, as its own description says.

VCSL's licence is genuinely CC0 (verbatim `LICENSE`, and a README that says *"you can do whatever
you want with these sounds (even make commercial software), no royalties, no credit, no special
terms"*). It fails on **coverage**, not on rights.

**Size, for the record:** VCSL is **6.16 GB** across 4,282 files — not the ~3.8 GB its GitHub `size`
field implies, since that field is compressed. `Grand Piano, Steinway B` alone is 1.34 GB. Bundling
it wholesale was never viable for a default install regardless of the guitar question.

*Retained as a future option:* VCSL is an excellent source for orchestral and world percussion and
for pianos, and FreePats already redistributes VCSL-derived CC0 percussion. It belongs in an optional
downloadable expansion, not the default install. Out of scope here.

### Meadowlark factory library for drums — rejected, it is empty

`MeadowlarkDAW/meadowlark-factory-library` is CC0 and contains **two files: `LICENSE` and
`README.md`** — 11,187 bytes, zero samples. Its README says the project moved to Codeberg;
`codeberg.org/Meadowlark/meadowlark-factory-library` likewise contains only `LICENSE` (7,048 B) and
`README.md` (3,987 B), repository size 8 KB, last updated 2025-09-22.

The repository is a well-drafted contribution policy — its rules explicitly refuse CC-BY and require
CC0 or public domain — attached to no content. There is nothing to bundle. Meadowlark itself is
described by its own maintainers as *"a (currently incomplete) open-source Digital Audio
Workstation."*

### FreePats Acoustic Drum Kit (MuldjordKit) — rejected on licence

The obvious CC0-adjacent acoustic kit, and genuinely good: *"2 kickdrums, 3 hanging toms, 1 floor
tom, 1 snare, 1 hihat, 2 crash cymbals, 2 ride cymbals, 1 china cymbal."* Its licence line reads
**"Creative Commons Attribution 4.0 license."**

CC-BY requires attribution. Issue #2 puts *"any library requiring attribution"* out of scope and
section 6.1 admits no exceptions. **Rejected**, despite being the best-recorded acoustic kit found.
The Karoryfer acoustic samples cover this need under CC0 instead.

### FreePats Steel-String Acoustic Guitar — rejected on the stated bar, available as a fallback

Licensed **GPLv3-or-later with the FreePats sound-sample exception**, by permission of Gary Campion
of FlameStudios. The exception is the FSF font-exception analogue, edited with Richard Stallman's
help:

> "As a special exception, if you create a composition which uses these sounds, and mix these sounds
> or unaltered portions of these sounds into the composition, these sounds do not by themselves
> cause the entire composition as a whole to be covered by the GNU General Public License."

That exception does preserve the user's ability to release their music, and GPLv3 is already the
project licence (section 3.2), so there is no incompatibility. **But it is neither CC0 nor public
domain**, and issue #2's acceptance criterion is explicit on that point. Excluded from the default
bundle. FreePats has itself deprecated this licence for new contributions.

Available at 2.74 MB (measured) if the owner later decides a steel-string is worth accepting a
non-CC0 licence for. **That is the owner's call to make consciously, not one to slip in.**

### The `microlag` sub-pack — excluded

61.6 MB of loops inside the Stargate pack whose README is a greeting and a linktree, with no licence
statement of its own. Covered by the repo's top-level CC0, but with no independent corroboration and
no first-party-upload evidence of the kind that settles the Karoryfer case. Loops are outside the
drums-and-guitars scope of this ADR, so nothing is lost by leaving it out. Excluding it costs nothing
and removes the pack's one unverified corner.

### Bundling whole libraries rather than subsets — rejected

Taking the Stargate pack whole is 197 MB downloaded / 271 MB on disk for ~218 MB of loops and teasers
the MVP does not need. Taking VCSL whole is 6.16 GB. Subsetting is what makes a ~64 MB default install
possible, and both sources' CC0 terms permit it without conditions.

---

## Consequences

**Good.**

- The default install carries ~64 MB of content, every byte of it CC0 or public domain. Section 6.1's
  promise holds unconditionally out of the box, with no attribution UI, no royalty accounting, and no
  negotiated agreement anywhere in the product.
- This composes with section 6.4: a default installation has no AI-generated content and no means of
  producing any, and now also no bundled content with a licence caveat. There is no mixed state to
  explain to the user.
- No per-sample licence review is needed at build time. Two sources, two licences, both CC0.

**Costs and follow-ups.**

- **No steel-string acoustic guitar** in the default set. Nylon, clean electric and bass only. If the
  owner wants steel-string, this ADR must be revisited to accept GPL+exception.
- **No distorted electric guitar** bundled. FreePats has one under CC0, but the smallest form is
  129 MiB — twice the whole rest of the bundle. Section 3.4 ships a Distortion Effect; running the
  clean electric through it is the intended path.
- The bundled subsets must be **vendored into the repository or a release artefact**, not fetched at
  build time from GitHub and zenvoid.org. CC0 permits this and it removes a build-time dependency on
  two third-party hosts.
- Record the provenance of each bundled file (source pack, original filename) in a manifest. CC0
  requires no attribution, but knowing where a sample came from is an engineering need, not a legal
  one — and it is what makes a future licence audit cheap.
- `docs/CORE_DOCUMENT.md` section 6.5 and Appendix B.2 need updating once this is accepted.
  **Appendix B.2's VCSL row is factually wrong and should be corrected rather than merely
  superseded**, so the error is not re-derived by a later reader.

---

## Sources

All retrieved 2026-08-23.

| What | Where |
|---|---|
| Stargate sample pack | `https://github.com/stargatedaw/stargate-sample-pack` |
| — its CC0 `LICENSE` | `raw.githubusercontent.com/stargatedaw/stargate-sample-pack/main/LICENSE` |
| — Karoryfer upload commits | `api.github.com/repos/stargatedaw/stargate-sample-pack/commits?path=stargate-sample-pack/karoryfer` |
| — contributor identity | `api.github.com/users/DSmolken` |
| VCSL | `https://github.com/sgossner/VCSL` (tree at `master`, recursive) |
| VSCO-2-CE | `https://github.com/sgossner/VSCO-2-CE` |
| Meadowlark factory library | `github.com/MeadowlarkDAW/meadowlark-factory-library`, `codeberg.org/Meadowlark/meadowlark-factory-library` |
| FreePats licence policy | `https://freepats.zenvoid.org/licenses.html` |
| FreePats nylon guitar | `https://freepats.zenvoid.org/Guitar/acoustic-guitar.html` |
| FreePats steel-string | `https://freepats.zenvoid.org/Guitar/steel-acoustic-guitar.html` |
| FreePats electric guitar | `https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html` |
| FreePats electric bass | `https://freepats.zenvoid.org/ElectricGuitar/clean-electric-bass.html` |
| FreePats acoustic drum kit | `https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html` |

> Not legal advice. CC0 and the licence texts quoted are reproduced as published by their sources;
> the reasoning about what they permit is engineering judgement, not counsel.
