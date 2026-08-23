# Core document

> Populated by deep interview with the project owner. Nothing here is inferred.
> Every statement below is traceable to something the owner said. Gaps are recorded as
> OPEN, never filled with a plausible default.
>
> **Status: AGREED — 2026-08-22, after seven interview rounds and two research passes.**
> Confirmed by the owner (Idse Val). All questions of intent are closed; the four remaining
> sourcing selections are delegated to research Issues and do not block the MVP's shape.
> This document is **living**: when a decision changes it, it is updated here first and the
> change log says what changed. No spec may contradict it silently.
>
> **The vocabulary in section 5 is binding.** From round 4 onward this document, and every
> artefact derived from it, uses those terms and no synonyms.

---

## 1. Purpose and success criteria

### 1.1 What it is

FruityClaw-Studio is an **open-source (GPLv3), native, local, cross-platform** digital audio
workstation modelled on FL Studio, with agentic AI integration built in rather than bolted on.

An **Assistant** has access to the musical contents of the open **Project** and operates them
through narrowly-scoped **Functions**. It can generate **Patterns**, import files, and delegate to
**Sub-agents** that can generate music from a prompt, place a given **Sample** into a Pattern, or
download melody and beat Patterns.

### 1.1a The Studio stands on its own (decided, round 7)

**FruityClaw-Studio is a fully functional DAW with no AI configured.** The Assistant is a layer on
top of a complete product, not infrastructure the product depends on.

- With no keys configured, every editing surface, Effect, Sample and recording feature works.
- **On first open** the user is offered the chance to paste a private key, with a **clear message
  that a key is required for the Assistant to work.**
- Declining is a first-class path, not a degraded one.

> Owner: "Yes, it is a fully functional DAW, but on first open, users will be given the option to
> paste a private key, with a clear message that for the agent to work a key is required."

**Consequence for everyone building this:** the editing surfaces must be good on their own merits.
The Assistant cannot be the reason a workflow is acceptable, because a large share of users will
never turn it on. This resolves the ambiguity in "built in rather than bolted on" — the AI is
deeply integrated *when present*, and wholly absent when not.

### 1.2 The intent behind the AI

The Assistant is **not** a "prompt a song, receive a song" system. The user prompts the
*individual components* of the Arrangement and composes them together themselves. The user stays
the author.

> Owner: "not by prompting and have the AI make a whole song, but by prompting the individual
> components of the [Arrangement] together, so the agent will be more like a studio assistant that
> helps the user operate the studio."

This is the load-bearing sentence of the project. Every later decision is checkable against it.

### 1.3 Success criteria

Success is **functional completeness of the MVP**, not adoption or revenue. The project succeeds
when the following are true and working:

1. A basic music editor exists and is usable.
2. Stock Samples ship with it.
3. The stock Effect set (section 3.4) ships with it.
4. Recording capability works.
5. The Assistant can operate the entire Studio's musical content — constructing the data objects
   that make up beats, creating Patterns, matching Samples to Patterns, mutating existing
   Patterns — through hard-bound Functions.
6. Music produced with the stock content is licenseable by the user.

**Accepted as stated.** For an open-source tool that sells nothing, capability completeness is a
legitimate definition of success. Adoption is explicitly *not* a success criterion at this stage.

### 1.4 Failure criteria — none, deliberately

There is **no stop condition**. The project runs as long as the owner has energy for it.

> Owner: "we will work on it as long as I still have energy for it, the base success parameters are
> substantial but very achievable, so I don't expect we need to stop soon."

Recorded as a deliberate position, not an unanswered question. **Closed.**

### 1.5 Guiding default

> "Basically, if in doubt, copy what FL Studio does."

Adopted **for behaviour and workflow only**. Explicitly not adopted for name, branding or visual
identity — see section 8.

---

## 2. Target users (decided, round 4)

**Everyone, including competent producers.**

> Owner: "The tool should be for everyone, also competent producers."

The Studio must not be a toy that a skilled user outgrows. The beginner is the *motivating* user —
people without much musical skill, enabled to create music — but not the *only* user, and
simplifications that would block a competent producer are not acceptable.

