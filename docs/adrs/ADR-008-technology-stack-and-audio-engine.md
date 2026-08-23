# ADR-008 — Technology stack and audio engine

- **Status:** Proposed — becomes **Frozen** on merge to `dev`
- **Date:** 2026-08-23
- **Issue:** #8 — Select the technology stack and audio engine
- **Decided by:** Architect agent
- **Supersedes:** nothing
- **Resolves:** ADR-001 open item 2 (project licence vs JUCE)
- **Evidence:** [`docs/research/issue-8-stack-licence-evidence.md`](../research/issue-8-stack-licence-evidence.md)
- **Companion spec:** [`docs/specs/architecture-seams.md`](../specs/architecture-seams.md) — the seams this decision creates

## Context

Core document 3.1, 3.2 and 3.6 fix the constraints and leave the stack open:

| Constraint | Source | What it forecloses |
|---|---|---|
| Native, local, not a browser application | 3.1 | Every web-shell stack |
| Windows, macOS and Linux each get a download | 3.1 | Any single-platform toolkit |
| GPLv3 | 3.2 | Anything not GPLv3-compatible — **including anything more strongly copyleft** |
| Must eventually host VST3 and/or CLAP Plugins | 3.6 | A stack that cannot reach a C++ plugin ABI |
| Audio input recording; MIDI hardware post-MVP | 3.7, 3.6 | Output-only audio backends |
| GitHub-first distribution | 3.3 | Anything needing a store to reach users |
| Usable by competent producers, not a toy | 2 | Toolkits with weak text input, accessibility or native integration |
| The product name must stay cheap to change | 8.1, 9.8 | Baking any name into identifiers, paths or file formats |

Two further constraints are not in the issue but bind the choice just as hard:

- **1.1a — the Studio is a fully functional DAW with no AI configured.** The editing surfaces have
  to be good on their own merits, so the UI toolkit is not a place to economise.
- **8.3 — a CLA is in force to keep a future proprietary relicense possible.** That option only
  survives if every copyleft dependency could in principle be swapped out.

The industry default for this problem is JUCE, and the obvious move is to reach for it. That move
does not survive contact with 3.2. The rest of this decision follows from that.

## Decision

### The stack

| Layer | Choice | Version floor | Licence | GPLv3-compatible |
|---|---|---|---|---|
| Language | **C++20** | — | — | — |
| Build system | **CMake + Ninja** | CMake 3.24 | BSD-3-Clause | Yes |
| User interface | **Qt 6** (Widgets, with custom-painted editor canvases) | current feature release, floor 6.5 | LGPLv3, also offered under GPLv3 | Yes |
| Audio device I/O | **PortAudio** | pinned commit (see *Consequences*) | MIT | Yes |
| Windows low-latency driver SDK | **Steinberg ASIO SDK**, behind `PA_USE_ASIO` | 2.3.3+ | GPL-3.0-or-later (dual) | Yes — same family |
| Audio file read/write | **libsndfile** | 1.2 | LGPL-2.1-or-later | Yes |
| Sample-rate conversion | **libsamplerate** | 0.2 | BSD-2-Clause | Yes |
| MIDI hardware *(post-MVP)* | **RtMidi** | 6.0 | MIT plus a non-binding request | Yes |
| Plugin hosting *(post-MVP)* | **VST 3 SDK 3.8+** and **CLAP** | 3.8 / 1.2 | MIT / MIT | Yes |
| Unit tests | **Catch2 v3** | 3.5 | BSL-1.0 | Yes |
| CI and release | **GitHub Actions** | — | — | — |

Every licence in that table was read from the project's own licence file or licensing page, not
recalled. The verification, with quotations, is in the evidence log.

**The whole set is free of AGPL.** The Studio can therefore be distributed as a plain GPLv3 work,
exactly as core document 3.2 states, with no footnote.

### Low-latency audio path, per platform

