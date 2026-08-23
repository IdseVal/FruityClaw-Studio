# ADR-003 — The handpicked music-generation model list

- **Status:** Proposed
- **Date:** 2026-08-23
- **Issue:** #3 — Which models go on the handpicked music-generation list
- **Closes:** Open question 3 (`docs/CORE_DOCUMENT.md` §6.4, §6.5, §10)
- **List owner:** Idse Val (project owner)
- **Review cadence:** Quarterly, plus event-driven (see [Curation commitment](#curation-commitment))
- **Deciders:** Project owner
- **Supersedes in part:** Appendix B.3 (see [Corrections](#corrections-to-appendix-b3))

---

## Context

§6.4 decides that music generation is **off by default**, that **no model ships or auto-downloads**
(NON-scope 7), and that the user enables it by picking from a list the project **handpicks**. §6.4
also records the obligation this creates:

> Handpicking the options is a **curation commitment**. Every model on that list carries an implicit
> claim to the user about what they are getting into. The list needs an owner and a review cadence,
> and each entry should state its rights position — not merely its name.

This ADR is that list.

Three constraints bound it:

- **§6.1 (licenseability, load-bearing)** — the Studio exists so users can produce releasable music.
  A model whose output cannot be commercially released is not a candidate.
- **NON-scope 1** — generation is limited to individual **Patterns** and **Samples**, never a whole
  Arrangement.
- **NON-scope 2** — the project never sells or resells compute. Remote models use the **user's own
  API key**.

Per B.3, this ADR covers the **audio model only**. AI-generated Patterns come from the LLM emitting
note data through a Function; no audio model is involved.

### Scope note

All § references are to `docs/CORE_DOCUMENT.md` as merged on `dev`.

---

## Decision

Five entries ship in the settings picker, across the two enablement paths §6.4 requires. Each entry
states its rights position, where it runs, and what leaves the machine.

### Local import path — no API key, nothing leaves the machine

#### 1. Stable Audio 3 (Small SFX / Small / Medium) — **recommended default**

| | |
|---|---|
| **Runs** | Locally. Weights imported by the user. |
| **Leaves the machine** | **Nothing** at generation time. One-time weight download from Hugging Face. |
| **Weights licence** | Stability AI Community License (+ Gemma Terms, see below) |
| **Code licence** | MIT — `Stability-AI/stable-audio-3` |
| **Output rights** | *"You own any outputs generated from the Models or Derivative Works to the extent permitted by applicable law."* |
| **Training data** | Fully licensed. Medium: 1,278,902 recordings — 806,284 AudioSparx (licensed), 472,618 Freesound (CC). Copyrighted music filtered out. |
| **Output** | Audio only. **No MIDI.** |
| **Variants** | Small SFX (sound effects, on-device), Small (≤2 min), Medium (≤6:20) |

**Rights position.** The cleanest on the list. Training data is licensed and documented, and the
user owns the output outright. Free for commercial use **below US $1,000,000 annual revenue**; above
that, *"any licenses granted to You under this Agreement shall terminate"* and an enterprise licence
must be requested. For the Studio's individual-producer audience (§2) that threshold is
comfortable — but it is a real cliff and the settings page must say so.

**Two obligations the user inherits, both non-obvious:**

1. **Attribution.** Distributing the weights or a product using them requires a `Notice` file and
   *"prominently display 'Powered by Stability AI' on a related website, user interface, blogpost, or
   product documentation."* The Studio does **not** distribute the weights (NON-scope 7), so this
   binds the *user* only if they redistribute — not FruityClaw. Worth stating so nobody assumes the
   Studio must carry the badge.
2. **A second licence layer.** The weight repos ship `LICENSE_GEMMA.md` alongside `LICENSE.md`, and
   bundle a `t5gemma-b-b-ul2` text encoder. The download is governed by the Stability Community
   License **and** Google's Gemma Terms of Use. B.3 does not mention this. Both must be surfaced.

**Friction the phrase "no API key" hides.** The repos are **gated** (`gated: auto`). An
unauthenticated fetch of the weights returns **HTTP 401** — *"Access to model
stabilityai/stable-audio-3-medium is restricted. You must have access to it and be authenticated to
access it. Please log in."* The local path therefore needs a **Hugging Face account and two licence
acceptances**. It costs nothing, but it is not frictionless, and the import UI must handle a 401
as an expected state, not an error.

**`stable-audio-3-small` was unreachable during this research** — the HF API returned HTTP 401
(`"Invalid username or password"`) where `-medium` and `-small-sfx` returned HTTP 200. Cause not
established. Treat Small as **unverified** until re-checked.

#### 2. ACE-Step v1.5 — the unencumbered alternative

| | |
|---|---|
| **Runs** | Locally. |
| **Leaves the machine** | **Nothing.** Weight download is **ungated** — no account needed. |
| **Weights licence** | MIT (v1.5) / Apache-2.0 (v1-3.5B) |
| **Code licence** | MIT — `ace-step/ACE-Step-1.5` |
| **Output rights** | Unrestricted. No revenue cap, no attribution, no registration. |
| **Training data** | **Not documented.** See below. |
| **Output** | Audio only. Vocals + instrumental. |

**Rights position — a deliberate trade against entry 1.** On *licence* terms ACE-Step is strictly
better than Stable Audio 3: MIT, no $1M cliff, no attribution, no gate. An unauthenticated range
request for its config returned **HTTP 206** — the weights genuinely download without credentials.

But §6.1 is about whether the user can *release* the music, and that depends on training data as
much as licence text. **ACE-Step publishes no training-data provenance comparable to Stability's.**
A permissive licence on the weights is not a warranty about what the weights learned from. This is
a real and unresolved gap, and it is why ACE-Step is listed **second, not first**, despite the
better paperwork.

It earns its place because it is the only entry with **no gate, no cap, and no second licence
layer** — the honest choice for a user who wants zero accounts and zero thresholds, told plainly
what they are trading for it.

### Remote path — user's own API key

#### 3. ElevenLabs Music

| | |
|---|---|
| **Runs** | Remotely. `POST https://api.elevenlabs.io/v1/music`, `xi-api-key` header. |
| **Leaves the machine** | The **text prompt**. For audio-to-audio/inpainting, **the user's audio**. |
| **Output rights** | Commercially licensed, **with a carve-out on every self-serve tier**. |
| **Training data** | Licensed before launch (Merlin Network, Kobalt). |

**Rights position — read the table, not the marketing.** The docs say Eleven Music is *"cleared for
nearly all commercial uses, from film and television to podcasts and social media videos."* The
binding rights table says otherwise. On **every self-serve tier — Free through Business — and on
Enterprise Music Lite**:

> *"All online and offline commercial use permitted, except film, TV, radio, & Studio Games"*

Only full **Enterprise Music** grants *"All online and offline commercial use permitted"* with no
carve-out. Three further limits that bear on a DAW:

- **Eligibility is capped by headcount.** Free/Starter/Creator/Pro are *"For Individual Use Only"*;
  Scale requires *"fewer than 10 employees"*, Business *"fewer than 50"*. Enrolling in a plan you
  are not eligible for *"may result in suspension or termination of your account and/or loss or
  forfeiture of Outputs."* A studio with 12 people cannot use Scale.
- **Free tier cannot download at all** (*"Not permitted"*), streaming is *"Prohibited"*, and
  attribution is *"Required (denote Eleven Music when distributing)"*. A Free key is useless to a
  DAW that must import a Sample.
- **Music Libraries & Repositories: "Prohibited"** on all self-serve — building a redistributable
  library of generated Samples is out of bounds.

Listed because the training data is licensed and the API is clean. It carries the **most conditions
of any entry**, and the settings page must show the carve-out at the point of key entry — not bury
it.

#### 4. Stable Audio via the Stability AI API — **provisional**

| | |
|---|---|
| **Runs** | Remotely, user's own key. |
| **Leaves the machine** | The text prompt; audio for audio-to-audio. |
| **Output rights / training data** | As entry 1 — same vendor, same licensed dataset. |
| **Status** | **Provisional — endpoint not verified.** |

The remote sibling of the recommended default: same rights story, no local GPU. **The Stable Audio
3 Large endpoint could not be confirmed.** Four candidate paths under `api.stability.ai/v2beta/audio/`
all returned **HTTP 404**, and no first-party API reference for it was reachable. What *is* live is
**`POST /v2beta/audio/stable-audio-2/text-to-audio`** (HTTP 401 unauthenticated — endpoint present,
auth required).

**This entry does not ship until the endpoint is verified against a real key.** Recorded as
provisional rather than dropped, because the rights story is the best available for a remote model.

### Rejected — recorded with reasons

| Candidate | Why not |
|---|---|
| **Udio** | **Structurally incompatible.** Per Udio's own help centre, *"downloading of audio, video, and stems has been disabled"* after the UMG settlement. A DAW must import a file. A model whose output cannot leave its platform cannot supply a Sample — this disqualifies Udio on mechanics before rights are even reached. |
| **Suno** | Grants a licence, not ownership — post-WMG terms removed user "Ownership"; Suno remains the author and grants a perpetual commercial licence. UMG and Sony litigation live, summary judgment heard July 2026. Fails the §6.1 promise. |
| **Meta MusicGen** | Weights are **CC-BY-NC 4.0** — non-commercial. Cannot support commercial release. Fails §6.1 outright. |
| **YuE** | Permissive licence, but oriented to **full-song** generation — pulls against NON-scope 1. Reconsider if a Sample-length mode lands. |

---

## Consequences

### The two paths are not symmetric, and the UI must not pretend they are

§6.4 frames the choice as "local, no API key" vs "remote, paste a key". Measurement says the real
axis is different:

| | Stable Audio 3 | ACE-Step | ElevenLabs |
|---|---|---|---|
| Account needed | HF account | **None** | ElevenLabs account |
| Revenue/size cap | $1M | **None** | Headcount tiers |
| Training data documented | **Yes** | No | Yes |
| Use-case carve-outs | None | **None** | film/TV/radio/Studio Games |

No entry is best on every axis. The settings page should present these as **trade-offs the user
picks between**, not a ranked list with one winner.

### Audio-to-audio is a Rework Function

§8.4 splits Functions into **Directive** (no Project content sent) and **Rework** (Project content
sent). Text-to-audio Sample generation is Directive — only the prompt leaves. But **audio-to-audio,
inpainting and continuation send the user's actual audio**, making them Rework-class by §8.4's own
test. They must be labelled as such in the switchboard (§3.10), or the "my music never leaves this
machine" guarantee quietly leaks.

### The §6.2 warning stands unchanged

Every entry — including Stable Audio 3 — warrants licence terms, not copyright. Purely AI-generated
music is generally not copyrightable regardless of vendor. §6.2's mandatory warning is not softened
by any entry here.

### GPLv3 is not threatened

All inference code is MIT or Apache-2.0 — compatible with the Studio's GPLv3 (§3.2). The restrictive
terms attach to **weights**, which the project never distributes (NON-scope 7). The user imports
them; the licence binds the user, not the distribution.

---

## Curation commitment

**Owner:** Idse Val. Owns entry/exit decisions and the accuracy of every rights claim above.

**Cadence:** **Quarterly** scheduled review — next due **2026-11-23**. Each review re-verifies, by
measurement and not by reading marketing copy: licence text and version, revenue/headcount
thresholds, use-case carve-outs, gating status, endpoint liveness, and litigation status.

**Event-driven review — any of these triggers one immediately:**

- A vendor changes its terms, licence, or rights table.
- A ruling or settlement lands in the Suno/Udio litigation.
- A listed model's weights change gating or licence.
- A listed endpoint starts failing.
- Udio re-enables downloads (revisit the rejection).
- The Stable Audio 3 Large endpoint becomes verifiable (promote entry 4).

**Removal is a first-class action.** If an entry's rights position degrades it comes **off the list**;
the implicit claim §6.4 describes is not one to leave standing while a fix is negotiated. Users with
a removed model already configured are told why.

---

## Corrections to Appendix B.3

Measurement contradicts B.3 on four points. B.3 should be amended:

1. **"an additional licence for advertising, film, TV, games and enterprise distribution"** —
   **advertising is not excluded.** The exclusion list is *"film, TV, radio, & Studio Games"*.
   **Radio is missing** from B.3, and "games" is too broad: it means **Studio Games** — *"video games
   which are commercialised… and made available for download or use through more than one platform"*.
2. **"Udio downloads paused pending its UMG platform"** understates it. Downloads of audio, video
   **and stems** are disabled with no shipped date — a structural disqualifier, not a pause.
3. **"Stable Audio 3's downloadable weights"** is true but incomplete: the repos are **gated**, and
   **Large is API-only**. Only Small SFX, Small and Medium are downloadable.
4. **B.3 omits the Gemma licence layer** on the Stable Audio 3 weights entirely.

B.3's central judgement — that Stable Audio has the cleanest rights story — **survives measurement**
and is adopted here.

---

## Evidence

All claims above were **measured on 2026-08-23**, not taken from documentation. Vendor documentation
is quoted only where the quote *is* the artefact under examination (licence text, rights tables).

**Tooling note.** The role specifies `just-scrape` for external observation. The skill is installed,
but **no `SGAI_API_KEY` is configured** (no environment variable, no `~/.scrapegraphai/config.json`,
no project `.env`) and the CLI prompts interactively, so it cannot run unattended. Evidence was
gathered with direct HTTP requests and WebFetch/WebSearch instead. The skill is not missing — its
credential is.

### Measured observations

| # | Request | Response | Establishes |
|---|---|---|---|
| E1 | `GET huggingface.co/api/models/stabilityai/stable-audio-3-medium` | `200`, `gated: auto`, `license:other`, files include `LICENSE.md`, `LICENSE_GEMMA.md`, `NOTICE`, `t5gemma-b-b-ul2/` | Medium exists, is **gated**, carries a **second Gemma licence** and a Gemma text encoder |
| E2 | `GET .../stable-audio-3-small` | `401 {"error":"Invalid username or password."}` | Small **unverified** — anomaly vs E1/E3 |
| E3 | `GET .../stable-audio-3-small-sfx` | `200`, `gated: auto`, same licence files | Small SFX exists, gated |
| E4 | `GET .../stable-audio-3-medium/resolve/main/model.safetensors` (range 0-1023, unauth) | **`401`** | Weights **not** downloadable without a HF account |
| E5 | `GET .../resolve/main/LICENSE_GEMMA.md` (unauth) | `401` — *"Access to model … is restricted. You must have access to it and be authenticated to access it. Please log in."* | Gate wording, verbatim |
| E6 | `GET .../resolve/main/LICENSE.md` | `200`, 11,852 bytes — Stability AI Community License, *Last Updated: July 5, 2024* | Licence text as quoted: $1M threshold, output ownership, "Powered by Stability AI" |
| E7 | `GET api.github.com/repos/Stability-AI/stable-audio-3` | `license: MIT`, 689 stars, pushed `2026-08-04` | Inference code **MIT** → GPLv3-compatible |
| E8 | `GET api.github.com/repos/ace-step/ACE-Step-1.5` + `raw…/LICENSE` | `MIT License, Copyright (c) 2026 ACEStep`, 12,336 stars, pushed `2026-08-16` | ACE-Step code is **MIT** — secondary sources claiming Apache-2.0 for 1.5 are **wrong** |
| E9 | `GET huggingface.co/api/models?search=ACE-Step` | `ACE-Step/Ace-Step1.5` → `license:mit`; `ACE-Step/ACE-Step-v1-3.5B` → `license:apache-2.0`; **not gated** | Weight licences differ by version; neither gated |
| E10 | `GET huggingface.co/ACE-Step/Ace-Step1.5/resolve/main/config.json` (range, unauth) | **`206`**, 1024 bytes | ACE-Step weights download **without credentials** |
| E11 | `POST api.elevenlabs.io/v1/music` (no auth) | `401` — *"Neither authorization header nor xi-api-key received"* | Endpoint live; auth header name confirmed |
| E12 | `GET elevenlabs.io/eleven-music-model-specific-terms` (raw HTML, 579,661 bytes) | Full Music Commercial Rights table extracted | Every tier/rights quote in entry 3 |
| E13 | `POST api.stability.ai/v2beta/audio/stable-audio-2/text-to-audio` (no auth) | `401 {"errors":["authorization: … invalid or missing header value"]}` | Stable Audio **2** endpoint live |
| E14 | `POST` × 4 candidate Stable Audio **3** paths under `/v2beta/audio/` | **`404`** on all | SA3 API path **not established** — entry 4 provisional |

### Stated unknowns

Recorded as gaps rather than filled with plausible answers:

- **The Stable Audio 3 Large API endpoint.** Not found (E14). Blocks entry 4.
- **Why `stable-audio-3-small` returns 401** where its siblings return 200 (E2).
- **ACE-Step's training-data provenance.** No source located. The central open risk on entry 2.
- **Per-generation API pricing** for either remote entry — not verified against a live key.
- **Whether the Gemma Terms' §3.2 use restrictions** conflict with any Studio use case. The
  restrictions were not retrievable (E5, gated).

None of these blocks the list's *shape*. Entry 4 is marked provisional precisely because one of
them touches it.
