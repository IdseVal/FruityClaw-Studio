# Spec — Repository layout and frozen seams

- **Status:** Proposed — becomes **Frozen** on merge to `dev`
- **Derives from:** [`ADR-008`](../adrs/ADR-008-technology-stack-and-audio-engine.md), core document sections 3.1, 3.6, 3.7, 3.8, 3.9, 5, 8.1, 9
- **Issue:** #8
- **Vocabulary:** domain terms are core document section 5 and are binding. Architecture terms —
  *module, interface, seam, adapter, depth* — are used in the sense of the `codebase-design` skill
  and are also binding: not "component", not "service", not "boundary".

This is the document a feature worktree reads before it writes code. It says where things go, what
may depend on what, and which four interfaces may not be widened to suit a caller.

It deliberately does **not** design the Project data model (#5), the Function surface (#6) or the
delta history (#7). Those are separate architect issues. What is frozen here is the shape they must
fit into, so that three worktrees can open against it at once.

---

## 1. Repository layout

```
/
├─ CMakeLists.txt              # FCS_PRODUCT_NAME / FCS_APP_ID split lives here
├─ CMakePresets.json           # the same presets CI uses
├─ cmake/
├─ docs/                       # CORE_DOCUMENT.md, adrs/, specs/, research/
├─ src/
│  ├─ core/          # the domain of section 5. No Qt. No audio library. No I/O.
│  ├─ engine/        # realtime graph, transport, the Processor interface, mixing
│  ├─ audioio/       # the AudioDevice seam + its PortAudio adapter
│  ├─ effects/       # the six stock Effects of ADR-001, as Processor adapters
│  ├─ plugins/       # RESERVED, post-MVP. VST3/CLAP host adapters. Created empty, with a README.
│  ├─ persistence/   # save/load, delta storage           (issues #7, #13)
│  ├─ assistant/     # Function registry, Function file, LLM transport (issues #6, #16, #17)
│  ├─ ui/            # Qt. The ONLY directory that links Qt.
│  └─ app/           # main(), the composition root: the only place adapters are constructed
├─ third_party/      # vendored: airwindows/, peaklim/, portaudio/, clap/ ...
├─ tests/
├─ packaging/        # windows/  macos/  linux/
└─ .github/workflows/
```

### 1.1 Dependency rules — enforced by CMake, not by good intentions

Each directory under `src/` is one CMake target. The allowed link edges are:

```
        app  ──────────────┐
         │                 │
    ┌────┼─────┬───────┬───┴────┐
    ▼    ▼     ▼       ▼        ▼
   ui  engine assistant persistence audioio
    │    │     │        │           │
    └────┴─────┴────────┴───────────┘
                  │
                  ▼
                core          effects ──▶ engine
                                          (Processor only)
```

Stated as rules, because the diagram is the summary and the rules are the contract:

1. **`core` depends on nothing else in the project.** It is the section 5 vocabulary as data plus
   the operations on it, and it links neither Qt nor PortAudio nor libsndfile.
2. **Only `ui` may link Qt.** If a Qt type appears in a header outside `src/ui/`, the layout has
   already failed.
3. **Only `audioio` may include a PortAudio header.** Only `effects` may include Airwindows or
   `peaklim` sources. Only `plugins` may include a VST3 or CLAP header.
4. **`app` is the composition root.** It is the single place that constructs concrete adapters and
   hands them to the modules that need them. Everywhere else accepts its dependencies; nothing else
   creates them.
5. **No module reaches around a seam.** If a caller needs something the interface does not offer,
   the answer is to deepen the module behind the seam, never to widen the interface for that one
   caller.

Rules 1 and 2 together are what make the Studio testable: `core`, `engine` and `persistence` run in
CI with no display and no audio device.

---

## 2. Seam A — `AudioDevice` (`src/audioio/`)

**What it hides:** every platform host API, every device enumeration quirk, and the identity of the
backend library. Nothing above this seam contains the strings "PortAudio", "ASIO", "WASAPI",
"CoreAudio", "ALSA" or "JACK" outside of settings labels shown to the user.

```cpp
struct DeviceInfo {
    DeviceId    id;             // opaque, stable across a session
    std::string name;           // shown to the user
    std::string backendName;    // "ASIO", "WASAPI (exclusive)", "CoreAudio", "JACK" ...
    int         maxInputChannels;
    int         maxOutputChannels;
    std::vector<double> supportedSampleRates;
    std::vector<int>    supportedBufferSizes;
};

struct StreamConfig {
    DeviceId inputDevice;       // may be none — output-only is valid
    DeviceId outputDevice;
    double   sampleRate;
    int      bufferFrames;
};

// Called on the realtime audio thread. Non-interleaved, one pointer per channel.
// See the realtime rules in section 4 — they apply to everything this callback reaches.
using AudioCallback = void (*)(const float* const* input,  int inputChannels,
                               float* const*       output, int outputChannels,
                               int frames, const StreamTime& time, void* userData);

class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual std::vector<DeviceInfo> enumerate() = 0;
    virtual Result open(const StreamConfig&, AudioCallback, void* userData) = 0;
    virtual Result start() = 0;
    virtual Result stop() = 0;
    virtual void   close() = 0;
    virtual StreamLatency measuredLatency() const = 0;
};
```

**Adapters:** `PortAudioDevice` today. `RtAudioDevice` is pre-qualified in ADR-008 as the second
adapter, and a native per-platform adapter remains possible. Two viable adapters is what makes this
a real seam rather than a hypothetical one.

**Why the interface stops here.** Six methods and two structs buy the whole cross-platform device
problem. Adding a backend-specific control — an ASIO control-panel call, a JACK port-registration
hook — is the exact move that turns a deep module into the union of its callers' conveniences. Such
a control goes behind the interface, expressed in terms every backend can answer, or it does not go
in.

---

## 3. Seam B — `Processor` (`src/engine/`)

**The single interface for anything that produces or transforms audio.** Stock Effects,
Instruments, and — post-MVP — hosted third-party Plugins are all adapters over it. This is the seam
that makes ADR-008's "Plugin hosting without a rewrite" claim checkable.

```cpp
struct ParameterDescriptor {
    ParamId     stableId;       // never reused, never renumbered — it is persisted
    std::string name;
    float       minValue, maxValue, defaultValue;
    std::string unit;
};

class Processor {
public:
    virtual ~Processor() = default;

    // Message thread. May allocate. Called before any process() call.
    virtual void prepare(double sampleRate, int maxBlockFrames) = 0;
    virtual void reset() = 0;

    // Realtime thread. Must obey the rules in section 4.
    virtual void process(AudioBuffer& buffer, const EventList& events) = 0;

    virtual std::span<const ParameterDescriptor> parameters() const = 0;
    virtual float getParameter(ParamId) const = 0;
    virtual void  setParameter(ParamId, float value) = 0;   // realtime-safe

    virtual std::vector<std::byte> saveState() const = 0;    // message thread
    virtual bool loadState(std::span<const std::byte>) = 0;  // message thread
};
```

### 3.1 The four frozen rules

These are frozen now, before there is code to argue with, because each one is cheap today and
expensive after three worktrees depend on it.

1. **Parameters are declared, never exposed as fields.** No `Processor` subclass offers typed
   accessors for its own knobs as part of this interface. Automation, the Assistant's Functions, the
   generic parameter UI and Project persistence all go through `ParamId`, so all four get every new
   Effect for free.
2. **State is opaque bytes.** The host never interprets a `Processor`'s state. A hosted Plugin's
   state is opaque by definition; letting the stock Effects be special guarantees the seam bends the
   day the Plugins arrive.
3. **No host-internal pointers, no Qt types, no allocation cross this interface.** This is the rule
   that pays for itself twice: it keeps `process()` realtime-safe, and it means an adapter can
   satisfy the interface across a **process boundary** — so out-of-process Plugin hosting, which
   stops a crashing third-party Plugin from taking the user's Project with it, stays a later
   decision instead of becoming a rewrite.
4. **`ParamId` values are permanent.** They are written into saved Projects. Renumbering them
   silently corrupts every Project that used the Effect.

**Adapters (MVP):** `AirwindowsEffect` wrapping the five ADR-001 picks, `PeaklimLimiter` wrapping
x42's `peaklim`. ADR-001 already anticipated this work: "each pick needs its processing block lifted
into the project's own Effect interface."

**Adapters (post-MVP):** `Vst3PluginHost`, `ClapPluginHost` — new files under `src/plugins/`, no
change above the seam.

---

## 4. Seam C — the command and event seam (`src/engine/`)

**The rule this exists to enforce: the audio callback thread never blocks, and no other thread ever
touches realtime state directly.**

- **Message thread to audio thread:** a single-producer, single-consumer lock-free command queue.
  Commands are trivially copyable value types. The audio thread drains the queue at the top of each
  block.
- **Audio thread to UI:** a lock-free event queue (levels, playhead position, xrun counts), polled
  by the UI on a timer. The UI never reads engine state directly.
- **Structural changes are built off-thread.** Anything requiring allocation — adding a Track,
  instantiating a Processor, loading a Sample — is constructed on the message thread and handed
  across as a ready-made object. The audio thread only ever swaps a pointer, and the old object is
  destroyed back on the message thread.

### 4.1 Realtime rules — non-negotiable on the audio callback thread

No allocation. No locks. No file or network I/O. No logging. No exceptions. No unbounded loops.
Anything reached from `AudioCallback` or `Processor::process()` inherits these rules, third-party
DSP included.

### 4.2 Why this seam is load-bearing for the core document

Because the Assistant reaches the engine through **this same seam** and no other, three core
document guarantees stop being conventions and become structural:

| Core document requirement | What the seam does |
|---|---|
| 3.8 / 9.2 — the Assistant may never perform a broad mutation | There is no broad command. A Function can only send commands that exist, and each command does one thing. |
| 3.9 — Assistant actions are deltas like any other | Commands are how deltas are applied. An Assistant action is not a second path; it is the same path. |
| 9.7 — no Assistant action may be irreversible | Undo operates on the command stream, so it covers Assistant actions for free rather than by remembering to. |

An Assistant that could call into `src/engine/` directly would break all three at once, quietly.
Hence: **`src/assistant/` does not link `src/engine/`.** It issues commands.

---

## 5. Seam D — `AssistantTransport` (`src/assistant/`) — reserved

Named and reserved for issue #6. Frozen here, and only this:

1. `src/assistant/` **does not link Qt** and **does not link `src/engine/`**. It reaches the engine
   only through the command seam of section 4.
2. Every provider the core document's open question 4 eventually selects is an **adapter** behind one
   interface. No provider-specific type appears outside `src/assistant/`.
3. The Function file (core document 3.10) is **generated** from the registry plus the user's
   toggles. Nothing may hold a hand-maintained second copy of the Function list — core document 9.3
   requires that a disabled Function be *absent*, not refused, and one generator with one source is
   the only way that stays true.

Everything else about Functions — their names, arguments, the Directive/Rework split (core document
8.4) — belongs to issue #6.

---

## 6. Where the open issues attach

| Issue | Fits at | This spec fixes only |
|---|---|---|
| #5 Project data model | `src/core/` | No Qt, no audio library, no I/O in `core` |
| #6 Function surface | Seam D | No Qt; no direct engine link; the Function file is generated |
| #7 Delta history | `src/persistence/` + Seam C | Deltas applied on the message thread, never on the audio thread |
| #9 Step sequencer, #10 Piano roll, #11 Arrangement, #12 Sample sidebar | `src/ui/` | Read through the event queue; write through commands |
| #13 Project save and load | `src/persistence/` | File magic derives from `FCS_APP_ID`, never a product name |
| #14 Audio recording | Seam A + `src/engine/` | Input arrives through `AudioDevice`; writing to disk happens off the audio thread |
| #15 Stock Effect chain | Seam B, `src/effects/` | The ADR-001 picks are `Processor` adapters |
| #16 Assistant panel, #17 Function switchboard | `src/ui/` + Seam D | The switchboard rebuilds the Function file; it does not filter at call time |
| #18 First-open key prompt, #19 Provenance marks, #20 Generation settings | `src/ui/` + `src/core/` | Provenance is a field on the data model (core document 6.3), not a UI concern |

---

## 7. Changing anything in this document

Per the architect's standing rule: a contract that changes while worktrees depend on it is not a
contract. If one of these seams turns out to be wrong:

1. **Stop** the work that depends on it.
2. Say so in the issue, naming what is wrong and which worktrees are affected.
3. Change it deliberately, in a new ADR that supersedes the relevant part of ADR-008.

Never widen an interface to unblock a single caller. That is how an interface becomes the union of
every caller's convenience, and it is the failure mode this document exists to prevent.