PortAudio is a multiplexer over native host APIs, not a layer that replaces them. The Studio
selects, in this order, and exposes the choice to the user in settings:

| Platform | Preferred | Then | Fallback | Realistic round-trip |
|---|---|---|---|---|
| **Windows** | **ASIO** (device driver required) | **WASAPI exclusive**, then **WDM-KS** | WASAPI shared, DirectSound | ASIO 1–5 ms; exclusive under 10 ms; shared 20–40 ms |
| **macOS** | **CoreAudio** (HAL) | — | — | 3–10 ms, no third-party driver needed |
| **Linux** | **JACK** | **ALSA** direct (`hw:`) | PulseAudio | JACK/ALSA 3–10 ms |

Three notes that keep this honest:

- **PipeWire needs no backend of its own.** It presents both an ALSA device and a JACK-compatible
  API, so the JACK and ALSA paths above cover a PipeWire system. This is a fact about PipeWire, not
  an assumption about it.
- **Shared-mode WASAPI and PulseAudio are convenience fallbacks**, present so the Studio makes sound
  on a machine with no configuration. They are not the product's latency claim, and the settings UI
  should say so rather than quietly leaving the user on a 40 ms path.
- **The ASIO trademark is not used.** The SDK is taken under GPLv3; displaying the ASIO name or logo
  would pull in Steinberg's usage guidelines. Not displaying it costs nothing and keeps core
  document 8.1 clean.

`PA_USE_ASIO` is a Windows-only, default-on CMake option (`FCS_ENABLE_ASIO`). It can be switched off
to produce a build with no Steinberg code in it at all — useful for distributions with their own
policies.

### How third-party Plugin hosting arrives later without a rewrite

This is the acceptance criterion that most constrains the design, so it is answered structurally
rather than by intention.

**There is exactly one interface for anything that makes or changes audio: `Processor`.** The six
stock Effects of ADR-001 are adapters over it. Instruments are adapters over it. Post-MVP, a hosted
VST3 Plugin and a hosted CLAP Plugin are two more adapters over the same interface. Nothing above
the seam learns a new concept when they arrive.

That only holds if the interface stays narrow enough for a plugin to fit behind. Four rules are
frozen, and they are frozen *now*, while there is no code to argue with:

1. **Parameters are declared, never exposed as fields.** A `Processor` publishes a flat list of
   `ParameterDescriptor { stableId, name, range, defaultValue, unit }` and is driven through
   `setParameter(stableId, value)`. Both plugin formats already work this way; so does every
   Airwindows pick in ADR-001.
2. **State is opaque bytes.** `saveState()` and `loadState()` return and accept a blob the host
   never interprets. A VST3 or CLAP plugin's state is opaque by definition; if the stock Effects
   are allowed to be special, the seam bends when the plugins arrive.
3. **No host-internal pointers, no Qt types, and no allocation cross the interface.** This is the
   rule that buys the most: an interface that only moves buffers, parameter values and byte blobs
   can be satisfied by an adapter that marshals across a **process boundary**. Out-of-process plugin
   hosting — the thing that stops a crashing third-party Plugin from taking the user's Project with
   it — therefore stays available as a later decision instead of becoming a rewrite.
4. **Plugin SDK headers appear in exactly one directory.** `src/plugins/` and nowhere else. The
   directory is reserved in the layout (companion spec, section 1) and is to carry a README stating
   the rule, so that the first developer to reach for a VST3 header has somewhere obvious to put it
   and no excuse for putting it elsewhere.

The plugin format SDKs are already GPLv3-compatible and free of registration (VST 3.8 is MIT since
October 2025; CLAP has been MIT since 2022), so nothing about the post-MVP feature needs a licence
decision revisited. It needs an adapter written.

### Build and distribution

One `CMakePresets.json` drives local builds and CI identically, so a failure reproduces without
reading a workflow file.

