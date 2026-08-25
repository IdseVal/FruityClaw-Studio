#include "app/audio_session.h"

#include <algorithm>
#include <cmath>

namespace app {

void AudioSession::audio_callback(const float* const* input, int input_channels,
                                  float* const* output, int output_channels, int frames,
                                  const audioio::StreamTime&, void* user_data) {
    auto* self = static_cast<AudioSession*>(user_data);
    self->recorder_.capture(input, input_channels, frames);
    self->player_.render(output, output_channels, frames);
}

AudioSession::AudioSession(audioio::AudioDevice* device, engine::Engine& player,
                           engine::Recorder& recorder)
    : device_(device), player_(player), recorder_(recorder) {}

AudioSession::~AudioSession() {
    // Stop the callback before the Engine and Recorder it reaches go away.
    if (recorder_.status().recording) recorder_.stop();
    close_stream();
}

double AudioSession::open() {
    if (!device_) return 0.0;
    for (const audioio::DeviceInfo& info : device_->enumerate()) {
        if (info.is_default_output) {
            output_ = info.id;
            break;
        }
    }
    if (output_.is_none()) return 0.0;

    for (double rate : {48000.0, 44100.0}) {
        if (open_stream(audioio::DeviceId{}, rate).ok) {
            sample_rate_ = rate;
            return rate;
        }
    }
    return 0.0;
}

audioio::Result AudioSession::open_stream(audioio::DeviceId input, double sample_rate) {
    audioio::StreamConfig config;
    config.input_device = input;
    config.output_device = output_;
    config.sample_rate = sample_rate;
    config.buffer_frames = 512;
    audioio::Result opened = device_->open(config, &AudioSession::audio_callback, this);
    if (!opened.ok) return opened;
    audioio::Result started = device_->start();
    if (!started.ok) {
        device_->close();
        return started;
    }
    running_ = true;
    return audioio::Result::success();
}

void AudioSession::close_stream() {
    if (!device_ || !running_) return;
    device_->stop();
    device_->close();
    running_ = false;
}

std::vector<core::InputInfo> AudioSession::inputs() {
    std::vector<core::InputInfo> result;
    if (!device_ || sample_rate_ == 0.0) return result;
    for (const audioio::DeviceInfo& info : device_->enumerate()) {
        if (info.max_input_channels < 1) continue;
        core::InputInfo input;
        input.id = info.id.value;
        input.name = info.name;
        input.backend_name = info.backend_name;
        input.channels = std::min(info.max_input_channels, engine::Recorder::kMaxChannels);
        // The default input first, so the combo box opens on the obvious pick.
        if (info.is_default_input) {
            result.insert(result.begin(), std::move(input));
        } else {
            result.push_back(std::move(input));
        }
    }
    return result;
}

std::string AudioSession::select_input(int id) {
    audioio::DeviceId wanted{id};
    if (wanted == input_) return {};
    if (!device_ || sample_rate_ == 0.0) return "No audio output is open, so nothing can be recorded";
    if (recorder_.status().recording) return "Stop recording before changing the input";

    close_stream();
    audioio::Result result = open_stream(wanted, sample_rate_);
    if (result.ok) {
        input_ = wanted;
        return {};
    }
    // Fall back to what was running so playback keeps working; the input
    // simply is not taken.
    input_ = audioio::DeviceId{};
    open_stream(input_, sample_rate_);
    return "Could not open that input: " + result.error;
}

bool AudioSession::start_recording() {
    if (!running_ || input_.is_none()) return false;
    // What the musician heard left the machine one output latency after it
    // was rendered, and what they played arrives one input latency later.
    // Dropping that round trip from the head of the take lines it up with
    // the material they played against.
    audioio::StreamLatency latency = device_->measured_latency();
    auto skip = static_cast<std::int64_t>(
        std::llround((latency.input_seconds + latency.output_seconds) * sample_rate_));
    return recorder_.start(sample_rate_, skip);
}

core::SampleSource AudioSession::stop_recording() { return recorder_.stop(); }

core::RecorderStatus AudioSession::status() {
    core::RecorderStatus s = recorder_.status();
    s.input_open = running_ && !input_.is_none();
    // The session owns the stream rate; the Recorder only learns it when a
    // take starts, so its value is stale between takes.
    s.sample_rate = sample_rate_;
    return s;
}

}  // namespace app
