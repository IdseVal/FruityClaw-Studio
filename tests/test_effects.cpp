// The six stock Effects behind the Processor seam: each declares the core
// schema, each audibly does its job on a known signal, state round-trips,
// and the engine applies chains per Instrument and on the master with
// tails that survive a republish.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "core/arrangement_functions.h"
#include "core/effect_functions.h"
#include "core/effect_schema.h"
#include "core/history.h"
#include "effects/stock_effects.h"
#include "engine/engine.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Stereo {
    std::vector<float> left, right;
    explicit Stereo(int frames) : left(frames), right(frames) {}
    int frames() const { return static_cast<int>(left.size()); }
    float peak() const {
        float p = 0.0f;
        for (int i = 0; i < frames(); ++i)
            p = std::max({p, std::fabs(left[i]), std::fabs(right[i])});
        return p;
    }
    double rms(int from = 0, int to = -1) const {
        if (to < 0) to = frames();
        double sum = 0.0;
        for (int i = from; i < to; ++i) sum += static_cast<double>(left[i]) * left[i];
        return std::sqrt(sum / std::max(1, to - from));
    }
    bool finite() const {
        for (int i = 0; i < frames(); ++i)
            if (!std::isfinite(left[i]) || !std::isfinite(right[i])) return false;
        return true;
    }
};

// A 220 Hz sine at `amplitude` for `seconds`, then silence to fill.
Stereo sine(double seconds, double total_seconds, float amplitude) {
    Stereo s(static_cast<int>(total_seconds * kRate));
    int on = static_cast<int>(seconds * kRate);
    for (int i = 0; i < on; ++i) {
        float v = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / kRate));
        s.left[i] = s.right[i] = v;
    }
    return s;
}

// Runs the signal through the Processor in engine-sized blocks, in place.
void run(engine::Processor& fx, Stereo& s) {
    engine::EventList none;
    for (int at = 0; at < s.frames(); at += kBlock) {
        float* channels[2] = {s.left.data() + at, s.right.data() + at};
        engine::AudioBuffer buffer{channels, 2, std::min(kBlock, s.frames() - at)};
        fx.process(buffer, none);
    }
}

std::unique_ptr<engine::Processor> prepared(EffectType type) {
    auto fx = effects::make_effect(type);
    REQUIRE(fx);
    fx->prepare(kRate, engine::Engine::kMaxBlockFrames);
    return fx;
}

engine::ParamId id_of(const engine::Processor& fx, std::string_view name) {
    for (const auto& d : fx.parameters())
        if (d.name == name) return d.stableId;
    FAIL("no parameter " << name);
    return 0;
}

}  // namespace

TEST_CASE("every stock Effect declares exactly the core schema, at its defaults") {
    for (int i = 0; i < kEffectTypeCount; ++i) {
        auto type = static_cast<EffectType>(i);
        auto fx = prepared(type);
        auto schema = effect_parameters(type);
        REQUIRE(fx->parameters().size() == schema.size());
        for (std::size_t p = 0; p < schema.size(); ++p) {
            const auto& d = fx->parameters()[p];
            CHECK(d.stableId == p);
            CHECK(d.name == schema[p].name);
            CHECK(d.minValue == schema[p].min_value);
            CHECK(d.maxValue == schema[p].max_value);
            CHECK(d.defaultValue == schema[p].default_value);
            CHECK(fx->getParameter(d.stableId) == Catch::Approx(schema[p].default_value));
        }
    }
}

TEST_CASE("state round-trips through opaque bytes") {
    for (int i = 0; i < kEffectTypeCount; ++i) {
        auto type = static_cast<EffectType>(i);
        auto a = prepared(type);
        auto b = prepared(type);
        for (const auto& d : a->parameters())
            a->setParameter(d.stableId, d.minValue + 0.37f * (d.maxValue - d.minValue));
        REQUIRE(b->loadState(a->saveState()));
        for (const auto& d : a->parameters())
            CHECK(b->getParameter(d.stableId) == Catch::Approx(a->getParameter(d.stableId)));
        CHECK_FALSE(b->loadState(std::vector<std::byte>(3)));
    }
}

