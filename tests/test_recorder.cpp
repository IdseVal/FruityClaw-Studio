// The recorder: what the audio callback captures comes back as a take,
// bit-for-bit, at the stream rate, minus the latency skip — and an overrun
// counts frames rather than blocking the audio thread.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

#include "engine/recorder.h"

using engine::Recorder;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

// A stereo block whose samples encode (channel, absolute frame), so the take
// can be checked against what went in.
struct Block {
    std::vector<float> left, right;
    const float* channels[2];

    explicit Block(int first_frame) : left(kBlock), right(kBlock) {
        for (int i = 0; i < kBlock; ++i) {
            left[static_cast<std::size_t>(i)] = static_cast<float>(first_frame + i) / 1e6f;
            right[static_cast<std::size_t>(i)] = -static_cast<float>(first_frame + i) / 1e6f;
        }
        channels[0] = left.data();
        channels[1] = right.data();
    }
};

}  // namespace

TEST_CASE("input is metered but not stored while no take runs") {
    Recorder recorder;
    Block block(0);
    recorder.capture(block.channels, 2, kBlock);

    core::RecorderStatus status = recorder.status();
    CHECK_FALSE(status.recording);
    CHECK(status.frames == 0);
    CHECK(status.peak == Catch::Approx(static_cast<float>(kBlock - 1) / 1e6f));
    // Reading the peak resets the hold.
    CHECK(recorder.status().peak == 0.0f);
    CHECK(recorder.stop() == nullptr);
}

TEST_CASE("a take round-trips the captured input minus the latency skip") {
    Recorder recorder;
    const std::int64_t skip = 100;
    REQUIRE(recorder.start(kRate, skip));
    CHECK_FALSE(recorder.start(kRate, 0));  // one take at a time

    const int blocks = 3;
    for (int b = 0; b < blocks; ++b) {
        Block block(b * kBlock);
        recorder.capture(block.channels, 2, kBlock);
    }
    core::SampleSource take = recorder.stop();
    REQUIRE(take);
    CHECK(take->sample_rate == kRate);
    CHECK(take->channels == 2);
    REQUIRE(take->frame_count() == blocks * kBlock - skip);

    for (std::int64_t f = 0; f < take->frame_count(); ++f) {
        float expected = static_cast<float>(f + skip) / 1e6f;
        REQUIRE(take->frames[static_cast<std::size_t>(f) * 2] == expected);
        REQUIRE(take->frames[static_cast<std::size_t>(f) * 2 + 1] == -expected);
    }
    CHECK(recorder.status().dropped_frames == 0);
    CHECK_FALSE(recorder.status().recording);
}

TEST_CASE("a mono input yields a mono take") {
    Recorder recorder;
    REQUIRE(recorder.start(kRate, 0));
    Block block(0);
    recorder.capture(block.channels, 1, kBlock);
    core::SampleSource take = recorder.stop();
    REQUIRE(take);
    CHECK(take->channels == 1);
    CHECK(take->frame_count() == kBlock);
    CHECK(take->frames[kBlock - 1] == static_cast<float>(kBlock - 1) / 1e6f);
}

