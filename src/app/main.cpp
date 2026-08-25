// The composition root: the only place adapters are constructed and wired.
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <cstdio>
#include <memory>

#include "app/audio_session.h"
#include "app/demo_project.h"
#include "app/generation_file.h"
#include "app/toggle_file.h"
#include "assistant/key_store.h"
#include "audioio/portaudio_device.h"
#include "core/history.h"
#include "engine/engine.h"
#include "engine/recorder.h"
#include "ui/assistant_key_dialog.h"
#include "ui/main_window.h"

int main(int argc, char** argv) {
    QApplication qt_app(argc, argv);
    // Config and data paths derive from the app id, never the product name.
    QCoreApplication::setApplicationName(FCS_APP_ID_STR);

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

    assistant::FunctionToggles toggles = app::load_toggles();
    // Music generation: off until the user chooses a model in settings (core
    // document 6.4). The choice and any key live in the per-user config dir.
    app::GenerationFile generation(app::GenerationFile::default_directory());
    ui::MainWindow window(history, player, player, session, toggles, generation,
                          FCS_PRODUCT_NAME_STR);
    QObject::connect(&window, &ui::MainWindow::toggles_changed,
                     [&toggles] { app::save_toggles(toggles); });

    // The Assistant key: offered once, on first open, over a Studio that is
    // already complete (core document 1.1a). A decline is never asked again;
    // the menu is how either answer gets changed later.
    assistant::FileKeyStore keys(assistant::FileKeyStore::default_directory());
    QAction* key_action = window.menuBar()->addMenu("&Studio")->addAction("&Assistant key...");
    QObject::connect(key_action, &QAction::triggered, &window, [&keys, &window] {
        ui::AssistantKeyDialog(keys, &window).exec();
    });
    window.show();
    if (!keys.offer_made()) {
        QTimer::singleShot(0, &window, [key_action] { key_action->trigger(); });
    }

    int result = qt_app.exec();

    // The session's destructor stops the callback before the Engine and
    // Recorder it renders through go away; it is declared after both, so
    // that ordering holds by construction.
    return result;
}
