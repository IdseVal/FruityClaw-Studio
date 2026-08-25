// The baked, immutable playback form of a Project.
//
// Built on the edit thread after every Delta and handed to the audio thread
// as a whole (architecture-seams section 4: structural changes are built
// off-thread; the audio thread only ever swaps a pointer). Everything in here
// is precomputed so the audio callback allocates nothing and looks nothing up.
//
// Baking flattens Arrangement -> Track -> Placement -> Pattern -> Part ->
// Event into one sorted trigger list. Drum and melody Patterns take exactly
// the same path — the only behavioural difference is the Instrument's
// SamplerMode, which is a property of the Instrument, not of the Pattern.
#pragma once

#include <memory>
#include <vector>

#include "core/entities.h"

namespace engine {

struct BakedInstrument {
    // Owning reference: keeps the audio alive for the model's lifetime even
    // if the Sample is deleted from the Project meanwhile.
    core::SampleSource audio;
    core::Pitch root_pitch = 60;
    core::SamplerMode mode = core::SamplerMode::OneShot;
    float gain = 0.8f;
    float pan = 0.5f;
    std::int64_t start_frame = 0;  // trim into the Sample, in Sample frames
    std::int64_t end_frame = 0;    // 0 = untrimmed
};

struct Trigger {
    std::int64_t sample_pos = 0;       // absolute output-stream sample
    std::int64_t gate_samples = 0;     // Sustain: samples until note-off
    core::Pitch pitch = 60;
    float velocity_gain = 1.0f;        // velocity / 127
    std::uint32_t instrument = 0;      // index into instruments
};

struct RenderModel {
    double sample_rate = 48000.0;
    double samples_per_tick = 0.0;
    std::vector<BakedInstrument> instruments;
    std::vector<Trigger> triggers;  // sorted by sample_pos
};

// Bakes the first Arrangement of the Project. Muted Tracks, muted Placements
// and muted Parts are silent by omission. A Placement longer than its Pattern
// loops it to fill; shorter trims it: events starting at or past the
// Placement's end are dropped and a Sustain gate never crosses it.
std::shared_ptr<const RenderModel> bake(const core::Project& project, double sample_rate);

}  // namespace engine
