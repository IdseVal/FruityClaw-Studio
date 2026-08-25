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
//
// Effect chains are baked as references to live Processors plus the
// parameter values to drive them with. The Processors themselves outlive any
// one model: a reverb tail or a delay line must survive the republish that
// follows every Delta, so they are owned by a ProcessorPool keyed by Effect
// Id and only referenced from here.
#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/entities.h"
#include "engine/processor.h"

namespace engine {

struct BakedEffect {
    Processor* processor = nullptr;  // owned by the ProcessorPool
    bool enabled = true;
    std::vector<std::pair<ParamId, float>> values;  // applied when the model is taken up
};

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
    std::vector<BakedEffect> chain;
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
    std::vector<BakedEffect> master_chain;
};

// Builds the Processor for one stock Effect type. Supplied by the composition
// root (src/effects/ links engine, never the reverse — architecture-seams
// section 1.1).
using ProcessorFactory = std::function<std::unique_ptr<Processor>(core::EffectType)>;

// The live Processors of the Project's Effects, keyed by Effect Id and kept
// across republishes. Edit thread only.
class ProcessorPool {
public:
    void set_factory(ProcessorFactory factory) { factory_ = std::move(factory); }
    bool has_factory() const { return static_cast<bool>(factory_); }

    // The Processor for `effect`, created and prepared on first sight or when
    // the sample rate changed. Null without a factory.
    std::shared_ptr<Processor> acquire(const core::Effect& effect, double sample_rate,
                                       int max_block_frames);

    // Hands back every Processor not acquired since the previous sweep — the
    // Effects that left the Project — for the caller to retire once the audio
    // thread has provably moved past them.
    std::vector<std::shared_ptr<Processor>> sweep();

private:
    struct Entry {
        std::shared_ptr<Processor> processor;
        double sample_rate = 0.0;
        bool seen = false;
    };
    std::unordered_map<core::Id, Entry> entries_;
    std::vector<std::shared_ptr<Processor>> replaced_;
    ProcessorFactory factory_;
};

// Bakes the first Arrangement of the Project. Muted Tracks, muted Placements
// and muted Parts are silent by omission. A Placement longer than its Pattern
// loops it to fill; shorter trims it: events starting at or past the
// Placement's end are dropped and a Sustain gate never crosses it. Without a
// pool (or a pool without a factory) Effect chains bake empty.
std::shared_ptr<const RenderModel> bake(const core::Project& project, double sample_rate,
                                        ProcessorPool* pool = nullptr,
                                        int max_block_frames = 0);

}  // namespace engine