| Platform | Toolchain | Artefact | Notes |
|---|---|---|---|
| **Windows** | MSVC 2022, x64 | portable `.zip` **and** an installer | `windeployqt` stages Qt; installer via Inno Setup |
| **macOS** | AppleClang, universal 2 (arm64 + x86_64) | `.app` in a `.dmg` | `macdeployqt`; signing and notarization — see *Open items* |
| **Linux** | GCC or Clang, built on the oldest `ubuntu-*` runner GitHub still offers, for the lowest practical glibc floor | **AppImage** | Self-contained single file, which is what GitHub-first distribution wants. Flatpak later if asked for. |

Platform floors: Windows 10 1809, macOS 12, glibc as above.

- **Dependencies:** Qt via `aqtinstall` on CI and system Qt on Linux distributions; everything else
  through CMake `FetchContent` pinned to explicit commit SHAs, never to tags or branches. Airwindows
  and `peaklim` sources from ADR-001 are **vendored** under `third_party/`, because ADR-001 already
  established their processing blocks get lifted into the project's own `Processor` interface rather
  than consumed as libraries.
- **Release:** a git tag fires one workflow, a three-platform matrix builds, and the artefacts plus
  SHA-256 checksums attach to a GitHub Release. A `THIRD_PARTY_NOTICES` file — required by ADR-001
  for Airwindows' MIT notice and now also by PortAudio, Qt, libsndfile, libsamplerate and the ASIO
  SDK — is generated as part of the build and shipped in every artefact.
- **Every pull request** builds all three platforms and runs the test suite. The headless modules
  (`core`, `engine`, `persistence`) are testable with no audio device and no display, which is what
  makes that affordable — see the companion spec.

### Naming disposability — a build-level guarantee

Core document 8.1 and 9.8 require that the name stay cheap to discard. Intent is not enough; a
mechanism is frozen instead. CMake carries two separate variables:

- `FCS_PRODUCT_NAME` — **display only**, disposable, may change in one commit.
- `FCS_APP_ID` — **stable, neutral, never shown to a user.**

The bundle identifier, installer upgrade GUID, config and data directories, Project-file magic
number and future Plugin identifiers derive **only** from `FCS_APP_ID`. No source file, resource or
packaging script may contain a product name literal. Until the owner picks the `FCS_APP_ID` value
(see *Open items*), nothing may hardcode any name anywhere — which is exactly the guarantee 8.1
asks for, arriving before the first line of code rather than after.

## Alternatives rejected

### JUCE 8 / 9 — rejected on licence

The default choice, and the one this ADR spent the most effort on. `LICENSE.md` on JUCE master:
"The JUCE Framework modules are dual-licensed under the AGPLv3 and the commercial JUCE licence."

GPLv3 section 13 *permits* combining GPLv3 and AGPLv3 work — so this is not a licence conflict, and
it is worth saying plainly that a JUCE build would be lawful. What it would not be is GPLv3. The
combined work must be distributed with AGPLv3's section 13 obligations attached, and core document
3.2 states the licence as GPLv3. A stack decision does not get to silently amend an owner decision.

Two further costs, either of which would be enough on its own:

- **A commercial JUCE licence cannot rescue an open-source project.** It would cover binaries the
  project itself ships; every contributor and every user who builds from source is using JUCE under
  AGPLv3 regardless. The problem is structural, not commercial.
- **It forecloses the CLA's purpose.** Core document 8.3 adopts a CLA to keep a proprietary
  relicense possible. AGPLv3 at the centre of the application removes that option permanently.

This is the resolution of ADR-001's open item 2, which flagged the question and correctly declined
to answer it.

### JUCE 7, pinned at its last GPLv3 release — rejected on maintenance

The licence works: JUCE 7's open-source option was GPLv3. But it is an abandoned branch. A project
whose stated success condition is "as long as the owner has energy for it" (core document 1.4) would
be building on a framework that will not gain support for future Windows and macOS releases and will
not receive security fixes. This trades a licence problem for a slower, less visible one.

### Tracktion Engine — rejected twice over

