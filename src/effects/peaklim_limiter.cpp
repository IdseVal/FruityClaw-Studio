#include "effects/peaklim_limiter.h"

#include <algorithm>
#include <cstring>

#include "core/effect_schema.h"

namespace effects {
namespace {

enum : engine::ParamId { kInputGain = 0, kThreshold = 1, kRelease = 2, kTruePeak = 3 };

}  // namespace

PeaklimLimiter::PeaklimLimiter() {
    engine::ParamId id = 0;
    for (const core::EffectParameter& p : core::effect_parameters(core::EffectType::Limiter)) {
        descriptors_.push_back({id, std::string(p.name), p.min_value, p.max_value,
                                p.default_value, std::string(p.unit)});
        values_[id++] = p.default_value;
    }
}

void PeaklimLimiter::prepare(double sampleRate, int) {
    sample_rate_ = static_cast<float>(sampleRate);
    reset();
}

// init() allocates the look-ahead delay lines, so it is a message-thread
// call; it also zeroes the coefficients the setters derive from, hence the
// re-apply.
void PeaklimLimiter::reset() {
    limiter_.init(sample_rate_, 2);
    for (engine::ParamId id = 0; id < values_.size(); ++id) apply(id);
}

void PeaklimLimiter::process(engine::AudioBuffer& buffer, const engine::EventList&) {
    if (buffer.channelCount < 2) return;
    // process() reads a whole chunk into its delay line before it writes
    // any output, so in place is safe.
    float* io[2] = {buffer.channels[0], buffer.channels[1]};
    limiter_.process(buffer.frames, io, io);
}

std::span<const engine::ParameterDescriptor> PeaklimLimiter::parameters() const {
    return descriptors_;
}

float PeaklimLimiter::getParameter(engine::ParamId id) const {
    return id < values_.size() ? values_[id] : 0.0f;
}

void PeaklimLimiter::setParameter(engine::ParamId id, float value) {
    if (id >= values_.size()) return;
    values_[id] = std::clamp(value, descriptors_[id].minValue, descriptors_[id].maxValue);
    apply(id);
}

void PeaklimLimiter::apply(engine::ParamId id) {
    switch (id) {
        case kInputGain: limiter_.set_inpgain(values_[id]); break;
        case kThreshold: limiter_.set_threshold(values_[id]); break;
        case kRelease: limiter_.set_release(values_[id]); break;
        case kTruePeak: limiter_.set_truepeak(values_[id] > 0.5f); break;
        default: break;
    }
}

std::vector<std::byte> PeaklimLimiter::saveState() const {
    std::vector<std::byte> state(values_.size() * sizeof(float));
    std::memcpy(state.data(), values_.data(), state.size());
    return state;
}

bool PeaklimLimiter::loadState(std::span<const std::byte> state) {
    if (state.size() != values_.size() * sizeof(float)) return false;
    std::array<float, 4> loaded;
    std::memcpy(loaded.data(), state.data(), state.size());
    for (engine::ParamId id = 0; id < loaded.size(); ++id) setParameter(id, loaded[id]);
    return true;
}

}  // namespace effects