TEST_CASE("EQ: a treble boost changes the signal; fully dry is identity") {
    auto fx = prepared(EffectType::Eq);
    Stereo in = sine(0.5, 0.5, 0.5f);

    Stereo dry = in;
    fx->setParameter(id_of(*fx, "Dry/Wet"), 0.0f);
    run(*fx, dry);
    CHECK(dry.finite());
    for (int i = 0; i < in.frames(); i += 997) CHECK(dry.left[i] == Catch::Approx(in.left[i]).margin(1e-4));

    Stereo boosted = in;
    fx->reset();
    fx->setParameter(id_of(*fx, "Dry/Wet"), 1.0f);
    fx->setParameter(id_of(*fx, "Low-Mid"), 1.0f);
    run(*fx, boosted);
    CHECK(boosted.finite());
    CHECK(boosted.rms(4800) > in.rms(4800) * 1.2);
}

TEST_CASE("Compressor: full compression squashes the peaks and narrows a 20 dB step") {
    auto fx = prepared(EffectType::Compressor);
    // Loud for half a second, then 20 dB quieter for half a second.
    Stereo in = sine(1.0, 1.0, 0.9f);
    int half = in.frames() / 2;
    for (int i = half; i < in.frames(); ++i) {
        in.left[i] *= 0.1f;
        in.right[i] *= 0.1f;
    }
    double step_in = in.rms(9600, half) / in.rms(half + 9600);
    Stereo out = in;
    fx->setParameter(id_of(*fx, "Compress"), 1.0f);
    run(*fx, out);
    CHECK(out.finite());
    // Pressure6 is a fast, saturating compressor: it works within the cycle,
    // so the loud sine's crest factor drops well below sqrt(2) while the
    // level step between the halves closes by a couple of dB.
    double crest = out.peak() / out.rms(9600, half);
    CHECK(crest < 1.25);
    double step_out = out.rms(9600, half) / out.rms(half + 9600);
    CHECK(step_in == Catch::Approx(10.0).epsilon(0.01));
    CHECK(step_out < step_in * 0.85);
}

TEST_CASE("Limiter: a signal over full scale comes out under the threshold") {
    auto fx = prepared(EffectType::Limiter);
    Stereo in = sine(0.5, 0.5, 2.0f);
    Stereo out = in;
    fx->setParameter(id_of(*fx, "Threshold"), -3.0f);
    run(*fx, out);
    CHECK(out.finite());
    // Past the look-ahead and the attack, the ceiling holds (-3 dB = 0.708).
    Stereo settled(out.frames() - 4800);
    std::copy(out.left.begin() + 4800, out.left.end(), settled.left.begin());
    std::copy(out.right.begin() + 4800, out.right.end(), settled.right.begin());
    CHECK(settled.peak() < 0.75f);
    CHECK(settled.peak() > 0.5f);
}

TEST_CASE("Reverb: sound continues after the input stops") {
    auto fx = prepared(EffectType::Reverb);
    Stereo in = sine(0.25, 1.0, 0.5f);
    int stop = static_cast<int>(0.25 * kRate);
    CHECK(in.rms(stop + 4800) == 0.0);
    fx->setParameter(id_of(*fx, "Wetness"), 1.0f);
    fx->setParameter(id_of(*fx, "Sustain"), 0.8f);
    run(*fx, in);
    CHECK(in.finite());
    CHECK(in.rms(stop + 4800, stop + 14400) > 0.005);
}