Also true of all users:

- They supply **their own API keys**. The project does not sell compute or resell model access.
- They are expected to want music they can **licence and release**.

---

## 3. Scope

### 3.1 Platform

**Native. Local. All platforms** — Windows, macOS and Linux each get a download option.

### 3.2 Licence

**GPLv3.**

> Owner: "GPLv3 is fine, whatever works for a truly open-source adaptable piece of software."

This closes the ASIO interaction raised in round 2: the GPLv3 ASIO SDK is licence-compatible with
the project, and low-latency Windows audio is unblocked.

### 3.3 Distribution

- **Initially:** everything through GitHub — releases, sharing copies.
- **Eventually:** a frontend download page. **Not** part of the MVP.

### 3.4 Stock Effect set

The MVP ships with six built-in **Effects**:

1. EQ
2. Compressor
3. Limiter
4. Reverb
5. Delay
6. Distortion

> Owner: "try to find the best available open-source ones, or the most common or industry standard,
> to be part of the stock set of the app."

**OPEN — Idse.** *Which* open-source implementation is used for each. A research task to be run
before the relevant Issues are written. Every candidate must satisfy both GPLv3 and the section 6.1
licenseability rule.

### 3.5 MVP

- basic editing,
- stock Samples,
- the stock Effect set (3.4),
- recording capability,
- Assistant integration that can operate the entire musical content of an open Project,
- full save capability (section 9).

### 3.6 In scope, beyond MVP

- MIDI hardware usable,
- Mixer,
- import of third-party **Plugins**,
- import of Samples,
- import of mixing options,
- frontend download page.

### 3.7 Core editing surfaces

- Project file management.
- Sample management in a sidebar.
- Drum Patterns, FL-Studio-style step sequencer.
- Piano roll for melody input.
- **Arrangement** view where Patterns are assembled into the finished piece.
- Recording from audio inputs.

### 3.8 Assistant authority — complete reach, narrow instruments

- The Assistant has **complete reach** over the musical content of an open Project. Its territory
  is not a small allowlist.
- It acts **only through single-purpose Functions**. Each Function does exactly one thing and
  touches nothing else.
- **Settings are excluded entirely** from the Assistant's reach.
- Individual Functions are **user-toggleable** (section 3.10).

> Owner: "if a user asks the agent to swap the instrument on a melody, the agent should have a
> mutation call that does only that, without otherwise touching the datastructure or pattern."

**Note for whoever builds this.** "The agent should not have many permissions" means *no blunt
tools*, not *a small territory*. A single broad `mutate_project(json)` Function would violate this
even though it is one permission. Breadth of reach comes from having many precise Functions.

### 3.9 History — undo and redo (decided, round 5)

The Studio keeps a **linear delta history** with undo and redo.

- Changes are stored as **deltas**, not snapshots.
- The user gets **undo and redo buttons** to move through them.
- **Branching discards the future:** continuing from an earlier delta discards every delta that
  came after it — the Microsoft Word model, not a tree.

> Owner: "let's store delta's and let's give the user an undo and redo button to toggle through the
> delta's, then when a user continues from a previous delta the ones that came after it get
> discarded, similar to how word works."

**Assistant actions are deltas like any other.** This is load-bearing rather than convenient: since
the user is never shown which Function ran (section 8.4, NON-scope 6), undo is the *only* remedy
available when the Assistant does something unwanted. An Assistant action that could not be undone
would leave the user with no recourse at all.

### 3.10 The Function switchboard

A settings tab lets the user switch individual Functions off.

- The toggle is **Assistant-only**. The user keeps the feature; the Assistant loses the tool.
- **The Function file is rebuilt from the toggles** — a disabled Function is removed from what the
  Assistant is offered, not merely refused at call time.

> Owner: "the settings tab for turning off agent functions only turns these off for agents, meaning
> the agent's function file should be changed based on that."

---

## 4. Explicit NON-scope

