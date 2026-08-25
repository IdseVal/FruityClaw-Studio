#include "engine/render_model.h"

#include <algorithm>
#include <unordered_map>

#include "core/effect_schema.h"

namespace engine {

std::shared_ptr<Processor> ProcessorPool::acquire(const core::Effect& effect,
                                                  double sample_rate, int max_block_frames) {
    if (!factory_) return nullptr;
    Entry& entry = entries_[effect.id];
    if (entry.processor && entry.sample_rate != sample_rate) {
        // Re-preparing a Processor the audio thread may be inside is not
        // safe; a fresh one is, and the old one is retired like any other.
        replaced_.push_back(std::move(entry.processor));
    }
    if (!entry.processor) {
        entry.processor = factory_(effect.type);
        if (!entry.processor) return nullptr;
        entry.processor->prepare(sample_rate, max_block_frames);
        entry.sample_rate = sample_rate;
    }
    entry.seen = true;
    return entry.processor;
}

std::vector<std::shared_ptr<Processor>> ProcessorPool::sweep() {
    std::vector<std::shared_ptr<Processor>> unused = std::move(replaced_);
    replaced_.clear();
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.seen) {
            it->second.seen = false;
            ++it;
        } else {
            unused.push_back(std::move(it->second.processor));
            it = entries_.erase(it);
        }
    }
    return unused;
}

namespace {

// The parameter values the Effect's params map holds, in ParamId order, with
// the schema default standing in for anything the map lacks.
std::vector<BakedEffect> bake_chain(const std::vector<core::Effect>& chain, ProcessorPool* pool,
                                    double sample_rate, int max_block_frames) {
    std::vector<BakedEffect> baked;
    if (!pool) return baked;
    for (const core::Effect& effect : chain) {
        std::shared_ptr<Processor> processor = pool->acquire(effect, sample_rate, max_block_frames);
        if (!processor) continue;
        BakedEffect b;
        b.processor = processor.get();
        b.enabled = effect.enabled;
        ParamId id = 0;
        for (const core::EffectParameter& parameter : core::effect_parameters(effect.type)) {
            auto it = effect.params.find(std::string(parameter.name));
            b.values.emplace_back(id++, it == effect.params.end() ? parameter.default_value
                                                                  : it->second);
        }
        baked.push_back(std::move(b));
    }
    return baked;
}

}  // namespace

std::shared_ptr<const RenderModel> bake(const core::Project& project, double sample_rate,
                                        ProcessorPool* pool, int max_block_frames) {
    auto model = std::make_shared<RenderModel>();
    model->sample_rate = sample_rate;
    model->samples_per_tick =
        sample_rate * 60.0 / (project.tempo * static_cast<double>(core::kPpq));
    model->master_chain = bake_chain(project.master_chain, pool, sample_rate, max_block_frames);

    if (project.arrangements.items.empty()) return model;
    const core::Arrangement& arrangement = project.arrangements.items.front();

    // Instruments referenced by played Parts, deduplicated by Id.
    std::unordered_map<core::Id, std::uint32_t> instrument_index;
    auto baked_instrument = [&](core::Id id) -> std::int64_t {
        if (auto it = instrument_index.find(id); it != instrument_index.end())
            return it->second;
        const core::Instrument* instrument = project.instruments.find(id);
        if (!instrument) return -1;
        const core::Sample* sample = project.samples.find(instrument->params.sample);
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
            (project.tempo * static_cast<double>(core::kPpq));
        baked.start_frame = static_cast<std::int64_t>(
            static_cast<double>(instrument->params.start_offset) * sample_frames_per_tick);
        baked.end_frame = static_cast<std::int64_t>(
            static_cast<double>(instrument->params.end_offset) * sample_frames_per_tick);
        baked.chain = bake_chain(instrument->chain, pool, sample_rate, max_block_frames);

        std::uint32_t index = static_cast<std::uint32_t>(model->instruments.size());
        model->instruments.push_back(std::move(baked));
        instrument_index.emplace(id, index);
        return index;
    };

    for (const core::Track& track : arrangement.tracks) {
        if (track.muted) continue;
        for (const core::Placement& placement : track.placements) {
            if (placement.muted || placement.length <= 0) continue;
            const core::Pattern* pattern = project.patterns.find(placement.pattern);
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
