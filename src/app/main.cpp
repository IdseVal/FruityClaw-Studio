// The composition root: the only place adapters are constructed and wired.
#include <QApplication>
#include <QMessageBox>
#include <cstdio>
#include <memory>

#include "app/demo_project.h"
#include "audioio/portaudio_device.h"
#include "core/history.h"
#include "effects/stock_effects.h"
#include "engine/engine.h"
#include "ui/main_window.h"

namespace {

void audio_callback(const float* const*, int, float* const* output, int output_channels,
                    int frames, const audioio::StreamTime&, void* user_data) {
    static_cast<engine::Engine*>(user_data)->render(output, output_channels, frames);
}

// Opens the default output. A machine with no usable audio still gets a fully
// working editor — the Studio stands on its own (core document 1.1a); it is
// just silent.
double open_audio(audioio::AudioDevice* device, engine::Engine& player) {
    if (!device) return 0.0;
    audioio::DeviceId output;
    for (const audioio::DeviceInfo& info : device->enumerate()) {
        if (info.is_default_output) {
            output = info.id;
            break;
        }
    }
    if (output.is_none()) return 0.0;

    for (double rate : {48000.0, 44100.0}) {
        audioio::StreamConfig config;
        config.output_device = output;
        config.sample_rate = rate;
        config.buffer_frames = 512;
        audioio::Result opened = device->open(config, &audio_callback, &player);
        if (!opened.ok) continue;
        audioio::Result started = device->start();
        if (started.ok) return rate;
        device->close();
    }
    return 0.0;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication qt_app(argc, argv);

    core::ProjectHistory history(app::make_demo_project());
    engine::Engine player;
    player.set_processor_factory(effects::make_effect);

    std::unique_ptr<audioio::AudioDevice> device = audioio::make_portaudio_device();
    double sample_rate = open_audio(device.get(), player);
    if (sample_rate == 0.0) {
        std::fprintf(stderr, "No audio output available; running silent.\n");
        sample_rate = 48000.0;  // the engine still bakes, for a later device
    }

    player.publish(history.read(), sample_rate);
    history.observe([&history, &player, sample_rate] {
        player.publish(history.read(), sample_rate);
    });

    ui::MainWindow window(history, player, player, FCS_PRODUCT_NAME_STR);
    window.show();

    int result = qt_app.exec();

    // Stop the callback before the Engine it renders through goes away.
    if (device) {
        device->stop();
        device->close();
    }
    return result;
}