TEST_CASE("the drain thread keeps up with a long take and reports its length") {
    Recorder recorder;
    REQUIRE(recorder.start(kRate, 0));
    // Ten seconds of audio, more than the ring holds, fed at real-time pace
    // in coarse steps so the drain thread has to move it.
    const int blocks = static_cast<int>(10.0 * kRate / kBlock);
    for (int b = 0; b < blocks; ++b) {
        Block block(b * kBlock);
        recorder.capture(block.channels, 2, kBlock);
        if (b % 50 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    core::RecorderStatus mid = recorder.status();
    CHECK(mid.recording);
    CHECK(mid.frames > 0);

    core::SampleSource take = recorder.stop();
    REQUIRE(take);
    CHECK(take->frame_count() == blocks * kBlock);
    CHECK(recorder.status().dropped_frames == 0);
}

TEST_CASE("a block larger than the free ring drops the excess and counts it") {
    Recorder recorder;
    REQUIRE(recorder.start(kRate, 0));
    const int frames = static_cast<int>(Recorder::kRingFrames) + 1000;
    std::vector<float> mono(static_cast<std::size_t>(frames), 0.5f);
    const float* channels[1] = {mono.data()};
    recorder.capture(channels, 1, frames);

    CHECK(recorder.status().dropped_frames >= 1000);
    core::SampleSource take = recorder.stop();
    REQUIRE(take);
    CHECK(take->frame_count() == static_cast<std::int64_t>(Recorder::kRingFrames));
}

TEST_CASE("a take shorter than the latency skip is nothing") {
    Recorder recorder;
    REQUIRE(recorder.start(kRate, 10 * kBlock));
    Block block(0);
    recorder.capture(block.channels, 2, kBlock);
    CHECK(recorder.stop() == nullptr);
}

TEST_CASE("a second take on the same recorder starts clean") {
    Recorder recorder;

    // Take 1: stereo, with a dropped-frame overrun so every counter is dirty.
    REQUIRE(recorder.start(kRate, 0));
    const int oversize = static_cast<int>(Recorder::kRingFrames) + 64;
    std::vector<float> big(static_cast<std::size_t>(oversize), 0.25f);
    const float* big_channels[2] = {big.data(), big.data()};
    recorder.capture(big_channels, 2, oversize);
    core::SampleSource first = recorder.stop();
    REQUIRE(first);
    CHECK(first->channels == 2);
    CHECK(recorder.status().dropped_frames == 64);

    // Take 2: mono, two blocks, a different skip. Nothing of take 1 may leak
    // into it: not its channel count, its frames, its skip or its overrun.
    const std::int64_t skip = 7;
    REQUIRE(recorder.start(kRate, skip));
    for (int b = 0; b < 2; ++b) {
        Block block(b * kBlock);
        recorder.capture(block.channels, 1, kBlock);
    }
    core::RecorderStatus status = recorder.status();
    CHECK(status.recording);
    CHECK(status.dropped_frames == 0);

    core::SampleSource second = recorder.stop();
    REQUIRE(second);
    CHECK(second->channels == 1);
    REQUIRE(second->frame_count() == 2 * kBlock - skip);
    for (std::int64_t f = 0; f < second->frame_count(); ++f)
        REQUIRE(second->frames[static_cast<std::size_t>(f)] == static_cast<float>(f + skip) / 1e6f);
    // The first take is untouched by the second.
    CHECK(first->frame_count() == static_cast<std::int64_t>(Recorder::kRingFrames));
}

TEST_CASE("the latency skip carries across drain cycles") {
    Recorder recorder;
    // Longer than one block, shorter than two: the drain thread must skip
    // all of the first block and part of the second, in separate passes.
    const std::int64_t skip = kBlock + 200;
    REQUIRE(recorder.start(kRate, skip));
    const int blocks = 4;
    for (int b = 0; b < blocks; ++b) {
        Block block(b * kBlock);
        recorder.capture(block.channels, 2, kBlock);
        // Let the drain thread (10 ms period) run between blocks so the skip
        // is consumed piecemeal rather than in one stop-time drain.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    core::RecorderStatus mid = recorder.status();
    CHECK(mid.frames == blocks * kBlock - skip);

    core::SampleSource take = recorder.stop();
    REQUIRE(take);
    REQUIRE(take->frame_count() == blocks * kBlock - skip);
    for (std::int64_t f = 0; f < take->frame_count(); ++f) {
        float expected = static_cast<float>(f + skip) / 1e6f;
        REQUIRE(take->frames[static_cast<std::size_t>(f) * 2] == expected);
        REQUIRE(take->frames[static_cast<std::size_t>(f) * 2 + 1] == -expected);
    }
}
