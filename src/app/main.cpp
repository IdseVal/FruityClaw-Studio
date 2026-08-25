// The composition root: the only place adapters are constructed and wired.
#include <QApplication>
#include <cstdio>
#include <memory>

#include "app/audio_session.h"
#include "app/demo_project.h"
#include "app/generation_file.h"
#include "audioio/portaudio_device.h"
#include "core/history.h"
#include "engine/engine.h"
#include "engine/recorder.h"
#include "ui/main_window.h"

int main(int argc, char** argv) {
    QApplication qt_app(argc, argv);

    core::ProjectHistory history(app::make_demo_project());
    engine::Engine player;
    engine::Recorder recorder;

    // A machine with no usable audio still gets a fully working editor — the
    // Studio stands on its own (core document 1.1a); it is just silent.
    std::unique_ptr<audioio::AudioDevice> device = audioio::make_portaudio_device();
    app::AudioSession session(device.get(), player, recorder);
    if (session.open() == 0.0) {
        std::fprintf(stderr, "No audio output available; running silent.\n");
    }

    player.publish(history.read(), session.sample_rate());
    history.observe([&history, &player, &session] {
        player.publish(history.read(), session.sample_rate());
    });

    // Music generation: off until the user chooses a model in settings (core
    // document 6.4). The choice and any key live in the per-user config dir.
    app::GenerationFile generation(app::GenerationFile::default_directory());

    ui::MainWindow window(history, player, player, session, generation,
                          FCS_PRODUCT_NAME_STR);
    window.show();

    int result = qt_app.exec();

    // The session's destructor stops the callback before the Engine and
    // Recorder it renders through go away; it is declared after both, so
    // that ordering holds by construction.
    return result;
}