A GPLv3-or-later DAW engine, and a genuine head start: transport, tracks, clips, plugin hosting,
already written.

- **Transitively AGPL.** Tracktion Engine is a JUCE module. Taking it under GPLv3 still links JUCE,
  whose open-source option is AGPLv3. The first rejection applies unchanged.
- **It would pre-empt decisions the owner has already made.** It brings its own Edit/Clip/Track
  model and its own undo. Core document 3.9 specifies a *linear delta history with Word-style
  discard-on-branch*, and issues #5 and #7 exist to design the Project data model and that history.
  Adopting an engine means inheriting its answers to questions the core document has already
  answered differently.

### Rust — rejected on ecosystem fit, not on merit

Rust's real-time safety story is genuinely better than C++'s, and this was the closest call after
JUCE. Three specific frictions decided it:

- **The stock Effects are C++.** ADR-001 selected five Airwindows processors and x42's `peaklim`,
  all plain C++ that must have their processing blocks lifted into the Studio's own `Processor`
  interface. In Rust that becomes port-or-FFI for all six, before the MVP's first Effect works.
- **VST3 is a C++ ABI.** CLAP's pure-C ABI is straightforward from Rust; VST3's COM-style C++ ABI
  realistically wants the C++ SDK. Core document 3.6 names VST3 *and/or* CLAP, so a CLAP-only host
  is arguably in scope — but choosing a language that makes the dominant format hard is a large bet
  on a format war, taken before the MVP, to buy nothing the MVP needs.
- **Four custom editing surfaces need a mature desktop toolkit.** Core document 3.7 requires a step
  sequencer, a piano roll, an Arrangement view and a Sample browser, and 1.1a says these must stand
  on their own merits with no AI. Rust's GUI ecosystem does not yet offer a Qt-class answer for text
  input, IME, accessibility and native dialogs.

Recorded rather than dismissed: if the Studio's DSP layer is ever rewritten, `Processor` is a C ABI
away from being Rust-implementable behind the same seam.

### Electron, Tauri, or any web-shell — rejected by the core document

Core document 3.1 says "Native. Local. Not a browser application." Independently, a garbage-collected
runtime with no real-time thread guarantees cannot deliver the latency table above.

### Dear ImGui — rejected on fitness for the users named in section 2

MIT, small, popular in audio tooling, and a real temptation for four heavily custom-drawn editors.
Immediate-mode rendering makes text input, IME, screen-reader accessibility, native menus and native
file dialogs into problems the project would have to solve itself. Core document 2 requires a Studio
a competent producer does not outgrow, and 1.1a requires editing surfaces that stand on their own.

### GTK4 — rejected on platform parity

Core document 3.1 gives Windows, macOS and Linux equal standing. GTK's Windows and macOS backends
are second-class in appearance and integration.

### miniaudio and libsoundio — rejected on capability

Both are excellent and permissively licensed. Neither has an ASIO backend. Core document 3.2's
licence decision deliberately unlocked ASIO; a backend that cannot reach it discards that.

### RtAudio — not rejected; held as the pre-qualified alternate

MIT-compatible, actively released, and the same author's RtMidi is selected here for post-MVP MIDI.
It is not the default only because its README documents its Windows APIs as DirectSound, ASIO and
WASAPI without documenting a WASAPI exclusive-mode or WDM-KS path, and those are the documented
sub-10 ms routes on Windows. That is an absence of documentation, not proof of absence — so RtAudio
is recorded as the qualified second adapter behind the `AudioDevice` seam rather than as a loser.

### Writing WASAPI, CoreAudio and ALSA directly — rejected for now, kept available

Three times the platform code before the Studio makes its first sound, and the seam means it can be
adopted per-platform later if PortAudio disappoints on one of them. Deferred, not closed.

## Consequences

### Good

- **The project can state its licence as GPLv3 without a footnote.** No AGPL anywhere in the tree.
- **The realtime path is short.** No framework layer between the Studio's mixing loop and the native
  host API; PortAudio is a thin multiplexer.
