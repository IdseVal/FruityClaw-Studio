#include "engine/render_model.h"

#include <algorithm>
#include <unordered_map>

namespace engine {

std::shared_ptr<const RenderModel> bake(const core::MusicalContent& content, double sample_rate) {
    auto model = std::make_shared<RenderModel>();
    model->sample_rate = sample_rate;
    model->samples_per_tick =
        sample_rate * 60.0 / (content.tempo * static_cast<double>(core::kPpq));

    if (content.arrangements.items.empty()) return model;
    const core::Arrangement& arrangement = content.arrangements.items.front();

    // Instruments referenced by played Parts, deduplicated by Id.
    std::unordered_map<core::Id, std::uint32_t> instrument_index;
    auto baked_instrument = [&](core::Id id) -> std::int64_t {
        if (auto it = instrument_index.find(id); it != instrument_index.end())
            return it->second;
        const core::Instrument* instrument = content.instruments.find(id);
        if (!instrument) return -1;
        const core::Sample* sample = content.samples.find(instrument->params.sample);
        if (!sample || !sample->source) return -1;

        BakedInstrument baked;
        baked.audio = sample->source;
        baked.root_pitch = instrument->params.root_pitch;
        baked.mode = instrument->params.mode;
        baked.gain = instrument->params.gain;
        baked.pan = instrument->params.pan;
        // Offsets are musical Ticks in the model; frames depend on the
        // Sample's own rate and the Project tempo.
        double sample_frames_per_tick =
            sample->source->sample_rate * 60.0 /
            (content.tempo * static_cast<double>(core::kPpq));
        baked.start_frame = static_cast<std::int64_t>(
            static_cast<double>(instrument->params.start_offset) * sample_frames_per_tick);
        baked.end_frame = static_cast<std::int64_t>(
            static_cast<double>(instrument->params.end_offset) * sample_frames_per_tick);

        std::uint32_t index = static_cast<std::uint32_t>(model->instruments.size());
        model->instruments.push_back(std::move(baked));
        instrument_index.emplace(id, index);
        return index;
    };

    for (const core::Track& track : arrangement.tracks) {
        if (track.muted) continue;
        for (const core::Placement& placement : track.placements) {
            if (placement.muted || placement.length <= 0) continue;
            const core::Pattern* pattern = content.patterns.find(placement.pattern);
            if (!pattern || pattern->length <= 0) continue;

            for (core::Ticks loop_start = 0; loop_start < placement.length;
                 loop_start += pattern->length) {
                for (const core::Part& part : pattern->parts) {
                    if (part.muted) continue;
                    std::int64_t instrument = baked_instrument(part.instrument);
                    if (instrument < 0) continue;
                    for (const core::Event& event : part.events) {
                        core::Ticks local = loop_start + event.start;
                        if (event.start >= pattern->length) continue;
                        if (local >= placement.length) continue;
                        core::Ticks gate =
                            std::min(event.duration, placement.length - local);

                        Trigger trigger;
                        trigger.sample_pos = static_cast<std::int64_t>(
                            static_cast<double>(placement.start + local) *
                            model->samples_per_tick);
                        trigger.gate_samples = static_cast<std::int64_t>(
                            static_cast<double>(gate) * model->samples_per_tick);
                        trigger.pitch = event.pitch;
                        trigger.velocity_gain =
                            static_cast<float>(event.velocity) / 127.0f;
                        trigger.instrument = static_cast<std::uint32_t>(instrument);
                        model->triggers.push_back(trigger);
                    }
                }
            }
        }
    }

    std::sort(model->triggers.begin(), model->triggers.end(),
              [](const Trigger& a, const Trigger& b) { return a.sample_pos < b.sample_pos; });
    return model;
}

}  // namespace engine
