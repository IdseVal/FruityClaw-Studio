// The AudioSession: the RecorderPort the Studio actually runs on, driven
// through a fake AudioDevice. What the Qt stub in test_ui.cpp assumes of the
// port — inputs ordered default-first, a refused input leaving playback
// running, a take trimmed by the device's measured latency — is pinned here
// against the real implementation.
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "app/audio_session.h"
#include "audioio/audio_device.h"
#include "engine/engine.h"
#include "engine/recorder.h"

using namespace audioio;

namespace {

constexpr int kBlock = 512;

// Records every seam call and lets a test push blocks through the callback
// as the audio thread would.
struct FakeDevice : AudioDevice {
    std::vector<DeviceInfo> devices;
    std::vector<StreamConfig> opens;
    int refuse_input = -1;   // open() fails when this input is asked for
    double only_rate = 0.0;  // when set, open() fails at any other rate
    StreamLatency latency{0.010, 0.020};
    bool opened = false;
    bool running = false;
    int stops = 0;
    int closes = 0;
    AudioCallback callback = nullptr;
    void* user_data = nullptr;
    StreamConfig current;

    std::vector<DeviceInfo> enumerate() override { return devices; }

    Result open(const StreamConfig& config, AudioCallback cb, void* ud) override {
        opens.push_back(config);
        if (!config.input_device.is_none() && config.input_device.value == refuse_input)
            return Result::failure("device busy");
        if (only_rate > 0.0 && config.sample_rate != only_rate)
            return Result::failure("rate unsupported");
        opened = true;
        callback = cb;
        user_data = ud;
        current = config;
        return Result::success();
    }
    Result start() override {
        running = true;
        return Result::success();
    }
    Result stop() override {
        ++stops;
        running = false;
        return Result::success();
    }
    void close() override {
        ++closes;
        opened = false;
    }
    StreamLatency measured_latency() const override { return latency; }

    // One callback tick with a constant stereo input, or output-only.
    void tick(bool with_input, int frames = kBlock) {
        REQUIRE(callback);
        auto n = static_cast<std::size_t>(frames);
        std::vector<float> in_l(n, 0.5f);
        std::vector<float> in_r(n, -0.5f);
        std::vector<float> out_l(n);
        std::vector<float> out_r(n);
        const float* in[2] = {in_l.data(), in_r.data()};
        float* out[2] = {out_l.data(), out_r.data()};
        int in_channels = with_input && !current.input_device.is_none() ? 2 : 0;
        callback(in_channels ? in : nullptr, in_channels, out, 2, frames, StreamTime{},
                 user_data);
    }
};

DeviceInfo make_device(int id, const char* name, int in, int out, bool default_in = false,
                       bool default_out = false) {
    DeviceInfo d;
    d.id = DeviceId{id};
    d.name = name;
    d.backend_name = "Fake";
    d.max_input_channels = in;
    d.max_output_channels = out;
    d.is_default_input = default_in;
    d.is_default_output = default_out;
    return d;
}

struct SessionFixture {
    FakeDevice device;
    engine::Engine player;
    engine::Recorder recorder;
    app::AudioSession session{&device, player, recorder};

    SessionFixture() {
        device.devices = {
            make_device(0, "Speakers", 0, 2, false, true),
            make_device(1, "Line In", 2, 0),
            make_device(2, "Mic", 1, 0, true),
            make_device(3, "Interface", 8, 8),
        };
    }
};

}  // namespace

TEST_CASE("AudioSession opens the default output alone, at 48 kHz first") {
    SessionFixture f;
    CHECK(f.session.open() == 48000.0);
    CHECK(f.session.sample_rate() == 48000.0);
    REQUIRE(f.device.opens.size() == 1);
    CHECK(f.device.opens[0].output_device == DeviceId{0});
    CHECK(f.device.opens[0].input_device.is_none());
    CHECK(f.device.running);
    CHECK(f.session.selected_input() == -1);
    CHECK_FALSE(f.session.status().input_open);
}

TEST_CASE("AudioSession falls back to 44.1 kHz when the device refuses 48") {
    SessionFixture f;
    f.device.only_rate = 44100.0;
    CHECK(f.session.open() == 44100.0);
    CHECK(f.device.opens.size() == 2);
    CHECK(f.device.closes == 0);  // a refused open leaves nothing to close
    CHECK(f.device.running);
}

TEST_CASE("AudioSession with no device or no output runs silent and offers no input") {
    engine::Engine player;
    engine::Recorder recorder;
    app::AudioSession silent(nullptr, player, recorder);
    CHECK(silent.open() == 0.0);
    CHECK(silent.sample_rate() == 48000.0);  // the Engine still bakes at a rate
    CHECK(silent.inputs().empty());
    CHECK_FALSE(silent.select_input(2).empty());
    CHECK_FALSE(silent.start_recording());
    CHECK(silent.stop_recording() == nullptr);

    SessionFixture f;
    f.device.devices = {make_device(1, "Line In", 2, 0)};  // an input, no output
    CHECK(f.session.open() == 0.0);
    CHECK(f.device.opens.empty());
    CHECK(f.session.inputs().empty());
}