- **Plugin hosting is an adapter, not a project.** The `Processor` rules above are what make that
  claim checkable rather than hopeful.
- **The domain is testable without a display or an audio device.** `core`, `engine` and
  `persistence` link neither Qt nor PortAudio, so most of the Studio's logic is unit-testable in CI.
- **Both remaining copyleft dependencies are replaceable.** Qt has substitutes and libsndfile has
  permissive alternatives, so core document 8.3's relicense option stays technically open. It would
  be expensive. It would not be impossible.

### Costs and risks, stated plainly

- **PortAudio's last tagged release is v19.7.0 from April 2021**, with the v19.8 milestone still open
  in August 2026. Mitigated three ways: pin an explicit commit rather than a tag, as Audacity and
  Mixxx do; vendor the pinned source so a disappearing upstream cannot break a build; and keep the
  `AudioDevice` seam narrow enough that RtAudio or a native backend can replace it. **Review at the
  first sign the snapshots stop.**
- **Qt is a heavy dependency.** Roughly 40–60 MB added to each artefact after `windeployqt` and
  `macdeployqt`, and a CI cache to manage.
- **Qt LTS patch streams are commercial-only.** Pinning "6.8 LTS" for stability would give an
  open-source project a branch whose fixes it cannot receive. The project therefore tracks the
  current feature release, with a floor of 6.5, and accepts a periodic upgrade tax as the price of
  receiving fixes at all.
- **macOS artefacts are unusable without notarization.** Gatekeeper blocks un-notarized apps
  regardless of format or licence. This is a recurring cost and an owner decision — see *Open items*.
- **Every C++ hazard is now the project's own problem.** No framework is catching use-after-free,
  data races or allocations on the audio thread. The companion spec makes the realtime rules
  explicit; CI should carry ASan/UBSan and TSan jobs from the first commit rather than after the
  first heisenbug.
- **`THIRD_PARTY_NOTICES` grows.** ADR-001 already required it for Airwindows. It now also carries
  PortAudio, Qt, libsndfile, libsamplerate, the ASIO SDK, RtMidi and, post-MVP, the plugin SDKs.
  Generated at build time so it cannot drift.

### What this ADR does *not* decide

Named so no one treats silence as permission:

- The **Project data model** — issue #5. This ADR fixes only that it lives in `src/core/` and links
  neither Qt nor any audio library.
- The **Function surface and Function file generation** — issue #6. This ADR fixes only that the
  Assistant reaches the engine through the same command seam the UI uses, and never directly.
- The **delta history** — issue #7. This ADR fixes only that deltas are applied on the message
  thread, never on the audio callback thread.
- **Qt Widgets versus Qt Quick for individual surfaces.** Widgets is the frozen default for
  application chrome; a UI issue may choose otherwise for a specific editor canvas, inside the seam,
  without reopening this ADR.
- **In-process versus out-of-process Plugin hosting.** Deliberately left open — rule 3 above is what
  keeps it open.

## Open items — require the project owner

1. **`FCS_APP_ID` value.** A short, neutral, permanent identifier with no relation to any product
   name, used for bundle IDs, config paths and file magic. Recorded as OPEN rather than filled with
   a plausible default, per the core document's own discipline. **Not blocking:** code depends on
   the symbol, not the string.
2. **Apple Developer Program membership (about 99 USD/year) for macOS signing and notarization.**
   Without it, macOS users get a Gatekeeper block on every download. Options: pay and store the
   Developer ID certificate in GitHub Actions secrets; or ship unsigned with documented `xattr`
   instructions and accept the friction. **This is a budget question, not a technical one, and it
   should be answered before the first macOS release rather than at it.**
3. **Confirmation that a heavier UI dependency is acceptable** given that core document 1.1a makes
   the editing surfaces load-bearing. Qt is the reason the artefacts are ~50 MB rather than ~10 MB.
   Flagged, not assumed.
