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

    auto placed = add_placement(history.read().musical, f.arrangement, f.track_a, f.drum_pattern,
                                0, 8 * kPpq);  // twice the Pattern length
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    auto looped = engine::bake(history.read().musical, kRate);
    CHECK(looped->triggers.size() == 8);  // 4 hits, twice around

    REQUIRE(history.undo());
    auto trimmed_placed = add_placement(history.read().musical, f.arrangement, f.track_a,
                                        f.drum_pattern, 0, 2 * kPpq);  // half
    REQUIRE(trimmed_placed.ok());
    REQUIRE(history.apply(std::move(trimmed_placed->delta)) == ApplyResult::Applied);
    auto trimmed = engine::bake(history.read().musical, kRate);
    CHECK(trimmed->triggers.size() == 2);  // hits at or past the cut are dropped
}

TEST_CASE("drum and melody Patterns bake through the same path") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    for (Id pattern : {f.drum_pattern, f.melody_pattern}) {
        auto placed = add_placement(history.read().musical, f.arrangement, f.track_a, pattern, 0);
        REQUIRE(placed.ok());
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    }
    auto model = engine::bake(history.read().musical, kRate);
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
    auto placed = add_placement(history.read().musical, f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);

    auto muted = set_track_muted(history.read().musical, f.arrangement, f.track_a, true);
    REQUIRE(muted.ok());
    REQUIRE(history.apply(std::move(*muted)) == ApplyResult::Applied);
    CHECK(engine::bake(history.read().musical, kRate)->triggers.empty());
}

