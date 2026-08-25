// The entities of the Project data model.
// Contract: docs/specs/project-data-model.md section 3. Field names, ownership
// and reference direction follow that document exactly; deviations are defects.
//
// Entities are plain data. The mutation discipline of section 5 — no public
// mutators, ProjectHistory::apply as the only door — is enforced by module
// convention: everything outside core receives `const Project&` and every write
// path goes through core::ProjectHistory (history.h).
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.h"
#include "core/primitives.h"

namespace core {

// ---------------------------------------------------------------------------
// Provenance (contract section 4)

struct Provenance {
    // Human by default. When generated, `generated_by` names the model or agent
    // and `generated_at` is a unix timestamp in seconds; the section 6.3 boolean
    // is `!is_human()`, derived and never stored separately.
    std::string generated_by;
    std::int64_t generated_at = 0;

    bool is_human() const { return generated_by.empty(); }
    bool operator==(const Provenance&) const = default;

    static Provenance human() { return {}; }
    static Provenance generated(std::string source, std::int64_t at) {
        return Provenance{std::move(source), at};
    }
};

// ---------------------------------------------------------------------------
// Sample

// Decoded audio held in memory. Whether Project files embed or link the bytes
// is the serialisation issue's decision (contract section 7.1); this in-memory
// form presumes neither. Immutable once created, so the audio thread may hold a
// pointer to it without coordination.
struct AudioData {
    double sample_rate = 48000.0;
    int channels = 1;
    std::vector<float> frames;  // interleaved when channels > 1

    std::int64_t frame_count() const {
        return channels == 0 ? 0 : static_cast<std::int64_t>(frames.size()) / channels;
    }
};

// Opaque handle to a Sample's audio (contract section 3.2).
using SampleSource = std::shared_ptr<const AudioData>;

struct Sample {
    Id id;
    std::string name;
    SampleSource source;
    Provenance provenance;

    bool operator==(const Sample&) const = default;
};

// ---------------------------------------------------------------------------
// Instrument

enum class SamplerMode {
    OneShot,  // ignores Event duration — the drum-step case
    Sustain,
};

// The MVP defines exactly one Instrument kind: Sampler (contract section 3.3).
// `Synth` and `Plugin` are named extension points, not defined here.
struct SamplerParams {
    Id sample;                // reference -> Sample
    Pitch root_pitch = 60;    // the Pitch at which the Sample plays at native rate
    Ticks start_offset = 0;   // trim into the Sample
    Ticks end_offset = 0;     // 0 = no trim
    SamplerMode mode = SamplerMode::OneShot;
    Unit gain = 0.8f;
    Unit pan = 0.5f;

    bool operator==(const SamplerParams&) const = default;
};

// An instance of one of the six built-in Effects (contract section 3.10). The
// data model freezes the container, not the contents; parameter schemas belong
// to the Effect specification. Carried here so instrument and master chains
// exist in the model; Effect processing itself is issue #15.
enum class EffectType { Eq, Compressor, Limiter, Reverb, Delay, Distortion };

struct Effect {
    Id id;
    EffectType type = EffectType::Eq;
    bool enabled = true;
    std::map<std::string, float> params;

    bool operator==(const Effect&) const = default;
};

struct Instrument {
    Id id;
    std::string name;
    SamplerParams params;         // kind == Sampler, the only MVP kind
    std::vector<Effect> chain;    // owned, ordered, may be empty

    bool operator==(const Instrument&) const = default;
};

// ---------------------------------------------------------------------------
// Pattern

struct Event {
    Id id;
    Ticks start = 0;      // relative to the start of the Pattern
    Ticks duration = 0;   // > 0; ignored by OneShot Instruments
    Pitch pitch = 60;
    Velocity velocity = 100;

    bool operator==(const Event&) const = default;
};

// The Events of one Instrument within one Pattern (contract section 3.5).
struct Part {
    Id id;
    Id instrument;               // reference -> Instrument
    std::vector<Event> events;   // owned; unordered as data
    bool muted = false;

    bool operator==(const Part&) const = default;
};

// One concept covering drum steps and melody notes; there is no DrumPattern
// and no MelodyPattern (contract section 3.4).
struct Pattern {
    Id id;
    std::string name;
    Ticks length = 4 * kPpq;     // authored, not derived from its Events
    std::vector<Part> parts;     // owned; order is the step-sequencer row order
    Provenance provenance;
    std::optional<Colour> colour;

    bool operator==(const Pattern&) const = default;
};

// ---------------------------------------------------------------------------
// Arrangement

// One appearance of a Pattern on a Track (contract section 3.9). Longer than
// the Pattern's length loops it to fill; shorter trims it.
struct Placement {
    Id id;
    Id pattern;         // reference -> Pattern
    Ticks start = 0;    // position on the Arrangement timeline
    Ticks length = 0;
    bool muted = false;

    bool operator==(const Placement&) const = default;
};

// One lane in the Arrangement (contract section 3.8). No effect chain in the
// MVP: per-Track processing is the Mixer, which is post-MVP.
struct Track {
    Id id;
    std::string name;
    std::vector<Placement> placements;  // owned
    bool muted = false;
    std::optional<Colour> colour;

    bool operator==(const Track&) const = default;
};

// The timeline where Patterns are assembled into the finished piece
// (contract section 3.7). Exactly one per Project in the MVP.
struct Arrangement {
    Id id;
    std::string name;
    std::vector<Track> tracks;  // owned; order is top-to-bottom in the view

    bool operator==(const Arrangement&) const = default;
};

// ---------------------------------------------------------------------------
// Project

// An insertion-ordered map Id -> T. Order is authored (it is the sidebar and
// palette order) so a plain vector carries it; lookup is linear, which is fine
// at Project scale and keeps the container trivially copyable and comparable.
template <typename T>
struct OrderedMap {
    std::vector<T> items;

    const T* find(const Id& id) const {
        for (const auto& item : items)
            if (item.id == id) return &item;
        return nullptr;
    }
    T* find(const Id& id) {
        for (auto& item : items)
            if (item.id == id) return &item;
        return nullptr;
    }
    bool operator==(const OrderedMap&) const = default;
};

struct Project {
    Id id;
    int format_version = 1;  // numeric only, never a branded string
    std::string title;       // the user's title for their work
    double tempo = 120.0;    // BPM, constant for the whole Project in the MVP
    std::pair<int, int> time_signature{4, 4};

    OrderedMap<Sample> samples;
    OrderedMap<Instrument> instruments;
    OrderedMap<Pattern> patterns;
    OrderedMap<Arrangement> arrangements;  // exactly one entry in the MVP
    std::vector<Effect> master_chain;

    bool operator==(const Project&) const = default;
};

}  // namespace core