1. **No AI whole-song generation.** Permitted only at the level of individual Patterns and Samples.
2. **No hosted compute.** Never sells or resells model access. Users supply their own API keys.
3. **No video export.**
4. **Assistant has no access to settings.**
5. **No frontend download page in the MVP.**
6. **No visible tool-call log.** The user is not shown which Function was invoked (section 8.4).
7. **No music-generation model ships or auto-downloads.** Generation is off by default and must be
   deliberately enabled and configured by the user (section 6.4).

---

## 5. Domain model and vocabulary (fixed, round 4 — binding)

One name per concept. These terms are used everywhere afterwards; synonyms are defects.

| Term | Meaning |
|---|---|
| **Studio** | The application itself |
| **Project** | The whole saved document a user opens |
| **Pattern** | A block of note or step events; covers drum *and* melody |
| **Arrangement** | The timeline where Patterns are assembled into the finished piece |
| **Track** | One lane in the Arrangement |
| **Sample** | An audio file usable in the Studio |
| **Instrument** | Turns notes into audio |
| **Effect** | A **built-in** processor — the six in section 3.4 |
| **Plugin** | A **third-party** VST3/CLAP module. Post-MVP only |
| **Assistant** | The user-facing AI |
| **Sub-agent** | A worker the Assistant delegates to |
| **Function** | One callable operation the Assistant may invoke |
| **Function file** | The manifest of Functions currently offered to the Assistant |
| **Mixer** | Post-MVP signal routing and level control |

### 5.1 Retired terms — do not use

- ~~choreography~~ → **Arrangement**. Owner: "Choreography was incidental, I couldn't find the
  word, I like that you named it Arrangement."
- ~~sound~~, ~~sound file~~ → **Sample**. Owner: "Sample is better."
- ~~plugin~~ used for a built-in processor → **Effect**. The word *Plugin* is now reserved
  exclusively for third-party modules.
- ~~tune~~, ~~song~~ → **Arrangement** (the structure) or **Project** (the document).

---

## 6. Data sources and their constraints

### 6.1 The licenseability rule — load-bearing

> "We will setup the project as such that users can produce licenceable music with it, so we only
> want to use open-source plugins and sounds etc. that will be useable to create licenceable music."

**All bundled content — Samples and Effects alike — must carry licensing that permits the user to
commercially release music made with it.** A hard constraint on every asset that ships, including
the Effect implementations chosen under 3.4.

### 6.2 The AI-generated exception

AI generation of individual Patterns and Samples is permitted, on three conditions:

1. Limited to individual Patterns and Samples, never a whole Arrangement.
2. The user supplies their own API key.
3. **The user is shown a message that what they produce might not be licenseable.**

Two content classes ship with different licensing promises: bundled content that is safe to
release, and AI-generated content that is explicitly not guaranteed.

### 6.3 AI provenance tracking

AI origin is tracked **per Sample and per Pattern** (extended in round 4 — originally Samples only)
and **surfaced visually**:

- a small logo in the corner of the Sample or Pattern,
- and when it is opened up.

> Owner: "Track what was AI created per sound that was created by the AI and visualise this in the
> view by a small logo in the corner of the sound, or when it is opened up." Extended round 4:
> "Yes AI generated patterns too."

Provenance is a property of the data model, not a one-off warning dialog.

### 6.4 Music generation is opt-in and unconfigured by default (decided, round 6)

**No music-generation model ships with the Studio, and none is downloaded automatically.**

- Music generation is **off by default**.
- The functionality is **built everywhere it belongs** — it is gated, not absent.
- To enable it, the user is guided to a settings page where they **choose a model**.
- The available options are **handpicked by the project**. It is not an open field.
- Two enablement paths exist:
  - **Local model import** — e.g. importing Stable Audio 3's downloadable weights. No API key.
  - **Remote model** — the user **pastes their own API key** on that settings page.

> Owner: "let's not make this download standard. Default, music generation is turned off and can be
> turned on in the settings, so we will build all the functionality for it everywhere, but if the
> user wants to use it we guide the user to the setting where he should choose a model for the music
> generation, here the user can import Stable Audio 3, or can choose a different model for the audio
> generation (we will handpick options) and the user then has to paste the api key there themselves."

