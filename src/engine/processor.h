// Seam B — Processor: the single interface for anything that produces or
// transforms audio (docs/specs/architecture-seams.md section 3). The six
// stock Effects are adapters over it today; Instruments and, post-MVP,
// hosted VST3/CLAP Plugins are further adapters, and nothing above this seam
// learns a new concept when they arrive.
//
// The member names follow the spec's listing verbatim — it is a frozen
// contract, and the four rules of section 3.1 hang off it: parameters are
// declared, never fields; state is opaque bytes; nothing host-internal, no
// Qt type and no allocation crosses process(); ParamIds are permanent.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine {

using ParamId = std::uint32_t;

struct ParameterDescriptor {
    ParamId stableId;  // never reused, never renumbered — it is persisted
    std::string name;
    float minValue, maxValue, defaultValue;
    std::string unit;
};

// Non-interleaved planar audio, one pointer per channel, processed in place.
struct AudioBuffer {
    float* const* channels;
    int channelCount;
    int frames;
};

// Reserved. The stock Effects take no events; Instruments and hosted Plugins
// will, and the signature is frozen now so they fit behind the same seam.
struct EventList {};

class Processor {
public:
    virtual ~Processor() = default;

    // Message thread. May allocate. Called before any process() call.
    virtual void prepare(double sampleRate, int maxBlockFrames) = 0;
    virtual void reset() = 0;

    // Realtime thread. Must obey the rules in architecture-seams section 4.1.
    virtual void process(AudioBuffer& buffer, const EventList& events) = 0;

    virtual std::span<const ParameterDescriptor> parameters() const = 0;
    virtual float getParameter(ParamId id) const = 0;
    virtual void setParameter(ParamId id, float value) = 0;  // realtime-safe

    virtual std::vector<std::byte> saveState() const = 0;      // message thread
    virtual bool loadState(std::span<const std::byte> state) = 0;  // message thread
};

}  // namespace engine
