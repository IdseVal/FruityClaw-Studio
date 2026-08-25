// The Processor adapter over x42's peaklim: the stock Limiter (ADR-001).
// peaklim works in its own units — gain and threshold in dB, release in
// seconds — which the core schema carries unchanged, so no scaling lives
// here. It is a look-ahead limiter and reports its latency; the engine does
// not compensate for it in the MVP (about 1.2 ms).
#pragma once

#include <array>
#include <vector>

#include "engine/processor.h"
#include "peaklim/peaklim.h"

namespace effects {

class PeaklimLimiter final : public engine::Processor {
public:
    PeaklimLimiter();

    void prepare(double sampleRate, int maxBlockFrames) override;
    void reset() override;
    void process(engine::AudioBuffer& buffer, const engine::EventList& events) override;

    std::span<const engine::ParameterDescriptor> parameters() const override;
    float getParameter(engine::ParamId id) const override;
    void setParameter(engine::ParamId id, float value) override;

    std::vector<std::byte> saveState() const override;
    bool loadState(std::span<const std::byte> state) override;

private:
    void apply(engine::ParamId id);

    std::vector<engine::ParameterDescriptor> descriptors_;
    std::array<float, 4> values_{};
    DPLLV2::Peaklim limiter_;
    float sample_rate_ = 48000.0f;
};

}  // namespace effects
