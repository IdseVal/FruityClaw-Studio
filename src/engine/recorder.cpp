#include "engine/recorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace engine {

Recorder::Recorder() : ring_(kRingFrames * kMaxChannels, 0.0f) {}

Recorder::~Recorder() {
    if (recording_.load(std::memory_order_acquire)) stop();
}

bool Recorder::start(double sample_rate, std::int64_t skip_frames) {
    if (recording_.load(std::memory_order_acquire)) return false;

    sample_rate_ = sample_rate;
    take_.clear();
    take_frames_.store(0, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
    skip_remaining_ = std::max<std::int64_t>(skip_frames, 0);
    channels_.store(0, std::memory_order_relaxed);
    // Nothing is written while recording_ is false, so the ring is quiescent
    // here; the counters are simply realigned rather than reset, which keeps
    // them monotonic for the audio thread.
    read_frames_.store(write_frames_.load(std::memory_order_acquire),
                       std::memory_order_release);

    drain_run_.store(true, std::memory_order_release);
    drain_ = std::thread([this] { drain_loop(); });
    recording_.store(true, std::memory_order_release);
    return true;
}

core::SampleSource Recorder::stop() {
    if (!recording_.load(std::memory_order_acquire)) return nullptr;
    recording_.store(false, std::memory_order_release);

    drain_run_.store(false, std::memory_order_release);
    if (drain_.joinable()) drain_.join();
    // A block written after the flag flipped but before the last drain ran
    // belongs to the take: it was captured while the user held record.
    drain_once();

    int channels = channels_.load(std::memory_order_acquire);
    if (channels == 0 || take_.empty()) return nullptr;

    auto audio = std::make_shared<core::AudioData>();
    audio->sample_rate = sample_rate_;
    audio->channels = channels;
    audio->frames = std::move(take_);
    take_ = {};
    return audio;
}

core::RecorderStatus Recorder::status() {
    core::RecorderStatus s;
    s.recording = recording_.load(std::memory_order_acquire);
    s.frames = take_frames_.load(std::memory_order_acquire);
    s.sample_rate = sample_rate_;
    s.peak = peak_.exchange(0.0f, std::memory_order_acq_rel);
    s.dropped_frames = dropped_.load(std::memory_order_acquire);
    return s;
}

void Recorder::capture(const float* const* input, int input_channels, int frames) {
    if (!input || input_channels <= 0 || frames <= 0) return;
    int channels = std::min(input_channels, kMaxChannels);

    float block_peak = 0.0f;
    for (int c = 0; c < channels; ++c) {
        for (int i = 0; i < frames; ++i) block_peak = std::max(block_peak, std::fabs(input[c][i]));
    }
    // Max-hold until the next status() read, so a transient between two UI
    // polls still reaches the meter.
    float held = peak_.load(std::memory_order_relaxed);
    while (block_peak > held &&
           !peak_.compare_exchange_weak(held, block_peak, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
    }

    if (!recording_.load(std::memory_order_acquire)) return;

    int expected = 0;
    channels_.compare_exchange_strong(expected, channels, std::memory_order_acq_rel);
    channels = channels_.load(std::memory_order_relaxed);

    std::uint64_t write = write_frames_.load(std::memory_order_relaxed);
    std::uint64_t read = read_frames_.load(std::memory_order_acquire);
    std::uint64_t free_frames = kRingFrames - (write - read);
    int n = static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(frames), free_frames));

    for (int i = 0; i < n; ++i) {
        std::size_t slot = static_cast<std::size_t>((write + static_cast<std::uint64_t>(i)) %
                                                    kRingFrames) *
                           kMaxChannels;
        for (int c = 0; c < channels; ++c) {
            // A take is 1 or 2 channels wide; a mono take on a stereo-capable
            // ring simply leaves the second slot unused.
            ring_[slot + static_cast<std::size_t>(c)] = input[c][i];
        }
    }
    write_frames_.store(write + static_cast<std::uint64_t>(n), std::memory_order_release);
    if (n < frames) dropped_.fetch_add(frames - n, std::memory_order_relaxed);
}

void Recorder::drain_loop() {
    while (drain_run_.load(std::memory_order_acquire)) {
        drain_once();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Recorder::drain_once() {
    std::uint64_t read = read_frames_.load(std::memory_order_relaxed);
    std::uint64_t write = write_frames_.load(std::memory_order_acquire);
    int channels = channels_.load(std::memory_order_acquire);
    if (write == read || channels == 0) return;

    std::uint64_t available = write - read;
    std::uint64_t skipped = std::min<std::uint64_t>(
        available, static_cast<std::uint64_t>(skip_remaining_));
    skip_remaining_ -= static_cast<std::int64_t>(skipped);

    for (std::uint64_t f = skipped; f < available; ++f) {
        std::size_t slot = static_cast<std::size_t>((read + f) % kRingFrames) * kMaxChannels;
        for (int c = 0; c < channels; ++c) take_.push_back(ring_[slot + static_cast<std::size_t>(c)]);
    }
    read_frames_.store(write, std::memory_order_release);
    take_frames_.store(static_cast<std::int64_t>(take_.size() / static_cast<std::size_t>(channels)),
                       std::memory_order_release);
}

}  // namespace engine
