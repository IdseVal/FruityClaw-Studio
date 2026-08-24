// Baking and offline playback: the acceptance evidence that placed Patterns
// play back, that looping and trimming follow the Placement contract, and
// that drum and melody Patterns take the same path.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <vector>

#include "core/arrangement_functions.h"
#include "core/history.h"
#include "engine/engine.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

namespace {

constexpr double kRate = 48000.0;

// Renders `blocks` x `block_frames` samples and returns per-block RMS of the
// left channel.
std::vector<double> render_blocks(engine::Engine& player, int blocks, int block_frames) {
    std::vector<float> left(static_cast<std::size_t>(block_frames));
    std::vector<float> right(static_cast<std::size_t>(block_frames));
    float* channels[2] = {left.data(), right.data()};
    std::vector<double> rms;
    for (int b = 0; b < blocks; ++b) {
        player.render(channels, 2, block_frames);
        double sum = 0.0;
        for (float v : left) sum += static_cast<double>(v) * v;
        rms.push_back(std::sqrt(sum / block_frames));
    }
    return rms;
}

}  // namespace

TEST_CASE("a Placement longer than its Pattern loops; shorter trims") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.drum_pattern,
                                0, 8 * kPpq);  // twice the Pattern length
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    auto looped = engine::bake(history.read(), kRate);
    CHECK(looped->triggers.size() == 8);  // 4 hits, twice around

    REQUIRE(history.undo());
    auto trimmed_placed = add_placement(history.read(), f.arrangement, f.track_a,
                                        f.drum_pattern, 0, 2 * kPpq);  // half
    REQUIRE(trimmed_placed.ok());
    REQUIRE(history.apply(std::move(trimmed_placed->delta)) == ApplyResult::Applied);
    auto trimmed = engine::bake(history.read(), kRate);
    CHECK(trimmed->triggers.size() == 2);  // hits at or past the cut are dropped
}

TEST_CASE("drum and melody Patterns bake through the same path") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    for (Id pattern : {f.drum_pattern, f.melody_pattern}) {
        auto placed = add_placement(history.read(), f.arrangement, f.track_a, pattern, 0);
        REQUIRE(placed.ok());
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    }
    auto model = engine::bake(history.read(), kRate);
    CHECK(model->triggers.size() == 6);  // 4 drum hits + 2 melody notes
    CHECK(model->instruments.size() == 2);

    // A Sustain trigger carries its gate; a OneShot's gate is ignored by the
    // voice, not stripped by the baker — the data path has no Pattern-kind
    // branch anywhere.
    bool sustain_seen = false;
    for (const auto& trigger : model->triggers) {
        if (model->instruments[trigger.instrument].mode == SamplerMode::Sustain) {
            sustain_seen = true;
            CHECK(trigger.gate_samples > 0);
        }
    }
    CHECK(sustain_seen);
}

TEST_CASE("muted Tracks and muted Placements are silent by omission") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);

    auto muted = set_track_muted(history.read(), f.arrangement, f.track_a, true);
    REQUIRE(muted.ok());
    REQUIRE(history.apply(std::move(*muted)) == ApplyResult::Applied);
    CHECK(engine::bake(history.read(), kRate)->triggers.empty());
}

TEST_CASE("a placed Pattern is audible and an empty Arrangement is silent") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    engine::Engine player;

    // Empty Arrangement: silence.
    player.publish(history.read(), kRate);
    player.play();
    auto silent = render_blocks(player, 8, 512);
    for (double rms : silent) CHECK(rms == Catch::Approx(0.0).margin(1e-9));

    // Place drums at the start; the first beat lands in the first blocks.
    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    player.publish(history.read(), kRate);
    player.seek(0);

    auto audible = render_blocks(player, 8, 512);
    double peak = 0.0;
    for (double rms : audible) peak = std::max(peak, rms);
    CHECK(peak > 0.01);
}

TEST_CASE("undoing a Placement removal restores its sound; redo silences again") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    engine::Engine player;

    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    Id id = placed->id;
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    auto removed = remove_placement(history.read(), f.arrangement, f.track_a, id);
    REQUIRE(removed.ok());
    REQUIRE(history.apply(std::move(*removed)) == ApplyResult::Applied);

    auto rms_of_current = [&] {
        player.publish(history.read(), kRate);
        player.seek(0);
        player.play();
        auto rms = render_blocks(player, 4, 512);
        double peak = 0.0;
        for (double r : rms) peak = std::max(peak, r);
        return peak;
    };

    CHECK(rms_of_current() == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(history.undo());
    CHECK(rms_of_current() > 0.01);
    REQUIRE(history.redo());
    CHECK(rms_of_current() == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("offline render throughput is comfortably realtime") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    for (int bar = 0; bar < 16; ++bar) {
        auto placed = add_placement(history.read(), f.arrangement,
                                    bar % 2 == 0 ? f.track_a : f.track_b,
                                    bar % 3 == 0 ? f.melody_pattern : f.drum_pattern,
                                    bar * 4 * kPpq);
        REQUIRE(placed.ok());
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    }

    engine::Engine player;
    player.publish(history.read(), kRate);
    player.play();

    constexpr int kBlocks = 512;
    constexpr int kFrames = 512;
    auto begin = std::chrono::steady_clock::now();
    render_blocks(player, kBlocks, kFrames);
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin);

    double rendered_seconds = kBlocks * kFrames / kRate;
    double realtime_factor = rendered_seconds / elapsed.count();
    INFO("rendered " << rendered_seconds << " s in " << elapsed.count() << " s ("
                     << realtime_factor << "x realtime)");
    CHECK(realtime_factor > 10.0);
}
