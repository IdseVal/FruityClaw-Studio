// The playback engine: renders the published RenderModel from the audio
// callback and implements the TransportPort the UI drives.
//
// Thread contract (architecture-seams section 4):
//   - publish() and reclaim run on the edit (message) thread.
//   - render() runs on the audio callback thread and obeys the realtime
//     rules: no allocation, no locks, no I/O. Model hand-over is one atomic
//     pointer swap; displaced models are retired and deleted back on the
//     edit thread once the audio thread has provably moved past them.
//   - play/stop/seek are trivially copyable writes into atomics drained by
//     the audio thread; status() reads what the audio thread publishes.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/playback.h"
#include "engine/render_model.h"

namespace engine {

class Engine : public core::TransportPort {
public:
    Engine() = default;
    ~Engine() override;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Edit thread. Bakes and publishes; the audio thread picks the new model
    // up at its next block. Also reclaims models the audio thread has left.
    void publish(const core::Project& project, double sample_rate);

    // Audio thread. Renders `frames` samples into non-interleaved planar
    // output. Callable directly from tests for offline rendering.
    void render(float* const* output, int output_channels, int frames);

    // TransportPort.
    void play() override;
    void stop() override;
    void seek(core::Ticks position) override;
    core::PlaybackStatus status() const override;

private:
    struct Voice {
        bool active = false;
        std::uint32_t instrument = 0;
        double src_pos = 0.0;
        double rate = 1.0;
        float gain_l = 0.0f;
        float gain_r = 0.0f;
        std::int64_t gate_remaining = 0;  // Sustain only; <0 = no gate
        float envelope = 1.0f;            // release ramp after gate closes
    };

    void start_voice(const RenderModel& model, const Trigger& trigger);
    void render_voices(const RenderModel& model, float* const* output,
                       int output_channels, int frames);

    static constexpr int kMaxVoices = 64;
    static constexpr float kReleasePerSample = 1.0f / 480.0f;  // ~10 ms at 48k

    // --- shared state ------------------------------------------------------
    std::atomic<const RenderModel*> current_{nullptr};
    std::atomic<bool> playing_{false};
    std::atomic<std::int64_t> seek_samples_{-1};      // -1 = no pending seek
    std::atomic<std::int64_t> playhead_samples_{0};   // audio thread publishes
    std::atomic<std::uint64_t> audio_epoch_{0};       // bumped per callback

    // --- audio-thread state ------------------------------------------------
    std::int64_t position_ = 0;
    std::size_t next_trigger_ = 0;
    const RenderModel* last_model_ = nullptr;
    std::array<Voice, kMaxVoices> voices_{};

    // --- edit-thread state --------------------------------------------------
    struct Retired {
        std::shared_ptr<const RenderModel> model;
        std::uint64_t epoch;
    };
    std::shared_ptr<const RenderModel> published_;  // owns what current_ points at
    std::vector<Retired> retired_;
    double sample_rate_ = 48000.0;
};

}  // namespace engine