TEST_CASE("Delay: a burst is repeated later") {
    auto fx = prepared(EffectType::Delay);
    Stereo in = sine(0.05, 1.5, 0.5f);
    int stop = static_cast<int>(0.05 * kRate);
    fx->setParameter(id_of(*fx, "Time"), 0.5f);
    fx->setParameter(id_of(*fx, "Regen"), 0.5f);
    fx->setParameter(id_of(*fx, "Dry/Wet"), 1.0f);
    run(*fx, in);
    CHECK(in.finite());
    // Silence immediately after the burst, an echo somewhere in the next second.
    CHECK(in.rms(stop + 480, stop + 960) < 0.01);
    CHECK(in.rms(stop + 960, in.frames()) > 0.01);
}

TEST_CASE("Distortion: driven hard, the waveform is no longer the sine") {
    auto fx = prepared(EffectType::Distortion);
    Stereo in = sine(0.5, 0.5, 0.5f);
    Stereo out = in;
    fx->setParameter(id_of(*fx, "Input"), 1.0f);
    run(*fx, out);
    CHECK(out.finite());
    double diff = 0.0;
    for (int i = 4800; i < in.frames(); ++i) diff += std::fabs(out.left[i] - in.left[i]);
    CHECK(diff / (in.frames() - 4800) > 0.05);
    CHECK(out.rms(4800) > 0.1);
}

// ---------------------------------------------------------------------------
// Through the engine

namespace {

struct Player {
    ProjectHistory history;
    engine::Engine player;
    explicit Player(Project project) : history(std::move(project)) {
        player.set_processor_factory(effects::make_effect);
    }
    void republish() { player.publish(history.read(), kRate); }
    void apply(Delta delta) { REQUIRE(history.apply(std::move(delta)) == ApplyResult::Applied); }
    Stereo render(int blocks) {
        Stereo out(blocks * kBlock);
        for (int b = 0; b < blocks; ++b) {
            float* channels[2] = {out.left.data() + b * kBlock, out.right.data() + b * kBlock};
            player.render(channels, 2, kBlock);
        }
        return out;
    }
};

}  // namespace

