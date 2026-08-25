// Captures the input side of the audio callback into a take.
//
// Thread contract (architecture-seams section 4 and the #14 row of section
// 6: input arrives through AudioDevice; writing happens off the audio thread):
//   - capture() runs on the audio callback thread. It copies into a
//     preallocated single-producer single-consumer ring and touches nothing
//     else: no allocation, no locks, no I/O.
//   - A drain thread, alive only while a take runs, moves the ring into the
//     growing take. It is the only place memory grows, and it keeps draining
//     while the UI is blocked in a dialog, so a stalled UI never loses input.
//   - start() and stop() run on the edit thread. stop() joins the drain
//     thread and returns the take as an immutable AudioData.
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "core/entities.h"
#include "core/playback.h"

namespace engine {

class Recorder {
public:
    static constexpr int kMaxChannels = 2;

    // Ring capacity in frames: ~5.5 s at 48 kHz. The drain thread empties it
    // every 10 ms; a single block larger than what is free loses the excess
    // and counts it as dropped, never blocks.
    static constexpr std::size_t kRingFrames = std::size_t{1} << 18;

    Recorder();
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Edit thread. Begins a take at `sample_rate`. The first `skip_frames`
    // captured frames are discarded: the round-trip latency the musician
    // played against, so the take lines up with what they heard. False when
    // a take is already running.
    bool start(double sample_rate, std::int64_t skip_frames);

    // Edit thread. Ends the take. Null when nothing was recording or nothing
    // survived the latency skip.
    core::SampleSource stop();

    // Edit thread. Reading resets the peak hold.
    core::RecorderStatus status();

    // Audio thread. `input` may be null (output-only stream); the peak is
    // tracked whenever input is present so the meter works before a take.
    void capture(const float* const* input, int input_channels, int frames);

private:
    void drain_loop();
    void drain_once();

    // --- shared state ------------------------------------------------------
    std::vector<float> ring_;  // interleaved, kMaxChannels wide, fixed size
    std::atomic<std::uint64_t> write_frames_{0};  // audio thread advances
    std::atomic<std::uint64_t> read_frames_{0};   // drain thread advances
    std::atomic<bool> recording_{false};
    std::atomic<int> channels_{0};  // fixed by the first captured block of a take
    std::atomic<float> peak_{0.0f};
    std::atomic<std::int64_t> dropped_{0};
    std::atomic<std::int64_t> take_frames_{0};

    // --- drain-thread state --------------------------------------------------
    std::vector<float> take_;  // interleaved at channels_
    std::int64_t skip_remaining_ = 0;
    std::thread drain_;
    std::atomic<bool> drain_run_{false};

    // --- edit-thread state ----------------------------------------------------
    double sample_rate_ = 48000.0;
};

}  // namespace engine