#### Why this strengthens section 6.1

A default installation contains **no AI-generated content and no means of producing any**. The
licenseability promise in 6.1 therefore holds *unconditionally* out of the box — there is no mixed
state to explain, and the 6.2 caveat only becomes reachable after a deliberate act by the user.
Provenance marks (6.3) can only ever appear on content the user chose to enable.

#### Obligation this creates

Handpicking the options is a **curation commitment**. Every model on that list carries an implicit
claim to the user about what they are getting into. The list needs an owner and a review cadence,
and each entry should state its rights position — not merely its name.

### 6.5 Open

**OPEN — Idse.** Which CC0 or open Sample library is bundled. Candidates in Appendix A.2 / B.2.

**OPEN — Idse.** Which models appear on the handpicked list in 6.4.

---

## 7. External systems

- **LLM provider** for the Assistant and Sub-agents — user-supplied key.
- **Music-generation service** — user-supplied key, optional.
- **Audio device I/O** on Windows, macOS and Linux.
- **MIDI hardware** (post-MVP).
- **Third-party Plugin formats** (post-MVP).
- **GitHub** as the distribution channel.

**OPEN — Idse.** Which specific LLM providers are supported.

---

## 8. Legal, privacy and compliance limits

### 8.1 The name

FruityClaw-Studio is the **open-source working name and is treated as disposable**.

> Owner: "if FL Studio ever complains we will just change it, just like Clawdbot, or OpenClaw or
> whatever did."

**Accepted as a deliberate, informed decision.** For the record: Image-Line themselves abandoned
*FruityLoops* in 2003 after **Kellogg's** — not a music company — challenged the US trademark. Two
possible objectors exist, not one.

**Constraint that follows:** a disposable name must stay cheap to dispose of. The name must not be
baked into Project file formats, file magic numbers, Plugin identifiers, config paths or URLs.

### 8.2 Cloning FL Studio

Copying FL Studio's **workflow and interaction model** is the stated intent and is supported by
*Google v. Oracle* (2021). Copying its **name, logo, or visual identity** is a separate question
that answers differently. The former is adopted; the latter is not.

### 8.3 Contributor licence agreement (decided, round 4)

**A CLA is used.** Owner: "Yes, we do use a CLA."

Rationale recorded so it is not re-litigated: GPLv3 permits selling binaries, support and services
but forecloses a proprietary relicense unless the project holds copyright over all contributions.
The CLA keeps the "might be sold at some point" option open. It must be **in force before the first
outside contribution** — it is effectively impossible to arrange retroactively.

Also noted: hosting proprietary third-party **Plugins** under a GPL host is long-established
practice (Ardour is GPL and hosts proprietary VSTs) but is a debated area of GPL interpretation.
Affects the post-MVP Plugin-import feature, not the MVP.

### 8.4 What leaves the machine (decided, round 4)

Per the owner, what is sent to the LLM API is:

- the user's prompt, and
- an instruction appended by the selected Function, describing the data structure the model must
  return.

> Owner: "The user prompts something, the function appends an instruction that matches the function
> call that was chosen to the API so it knows what datastructure to return. Indeed that is it."

**The user is not told which Function was invoked.** They see only the result in the Studio. This is
a deliberate UX decision, recorded in NON-scope item 6.

#### Two classes of Function (decided, round 5)

Both shapes are permitted, and **the choice of Function is what determines whether Project content
leaves the machine**:

| Class | Example | Project content sent? |
|---|---|---|
| **Directive** | `change_instrument` | **No.** The model chooses the Function and its arguments; the Studio does the work locally. |
| **Rework** | `rework_pattern` ("make this jazzier") | **Yes.** The model must see the existing Pattern to transform it. |

> Owner: "Both should be possible, so it should be possible to run either with a corresponding
> function. Make something jazzier can be run with a rework pattern call, instead of the just
> 'change_instrument' call."

#### Emergent property — the switchboard is also a privacy control

**Adopted, round 7.** Because the Function switchboard (3.10) rebuilds the Function file from user
toggles, and because only Rework-class Functions transmit Project content, a user who disables
every Rework Function has enforced *"my music never leaves this machine"* — as a structural
guarantee, not a promise.

