// FMEA row: Engine publish()/reclaim vs render() — the atomic model swap and
// epoch-based retirement. A publish storm on the edit thread races a running
// audio thread; the contract is no use-after-free, no torn state and finite
// output throughout. Confirmed in the PR #30 FMEA interview (Q2: in scope).
// Deliberately no timing assertions — the test pins safety, not speed.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "core/arrangement_functions.h"
#include "core/history.h"
#include "engine/engine.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

TEST_CASE("a publish storm against a running audio thread stays clean") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    // Real triggers on both Tracks so renders do actual voice work.
    for (int bar = 0; bar < 8; ++bar) {
        auto placed = add_placement(history.read().musical, f.arrangement,
                                    bar % 2 == 0 ? f.track_a : f.track_b,
                                    bar % 3 == 0 ? f.melody_pattern : f.drum_pattern,
                                    bar * 4 * kPpq);
        REQUIRE(placed.ok());
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    }

    engine::Engine player;
    player.publish(history.read().musical, 48000.0);
    player.play();

    std::atomic<bool> done{false};
    std::atomic<bool> non_finite_sample{false};
    std::atomic<long long> blocks_rendered{0};

    std::thread audio([&] {
        std::vector<float> left(256), right(256);
        float* channels[2] = {left.data(), right.data()};
        while (!done.load(std::memory_order_acquire)) {
            player.render(channels, 2, 256);
            blocks_rendered.fetch_add(1, std::memory_order_relaxed);
            for (float v : left) {
                if (!std::isfinite(v)) non_finite_sample.store(true);
            }
        }
    });

    // Edit thread: mutate and republish as fast as possible, with seeks mixed
    // in. Half the mutes are same-value no-ops; every iteration republishes,
    // which is the reclaim path's worst case.
    for (int i = 0; i < 4000; ++i) {
        auto muted = set_track_muted(history.read().musical, f.arrangement, f.track_a, i % 2 == 0);
        REQUIRE(muted.ok());
        history.apply(std::move(*muted));
        player.publish(history.read().musical, 48000.0);
        if (i % 50 == 0) player.seek(0);
    }

    done.store(true, std::memory_order_release);
    audio.join();

    CHECK_FALSE(non_finite_sample.load());
    CHECK(blocks_rendered.load() > 0);

    // The engine is still coherent after the storm: it accepts a publish and
    // renders a finite block on this thread.
    player.publish(history.read().musical, 48000.0);
    std::vector<float> left(512), right(512);
    float* channels[2] = {left.data(), right.data()};
    player.render(channels, 2, 512);
    for (float v : left) REQUIRE(std::isfinite(v));
    CHECK(player.status().position >= 0);
}

// Same FMEA row, the AuditionPort hand-over: cues are handed to the audio
// thread by the same pointer swap and reclaimed through the same epoch list
// as RenderModels, so a storm of auditions interleaved with publishes must
// leave no dangling cue while one may still be playing.
TEST_CASE("an audition storm against a running audio thread stays clean") {
    auto f = make_fixture();
    SampleSource hit = f.project.musical.samples.items[0].source;
    SampleSource tone = f.project.musical.samples.items[1].source;
    ProjectHistory history(std::move(f.project));

    engine::Engine player;
    player.publish(history.read().musical, 48000.0);
    player.play();

    std::atomic<bool> done{false};
    std::atomic<bool> non_finite_sample{false};
    std::atomic<long long> blocks_rendered{0};

    std::thread audio([&] {
        std::vector<float> left(256), right(256);
        float* channels[2] = {left.data(), right.data()};
        while (!done.load(std::memory_order_acquire)) {
            player.render(channels, 2, 256);
            blocks_rendered.fetch_add(1, std::memory_order_relaxed);
            for (float v : left) {
                if (!std::isfinite(v)) non_finite_sample.store(true);
            }
        }
    });

    // Edit thread: fresh cue objects every time (a shared source, but each
    // audition allocates its own AuditionCue), with publishes mixed in so the
    // retire list interleaves both object kinds.
    for (int i = 0; i < 8000; ++i) {
        player.audition(i % 3 == 0 ? tone : hit);
        if (i % 7 == 0) player.publish(history.read().musical, 48000.0);
    }

    done.store(true, std::memory_order_release);
    audio.join();

    CHECK_FALSE(non_finite_sample.load());
    CHECK(blocks_rendered.load() > 0);

    std::vector<float> left(512), right(512);
    float* channels[2] = {left.data(), right.data()};
    player.audition(hit);
    player.render(channels, 2, 512);
    for (float v : left) REQUIRE(std::isfinite(v));
}