TEST_CASE("a master Reverb rings out after the last hit; bypassing it does not") {
    auto f = make_fixture();
    Player p(std::move(f.project));
    auto placed = add_placement(p.history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    p.apply(std::move(placed->delta));

    // The last hit is at beat 4 (1.5 s at 120 BPM) and lasts 0.1 s; sample
    // blocks 2.0 s .. 2.5 s for the tail.
    auto tail_rms = [&] {
        p.republish();
        p.player.seek(0);
        p.player.play();
        Stereo out = p.render(240);
        return out.rms(static_cast<int>(2.0 * kRate), static_cast<int>(2.5 * kRate));
    };
    CHECK(tail_rms() == Catch::Approx(0.0).margin(1e-9));

    auto added = add_effect(p.history.read(), ChainRef::master(), EffectType::Reverb);
    REQUIRE(added.ok());
    Id reverb = added->id;
    p.apply(std::move(added->delta));
    auto wet = set_effect_parameter(p.history.read(), reverb, "Sustain", 0.9f);
    REQUIRE(wet.ok());
    p.apply(std::move(*wet));
    CHECK(tail_rms() > 0.001);

    auto bypassed = set_effect_bypassed(p.history.read(), reverb, true);
    REQUIRE(bypassed.ok());
    p.apply(std::move(*bypassed));
    CHECK(tail_rms() == Catch::Approx(0.0).margin(1e-9));

    REQUIRE(p.history.undo());
    CHECK(tail_rms() > 0.001);
}

TEST_CASE("an Instrument's chain touches only that Instrument") {
    auto f = make_fixture();
    Player twin(f.project);
    Player p(std::move(f.project));
    for (Player* each : {&p, &twin}) {
        auto placed = add_placement(each->history.read(), f.arrangement, f.track_a,
                                    f.melody_pattern, 0);
        REQUIRE(placed.ok());
        each->apply(std::move(placed->delta));
        auto drums = add_placement(each->history.read(), f.arrangement, f.track_b,
                                   f.drum_pattern, 0);
        REQUIRE(drums.ok());
        each->apply(std::move(drums->delta));
    }

    // A Distortion on the drums leaves the first melody note (a lone Sustain
    // tone before the second drum hit) identical between the two engines.
    Id drum = p.history.read().instruments.items[0].id;
    auto added = add_effect(p.history.read(), ChainRef::of(drum), EffectType::Distortion);
    REQUIRE(added.ok());
    p.apply(std::move(added->delta));
    auto hot = set_effect_parameter(p.history.read(), added->id, "Input", 1.0f);
    REQUIRE(hot.ok());
    p.apply(std::move(*hot));

    for (Player* each : {&p, &twin}) {
        each->republish();
        each->player.seek(0);
        each->player.play();
    }
    // Half a second: 48 blocks of 512 frames at 48 kHz.
    Stereo a = p.render(48), b = twin.render(48);
    auto max_difference = [&](int from, int to) {
        float d = 0.0f;
        for (int i = from; i < to; ++i) d = std::max(d, std::fabs(a.left[i] - b.left[i]));
        return d;
    };
    // Frames 0.15 s .. 0.45 s: only the melody note is sounding. The drum
    // bus still runs through the Distortion, whose dither is far below one
    // float ulp of the note, so the two renders agree to rounding.
    CHECK(max_difference(7200, 21600) < 1e-5f);
    // Frames 0 .. 0.1 s: the first drum hit, distorted on one side only.
    CHECK(max_difference(0, 4800) > 0.1f);
}

TEST_CASE("a parameter change does not restart the Effect: the delay line survives the republish") {
    auto f = make_fixture();
    Player p(std::move(f.project));
    auto placed = add_placement(p.history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    p.apply(std::move(placed->delta));
    auto added = add_effect(p.history.read(), ChainRef::master(), EffectType::Delay);
    REQUIRE(added.ok());
    p.apply(std::move(added->delta));
    for (auto [name, value] : {std::pair{"Regen", 0.6f}, std::pair{"Time", 0.5f}}) {
        auto set = set_effect_parameter(p.history.read(), added->id, name, value);
        REQUIRE(set.ok());
        p.apply(std::move(*set));
    }
    p.republish();
    p.player.seek(0);
    p.player.play();
    p.render(24);  // the first hit is in the delay line
    p.player.stop();

    // Tweak while stopped: a fresh Processor would have an empty line and
    // produce silence; the kept one still echoes.
    auto tweak = set_effect_parameter(p.history.read(), added->id, "Freq", 0.7f);
    REQUIRE(tweak.ok());
    p.apply(std::move(*tweak));
    p.republish();
    Stereo after = p.render(96);
    CHECK(after.finite());
    CHECK(after.rms() > 0.001);
}

TEST_CASE("Effects that leave the Project are released, and churn stays finite") {
    auto f = make_fixture();
    Player p(std::move(f.project));
    auto placed = add_placement(p.history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    p.apply(std::move(placed->delta));
    p.republish();
    p.player.play();

    for (int round = 0; round < 40; ++round) {
        auto type = static_cast<EffectType>(round % kEffectTypeCount);
        auto added = add_effect(p.history.read(), ChainRef::master(), type);
        REQUIRE(added.ok());
        p.apply(std::move(added->delta));
        p.republish();
        CHECK(p.render(2).finite());
        auto removed = remove_effect(p.history.read(), added->id);
        REQUIRE(removed.ok());
        p.apply(std::move(*removed));
        p.republish();
        CHECK(p.render(2).finite());
    }
}

TEST_CASE("without a factory the chains are silent pass-throughs") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto added = add_effect(history.read(), ChainRef::master(), EffectType::Reverb);
    REQUIRE(added.ok());
    REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);
    auto model = engine::bake(history.read(), kRate);
    CHECK(model->master_chain.empty());
}