TEST_CASE("AudioSession lists only devices with inputs, default first, capped at two channels") {
    SessionFixture f;
    REQUIRE(f.session.open() > 0.0);
    std::vector<core::InputInfo> inputs = f.session.inputs();
    REQUIRE(inputs.size() == 3);
    CHECK(inputs[0].id == 2);
    CHECK(inputs[0].name == "Mic");
    CHECK(inputs[0].backend_name == "Fake");
    CHECK(inputs[0].channels == 1);
    CHECK(inputs[1].id == 1);
    CHECK(inputs[1].channels == 2);
    CHECK(inputs[2].id == 3);
    CHECK(inputs[2].channels == 2);  // an 8-in interface records 1-2
}

TEST_CASE("selecting an input reopens the stream full duplex at the session rate") {
    SessionFixture f;
    f.device.only_rate = 44100.0;
    REQUIRE(f.session.open() == 44100.0);

    CHECK(f.session.select_input(2).empty());
    CHECK(f.session.selected_input() == 2);
    CHECK(f.device.stops == 1);
    CHECK(f.device.closes == 1);
    const StreamConfig& duplex = f.device.opens.back();
    CHECK(duplex.input_device == DeviceId{2});
    CHECK(duplex.output_device == DeviceId{0});
    CHECK(duplex.sample_rate == 44100.0);
    CHECK(f.device.running);
    CHECK(f.session.status().input_open);
    CHECK(f.session.status().sample_rate == 44100.0);

    // Picking the same input again is a no-op: no reopen.
    std::size_t opens = f.device.opens.size();
    CHECK(f.session.select_input(2).empty());
    CHECK(f.device.opens.size() == opens);

    // Releasing the input goes back to output-only.
    CHECK(f.session.select_input(-1).empty());
    CHECK(f.session.selected_input() == -1);
    CHECK(f.device.opens.back().input_device.is_none());
    CHECK_FALSE(f.session.status().input_open);
    CHECK(f.device.running);
}

TEST_CASE("an input that will not open is reported and playback keeps running output-only") {
    SessionFixture f;
    REQUIRE(f.session.open() > 0.0);
    REQUIRE(f.session.select_input(2).empty());

    f.device.refuse_input = 1;
    std::string error = f.session.select_input(1);
    CHECK(error == "Could not open that input: device busy");
    CHECK(f.session.selected_input() == -1);
    CHECK(f.device.running);
    CHECK(f.device.opens.back().input_device.is_none());
    CHECK_FALSE(f.session.status().input_open);
    CHECK_FALSE(f.session.start_recording());
}

TEST_CASE("a take through the session is trimmed by the measured round-trip latency") {
    SessionFixture f;
    REQUIRE(f.session.open() == 48000.0);
    CHECK_FALSE(f.session.start_recording());  // no input yet
    REQUIRE(f.session.select_input(1).empty());

    f.device.latency = StreamLatency{0.010, 0.020};  // 30 ms = 1440 frames
    REQUIRE(f.session.start_recording());
    CHECK_FALSE(f.session.start_recording());  // one take at a time
    CHECK(f.session.select_input(2) == "Stop recording before changing the input");
    CHECK(f.session.selected_input() == 1);

    const int blocks = 6;  // 3072 frames captured
    for (int b = 0; b < blocks; ++b) f.device.tick(true);

    core::RecorderStatus status = f.session.status();
    CHECK(status.recording);
    CHECK(status.input_open);
    CHECK(status.peak == 0.5f);
    CHECK(status.sample_rate == 48000.0);

    core::SampleSource take = f.session.stop_recording();
    REQUIRE(take);
    CHECK(take->sample_rate == 48000.0);
    CHECK(take->channels == 2);
    CHECK(take->frame_count() == blocks * kBlock - 1440);
    CHECK(take->frames[0] == 0.5f);
    CHECK(take->frames[1] == -0.5f);
    CHECK_FALSE(f.session.status().recording);
    CHECK(f.session.stop_recording() == nullptr);
}

TEST_CASE("an output-only callback meters nothing and records nothing") {
    SessionFixture f;
    REQUIRE(f.session.open() > 0.0);
    f.device.tick(false);
    CHECK(f.session.status().peak == 0.0f);
    CHECK_FALSE(f.session.start_recording());
    CHECK(f.session.stop_recording() == nullptr);
}

TEST_CASE("destroying the session stops the stream and any running take") {
    engine::Engine player;
    engine::Recorder recorder;
    FakeDevice device;
    device.devices = {make_device(0, "Speakers", 0, 2, false, true),
                      make_device(1, "Line In", 2, 0, true)};
    {
        app::AudioSession session(&device, player, recorder);
        REQUIRE(session.open() > 0.0);
        REQUIRE(session.select_input(1).empty());
        REQUIRE(session.start_recording());
        device.tick(true);
    }
    CHECK_FALSE(device.running);
    CHECK_FALSE(device.opened);
    CHECK_FALSE(recorder.status().recording);
}
