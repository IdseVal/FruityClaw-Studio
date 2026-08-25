// The Processor adapter over one vendored Airwindows plugin class.
//
// Airwindows plugins are stereo-in, stereo-out, driven by N controls in
// 0..1 and the sample rate, processed in place. The adapter declares its
// ParameterDescriptors from the core schema (effect_schema.h) so the names
// the user and the Assistant see are the schema's, and ParamId i is
// Airwindows parameter index i for good.
//
// One translation unit per plugin: every Airwindows header defines the same
// global enumerators (kParamA, kNumParameters ...), so two cannot share a TU.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/effect_schema.h"
#include "engine/processor.h"

namespace effects {

template <class Plugin>
class AirwindowsEffect final : public engine::Processor {
public:
    explicit AirwindowsEffect(core::EffectType type) {
        engine::ParamId id = 0;
        for (const core::EffectParameter& p : core::effect_parameters(type)) {
            descriptors_.push_back({id++, std::string(p.name), p.min_value, p.max_value,
                                    p.default_value, std::string(p.unit)});
        }
        plugin_.emplace(nullptr);
    }

    void prepare(double sampleRate, int) override {
        sample_rate_ = static_cast<float>(sampleRate);
        plugin_->setSampleRate(sample_rate_);
    }

    // Airwindows exposes no state clear; a fresh instance with the same
    // controls is the reset.
    void reset() override {
        std::vector<float> values;
        for (const auto& d : descriptors_)
            values.push_back(plugin_->getParameter(static_cast<std::int32_t>(d.stableId)));
        plugin_.emplace(nullptr);
        plugin_->setSampleRate(sample_rate_);
        for (std::size_t i = 0; i < values.size(); ++i)
            plugin_->setParameter(static_cast<std::int32_t>(i), values[i]);
    }

    void process(engine::AudioBuffer& buffer, const engine::EventList&) override {
        if (buffer.channelCount < 2) return;
        // processReplacing reads each sample before it writes it, so the
        // buffer is safely both input and output.
        float* io[2] = {buffer.channels[0], buffer.channels[1]};
        plugin_->processReplacing(io, io, buffer.frames);
    }

    std::span<const engine::ParameterDescriptor> parameters() const override {
        return descriptors_;
    }

    float getParameter(engine::ParamId id) const override {
        if (id >= descriptors_.size()) return 0.0f;
        return const_cast<Plugin&>(*plugin_).getParameter(static_cast<std::int32_t>(id));
    }

    void setParameter(engine::ParamId id, float value) override {
        if (id >= descriptors_.size()) return;
        plugin_->setParameter(static_cast<std::int32_t>(id), std::clamp(value, 0.0f, 1.0f));
    }

    std::vector<std::byte> saveState() const override {
        std::vector<std::byte> state(descriptors_.size() * sizeof(float));
        for (std::size_t i = 0; i < descriptors_.size(); ++i) {
            float v = getParameter(static_cast<engine::ParamId>(i));
            std::memcpy(state.data() + i * sizeof(float), &v, sizeof(float));
        }
        return state;
    }

    bool loadState(std::span<const std::byte> state) override {
        if (state.size() != descriptors_.size() * sizeof(float)) return false;
        for (std::size_t i = 0; i < descriptors_.size(); ++i) {
            float v;
            std::memcpy(&v, state.data() + i * sizeof(float), sizeof(float));
            setParameter(static_cast<engine::ParamId>(i), v);
        }
        return true;
    }

private:
    std::vector<engine::ParameterDescriptor> descriptors_;
    std::optional<Plugin> plugin_;
    float sample_rate_ = 44100.0f;
};

// One factory per plugin, each defined in its own translation unit.
std::unique_ptr<engine::Processor> make_eq();
std::unique_ptr<engine::Processor> make_compressor();
std::unique_ptr<engine::Processor> make_reverb();
std::unique_ptr<engine::Processor> make_delay();
std::unique_ptr<engine::Processor> make_distortion();

}  // namespace effects
