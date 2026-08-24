// Seam A — AudioDevice (docs/specs/architecture-seams.md section 2).
//
// Hides every platform host API, every device enumeration quirk, and the
// identity of the backend library. Nothing above this seam names a backend
// outside of settings labels shown to the user.
#pragma once

#include <string>
#include <vector>

namespace audioio {

// Opaque, stable across a session.
struct DeviceId {
    int value = -1;
    bool operator==(const DeviceId&) const = default;
    bool is_none() const { return value < 0; }
};

struct DeviceInfo {
    DeviceId id;
    std::string name;          // shown to the user
    std::string backend_name;  // "WASAPI", "CoreAudio", ... settings label only
    int max_input_channels = 0;
    int max_output_channels = 0;
    std::vector<double> supported_sample_rates;
    // One field beyond the frozen seam spec, so a first open with no saved
    // configuration can pick a device. Every backend can answer it. Flagged
    // in the PR for the seam owner to ratify.
    bool is_default_output = false;
};

struct StreamConfig {
    DeviceId input_device;   // may be none — output-only is valid
    DeviceId output_device;
    double sample_rate = 48000.0;
    int buffer_frames = 512;
};

struct StreamTime {
    double seconds = 0.0;
};

struct StreamLatency {
    double input_seconds = 0.0;
    double output_seconds = 0.0;
};

struct Result {
    bool ok = true;
    std::string error;

    static Result success() { return {}; }
    static Result failure(std::string message) { return {false, std::move(message)}; }
};

// Called on the realtime audio thread. Non-interleaved, one pointer per
// channel. Everything reached from it obeys the realtime rules of
// architecture-seams section 4.1.
using AudioCallback = void (*)(const float* const* input, int input_channels,
                               float* const* output, int output_channels, int frames,
                               const StreamTime& time, void* user_data);

class AudioDevice {
public:
    virtual ~AudioDevice() = default;

    virtual std::vector<DeviceInfo> enumerate() = 0;
    virtual Result open(const StreamConfig& config, AudioCallback callback,
                        void* user_data) = 0;
    virtual Result start() = 0;
    virtual Result stop() = 0;
    virtual void close() = 0;
    virtual StreamLatency measured_latency() const = 0;
};

}  // namespace audioio
