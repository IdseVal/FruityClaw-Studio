// The one audio stream of the Studio, and the RecorderPort over it.
//
// Lives in the composition root because it is the only place that may hold
// both ends of the wiring: the AudioDevice adapter (seam A) on one side and
// the Engine and Recorder on the other. The callback it installs runs
// capture then render, so a take and playback share one clock — which is
// what lets a musician record against existing material.
//
// Selecting an input reopens the stream full duplex on that input. The
// output stays the default output, at the sample rate the session opened
// with, so nothing published to the Engine goes stale.
#pragma once

#include <string>
#include <vector>

#include "audioio/audio_device.h"
#include "core/playback.h"
#include "engine/engine.h"
#include "engine/recorder.h"

namespace app {

class AudioSession final : public core::RecorderPort {
public:
    // `device` may be null: the Studio then runs silent and no input is
    // offered, and every editing surface still works (core document 1.1a).
    AudioSession(audioio::AudioDevice* device, engine::Engine& player,
                 engine::Recorder& recorder);
    ~AudioSession() override;

    AudioSession(const AudioSession&) = delete;
    AudioSession& operator=(const AudioSession&) = delete;

    // Opens the default output, output-only. Returns the sample rate the
    // stream runs at, or 0 when no output could be opened.
    double open();

    // The rate the Engine must bake at. 48000 when running silent.
    double sample_rate() const { return sample_rate_ > 0.0 ? sample_rate_ : 48000.0; }

    // RecorderPort.
    std::vector<core::InputInfo> inputs() override;
    int selected_input() const override { return input_.value; }
    std::string select_input(int id) override;
    bool start_recording() override;
    core::SampleSource stop_recording() override;
    core::RecorderStatus status() override;

private:
    // The audio thread: capture the input side, then render the output side.
    static void audio_callback(const float* const* input, int input_channels,
                               float* const* output, int output_channels, int frames,
                               const audioio::StreamTime& time, void* user_data);

    audioio::Result open_stream(audioio::DeviceId input, double sample_rate);
    void close_stream();

    audioio::AudioDevice* device_;
    engine::Engine& player_;
    engine::Recorder& recorder_;
    audioio::DeviceId output_;
    audioio::DeviceId input_;
    double sample_rate_ = 0.0;
    bool running_ = false;
};

}  // namespace app