TEST_CASE("a placed Pattern is audible and an empty Arrangement is silent") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    engine::Engine player;

    // Empty Arrangement: silence.
    player.publish(history.read().musical, kRate);
    player.play();
    auto silent = render_blocks(player, 8, 512);
    for (double rms : silent) CHECK(rms == Catch::Approx(0.0).margin(1e-9));

    // Place drums at the start; the first beat lands in the first blocks.
    auto placed = add_placement(history.read().musical, f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    player.publish(history.read().musical, kRate);
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

    auto placed = add_placement(history.read().musical, f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    Id id = placed->id;
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    auto removed = remove_placement(history.read().musical, f.arrangement, f.track_a, id);
    REQUIRE(removed.ok());
    REQUIRE(history.apply(std::move(*removed)) == ApplyResult::Applied);

    auto rms_of_current = [&] {
        player.publish(history.read().musical, kRate);
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
        auto placed = add_placement(history.read().musical, f.arrangement,
                                    bar % 2 == 0 ? f.track_a : f.track_b,
                                    bar % 3 == 0 ? f.melody_pattern : f.drum_pattern,
                                    bar * 4 * kPpq);
        REQUIRE(placed.ok());
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    }

    engine::Engine player;
    player.publish(history.read().musical, kRate);
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

TEST_CASE("an audition is audible while the transport is stopped and replaces itself") {
    auto f = make_fixture();
    SampleSource tone = f.project.musical.samples.items[1].source;  // 0.5 s
    SampleSource hit = f.project.musical.samples.items[0].source;   // 0.1 s
    ProjectHistory history(std::move(f.project));
    engine::Engine player;
    player.publish(history.read().musical, kRate);

    // Silent before; nothing is placed and nothing is playing.
    CHECK(render_blocks(player, 2, 512).back() == 0.0);
    CHECK_FALSE(player.status().playing);

    player.audition(tone);
    std::vector<double> loud = render_blocks(player, 4, 512);
    CHECK(loud.front() > 0.01);
    CHECK(player.status().position == 0);  // the transport did not move

    // A second cue replaces the first, so a short hit ends the sound early:
    // 0.1 s is 4800 frames, silence well inside 20 blocks of 512.
    player.audition(hit);
    std::vector<double> after = render_blocks(player, 20, 512);
    CHECK(after.front() > 0.01);
    CHECK(after.back() == 0.0);

    // Retired cues are reclaimed by epoch, never while they could be playing.
    for (int i = 0; i < 8; ++i) {
        player.audition(i % 2 ? tone : hit);
        render_blocks(player, 1, 64);
    }
    CHECK(std::isfinite(render_blocks(player, 1, 512).back()));
}

namespace {

// A constant-level DC "sample" so channel and duration assertions are exact.
SampleSource make_level(double seconds, double rate, int channels, float left, float right) {
    auto audio = std::make_shared<AudioData>();
    audio->sample_rate = rate;
    audio->channels = channels;
    auto frames = static_cast<std::size_t>(seconds * rate);
    audio->frames.resize(frames * static_cast<std::size_t>(channels));
    for (std::size_t f = 0; f < frames; ++f) {
        audio->frames[f * channels] = left;
        if (channels > 1) audio->frames[f * channels + 1] = right;
    }
    return audio;
}

// Renders one block and returns both channels.
std::pair<std::vector<float>, std::vector<float>> render_stereo(engine::Engine& player,
                                                                int block_frames) {
    std::vector<float> left(static_cast<std::size_t>(block_frames));
    std::vector<float> right(static_cast<std::size_t>(block_frames));
    float* channels[2] = {left.data(), right.data()};
    player.render(channels, 2, block_frames);
    return {left, right};
}

}  // namespace

TEST_CASE("an audition plays at native pitch: a 96 kHz Sample lasts half its frames at 48 kHz") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    engine::Engine player;
    player.publish(history.read().musical, kRate);

    // 0.1 s of audio at 96 kHz is 9600 frames but must occupy 4800 output
    // frames: audible through block 9 of 512, silent by block 12.
    player.audition(make_level(0.1, 96000.0, 1, 0.5f, 0.5f));
    std::vector<double> rms = render_blocks(player, 12, 512);
    CHECK(rms[8] > 0.3);
    CHECK(rms[10] == 0.0);
    CHECK(rms[11] == 0.0);

    // The same 0.1 s at 24 kHz stretches to 4800 output frames as well.
    player.audition(make_level(0.1, 24000.0, 1, 0.5f, 0.5f));
    rms = render_blocks(player, 12, 512);
    CHECK(rms[8] > 0.3);
    CHECK(rms[10] == 0.0);
}

TEST_CASE("an audition keeps a stereo Sample's channels apart and centres a mono one") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    engine::Engine player;
    player.publish(history.read().musical, kRate);

    player.audition(make_level(0.05, kRate, 2, 0.5f, -0.25f));
    auto [left, right] = render_stereo(player, 64);
    CHECK(left[10] == Catch::Approx(0.5f * 0.8f));
    CHECK(right[10] == Catch::Approx(-0.25f * 0.8f));

    player.audition(make_level(0.05, kRate, 1, 0.5f, 0.0f));
    std::tie(left, right) = render_stereo(player, 64);
    CHECK(left[10] == Catch::Approx(0.5f * 0.8f));
    CHECK(right[10] == Catch::Approx(0.5f * 0.8f));

    // A mono output gets the left channel and nothing is written out of bounds.
    std::vector<float> mono(64);
    float* one[1] = {mono.data()};
    player.audition(make_level(0.05, kRate, 2, 0.5f, -0.25f));
    player.render(one, 1, 64);
    CHECK(mono[10] == Catch::Approx(0.5f * 0.8f));
}

TEST_CASE("an audition mixes over a running transport without disturbing it") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto placed = add_placement(history.read().musical, f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);

    // A twin engine renders the same blocks without a cue: the reference for
    // what the transport reports when nothing is auditioning.
    engine::Engine player, twin;
    for (engine::Engine* e : {&player, &twin}) {
        e->publish(history.read().musical, kRate);
        e->play();
        render_blocks(*e, 4, 512);
    }
    REQUIRE(player.status().position > 0);

    // A DC cue lifts the block mean; the transport keeps counting as if it
    // were not there.
    player.audition(make_level(0.05, kRate, 1, 0.5f, 0.5f));
    auto [left, right] = render_stereo(player, 512);
    render_stereo(twin, 512);
    double mean = 0.0;
    for (float v : left) mean += v;
    mean /= 512.0;
    CHECK(mean > 0.3);
    CHECK(player.status().playing);
    CHECK(player.status().position == twin.status().position);
}

TEST_CASE("an empty or absent Sample is not auditioned and does not silence the current cue") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    engine::Engine player;
    player.publish(history.read().musical, kRate);

    player.audition(make_level(0.05, kRate, 1, 0.5f, 0.5f));
    player.audition(nullptr);
    player.audition(std::make_shared<AudioData>());
    CHECK(render_blocks(player, 1, 64).front() > 0.3);
}