**Directive and Rework Functions are visibly labelled in the settings tab.** Owner, round 7: "Yes
label directive and rework functions."

Without the labels the property would exist but be invisible; with them, a user can deliberately
achieve a local-only configuration and see that they have.

---

## 9. What must never happen

1. **The Assistant must never touch the Studio's settings.**
2. **The Assistant must never perform a broad mutation.** Every Function does one thing and touches
   nothing else.
3. **The Assistant must never be offered a Function the user has switched off** — the Function file
   itself is rebuilt from the toggles.
4. **The product must never present AI-generated content as licenseable.** The warning is mandatory
   and provenance is tracked per Sample and per Pattern.
5. **Bundled content must never carry licensing that prevents commercial release.**
6. **The Studio must never lose a Project.** Full, proper save capability. Owner, round 4: "Yes,
   full proper saving capability. Every project should be saveable."
7. **No Assistant action may be irreversible.** Every Assistant mutation is a delta on the undo
   history (section 3.9). Since the user is never shown which Function ran, undo is their only
   remedy.
8. **The project name must never become expensive to change.**

---

## 10. Open questions

All questions of intent are closed. What remains is **sourcing selection**, which the owner has
delegated to research rather than answering from the chair — a legitimate close, not a gap.

| # | Question                                                             | Section | Disposition |
| - | -------------------------------------------------------------------- | ------- | ----------- |
| 1 | Which open-source implementation for each of the six stock Effects?   | 3.4     | Research Issue |
| 2 | Which CC0 Sample libraries are bundled (drums, guitars)?              | 6.5     | Research Issue |
| 3 | Which models go on the handpicked music-generation list?              | 6.4     | Research Issue |
| 4 | Which LLM provider(s) are supported?                                  | 7       | Research Issue |

Shortlists and a suggested pick for each are in Appendix B. **None of the four blocks the MVP's
shape** — each is a substitution inside a decided structure, not a change to it.

Everything else raised across seven rounds is decided and recorded above.

---

## Appendix A — Research input (not decisions)

Gathered at the owner's request after round 1. **Nothing here is a decision.**

### A.1 Plugin formats

- **VST3 SDK relicensed to MIT** in October 2025 (VST 3.8), replacing GPLv3-or-proprietary dual
  licensing. **ASIO SDK moved to GPLv3.**
- **CLAP** (Bitwig / u-he, 2022) — MIT from the start, community-governed, supported by ~15 DAWs
  including FL Studio, Reaper, Bitwig. Not Ableton, Logic, Cubase or Pro Tools.
- With GPLv3 chosen (3.2), both SDKs are licence-compatible with this project.

### A.2 CC0 Sample libraries built for redistribution

- **Versilian Community Sample Library (VCSL)** — CC0, general-purpose, built to be bundled.
- **Stargate DAW sample pack** — public domain, created because most packs forbid redistribution.
- **Meadowlark factory library** — CC0, factory content of an open-source DAW.

All three satisfy the section 6.1 licenseability rule.

### A.3 AI music generation — output rights

- Suno and Udio paid plans grant a **commercial-use licence to output, not copyright ownership**.
  Suno's wording changed after its November 2025 Warner Music Group settlement.
- Udio has the broadest label agreements but **downloads were paused** pending its UMG platform.
- **Stable Audio** trains on a licensed dataset; **ElevenLabs Music** licensed before launch.
- Training-data litigation still live as of April 2026.

This is why the section 6.2 warning is the right call.

### A.4 Name and trade dress

Image-Line's **FruityLoops** was renamed FL Studio in 2003 after **Kellogg's** challenged the US
trademark. *Google v. Oracle* (2021) held that reimplementing an interface so users can "put their
accrued talents to work in a new and transformative program" is fair use — supporting workflow
cloning, saying nothing about name or visual identity.

> Not legal advice. This is the brief to take to a lawyer, not a substitute for one.

---

## Appendix B — Sourcing research (shortlists, not decisions)

Run at the owner's request to close open questions 1–4. **Every item is a candidate awaiting the
owner's pick.** Each was checked against GPLv3 (section 3.2) and the licenseability rule
(section 6.1).

### B.1 Stock Effects — open question 1

| Candidate | Licence | Covers | Notes |
|---|---|---|---|
| **Airwindows** | **MIT** | All six categories; 300+ processors | One permissive licence over the whole set. MIT is GPLv3-compatible. Maintained by Chris Johnson. `airwin2rack` consolidates them into a single library. |
| **Calf Studio Gear** | LGPL/GPL | EQ, compressor, limiter, delay, reverb, distortion | Professional LV2 suite. Quality reports are mixed — some users report distortion artefacts. |
| **LSP Plugins** | LGPL | Broad set | Widely used; some users report the limiter behaves unpredictably. |
| **x42** | GPL | Limiter especially | The x42 digital peak limiter is widely regarded as transparent. |
| **Chowdhury DSP** | BSD 3-clause | Delay (ChowMatrix), others | High quality, permissive. |

**Suggested pick: Airwindows as the base for all six**, because a single MIT licence covering every
category removes per-Effect licence review, and MIT flows into GPLv3 without friction. Substitute
x42's limiter if the Airwindows limiter disappoints in listening tests.

**Caveat worth knowing now:** building with JUCE plus the VST3 SDK can pull GPL3 dependencies into
the result even when your own source is MIT. Harmless here — GPLv3 is already the project licence
(3.2) — but it would have been a trap under a permissive licence.

### B.2 Stock Samples — open question 2

The owner asked for drums and guitars specifically.

| Candidate | Licence | Best for |
|---|---|---|
| **Stargate DAW sample pack** | Public domain | Drums. Built explicitly for redistribution with a DAW. |
| **Meadowlark factory library** | CC0 | Drums and general factory content, from an open-source DAW. |
| **VCSL** (Versilian Community Sample Library) | CC0 | Acoustic instruments, including guitars. |

**Suggested pick: Stargate or Meadowlark for drums, VCSL for guitars.** All three satisfy 6.1
outright — no negotiation, no attribution burden, no royalty.

### B.3 Music generation — open question 3

| Candidate | Rights story | Fit |
|---|---|---|
| **Stable Audio 3** | Fully licensed, fully documented training data. Weights are **downloadable**; can be **fine-tuned with LoRA and run offline**. Commercial use free under Stability's Community Licence below $1M revenue. | **Strongest fit.** See below. |
| **ElevenLabs Music** | Output commercially licensed; an additional licence is required for advertising, film, TV, games and enterprise distribution. Cleanest API integration. | Good, but the extra-licence carve-out weakens the section 6.1 promise. |
| **Suno / Udio** | Licence granted, not ownership. Udio downloads paused pending its UMG platform. Training-data litigation live. | Weakest rights story. |

**Stable Audio 3 is a structural fit with three decisions already made.** Because the weights can be
downloaded and run offline, it would mean: no hosted compute (NON-scope 2 satisfied by
construction), no API key required, nothing leaving the machine (section 8.4), and the cleanest
available licensing (section 6.1). It is the only candidate that aligns with *native, local* rather
than merely tolerating it.

> **Superseded in part by round 6.** Stable Audio 3 is **not bundled and not downloaded by
> default** — see section 6.4. It is one **user-importable option** on a handpicked list, alongside
> remote models the user supplies a key for. The structural advantages above still apply, but only
> to users who choose that path.

**Two limits that shape the design:** Stable Audio produces **audio only — no MIDI**, and no vocals.

**Consequence worth noting.** This splits AI generation cleanly along a line the architecture
already has:

- **AI-generated Samples** → an audio-generation model such as Stable Audio.
- **AI-generated Patterns** → the **LLM itself**, emitting note data through a Function. No audio
  model is involved, because a Pattern is structured data, not sound.

That split is not yet an owner decision. It is recorded because sections 6.2 and 6.3 permit both,
and they turn out to need entirely different machinery.

### B.4 LLM providers — open question 4

The relevant finding is a capability match rather than a vendor comparison.

**The Claude API supports strict tool use** — setting `strict: true` on a tool definition, with
`additionalProperties: false` and `required` on the schema, **guarantees** the returned arguments
validate exactly against the schema. Current models: Claude Opus 5, Sonnet 5, Haiku 4.5.

This is a direct mechanical match for section 3.8's requirement that Functions be *hard bound* —
"a mutation call that does only that, without otherwise touching the datastructure or pattern."
Schema-level guarantees turn that requirement from a convention the Assistant is asked to respect
into a constraint the API enforces.

**OPEN — Idse.** Whether to support one provider or several. Supporting several means the weakest
provider's tool-calling guarantees set the floor for what the Studio can promise about Function
behaviour.

---

## Change log

- **2026-08-22 — round 1.** Initial capture. Purpose and AI philosophy recorded. Scope captured as
  a raw list. Sixteen open questions raised.
- **2026-08-22 — research pass.** Appendix A added at owner's request. No decisions taken.
- **2026-08-22 — round 2.** Platform decided: native, local. Open-source, no hosted compute,
  bring-your-own-key. Success criteria accepted as functional completeness. Licenseability rule
  added. NON-scope populated. Name decided as disposable. ASIO/GPLv3 interaction raised.
- **2026-08-22 — round 3.** Licence decided: GPLv3, closing the ASIO question. All three desktop
  platforms in scope. Distribution GitHub-first. Stock Effect set enumerated. Assistant authority
  confirmed as complete-reach/narrow-instruments. Function switchboard specified. AI provenance
  tracked per Sample. Failure criteria closed as a deliberate "none".
- **2026-08-22 — round 4.** **Vocabulary fixed and made binding** (section 5); *choreography*,
  *sound* and the overloaded *plugin* retired; the Effect/Plugin split resolves the MVP vs post-MVP
  collision. Target users widened to include competent producers. CLA adopted. Provenance extended
  to Patterns. Save capability added to "must never happen". Data sent to the LLM scoped to prompt
  plus Function instruction; tool-call log deliberately hidden. Open questions cut from ten to six.
- **2026-08-22 — round 5.** Two Function classes defined — Directive (nothing leaves) and Rework
  (Project content sent) — making the transmitting boundary a property of the Function, not of the
  request. Linear delta history with undo/redo adopted, Word-style discard-on-branch; Assistant
  actions made non-irreversible in section 9, which the hidden tool-call log had left unguarded.
  Noted as emergent: the Function switchboard doubles as a structural privacy control. Open
  questions cut from six to five, four of which are sourcing research.
- **2026-08-22 — sourcing research.** Appendix B added: shortlists for the six stock Effects,
  the stock Samples, the music-generation service and the LLM provider, each checked against GPLv3
  and the licenseability rule. Two structural findings recorded — Stable Audio 3's downloadable
  offline weights align with native/local/no-hosted-compute, and AI Pattern generation belongs to
  the LLM rather than an audio model. No open question closed; all four await the owner's pick.
- **2026-08-22 — round 6.** Music generation made **opt-in and unconfigured by default** (6.4): no
  model ships or auto-downloads, the functionality is built but gated, and the user enables it by
  choosing from a handpicked list — importing local weights, or pasting their own key for a remote
  model. Added as NON-scope 7. Noted that this makes the section 6.1 licenseability promise
  unconditional for a default install. Appendix B.3 marked partly superseded. New open question
  raised: whether the Studio is fully usable with no AI configured at all.
- **2026-08-22 — round 7. Document AGREED.** The Studio confirmed as a **fully functional DAW with
  no AI configured** (1.1a), with a first-open key prompt stating plainly that a key is required
  for the Assistant; declining is a first-class path. This resolves "built in rather than bolted
  on" — deeply integrated when present, wholly absent when not — and means the editing surfaces
  must stand on their own merits. Directive/Rework labelling adopted (8.4), making a local-only
  configuration something a user can achieve deliberately and verify. The four remaining sourcing
  questions delegated to research Issues. Owner confirmed the document and authorised Issue
  creation.
