// The Studio's settings, one tab per concern. Settings are permanently
// outside the Assistant's reach (core document 9.1): nothing here is
// observable from a Function. The Assistant Functions tab is the
// switchboard; Music generation is the section 6.4 page. Later tabs (audio
// device, provider key) arrive with their own issues.
#pragma once

#include <QDialog>

class QTabWidget;

#include "assistant/function_file.h"
#include "core/generation.h"

namespace ui {

class FunctionSwitchboard;
class GenerationSettingsPage;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(assistant::FunctionToggles& toggles, assistant::Capabilities capabilities,
                   core::GenerationSettingsPort& generation, QWidget* parent = nullptr);

    FunctionSwitchboard* switchboard() const { return switchboard_; }

    // Brings the Music generation tab to the front with the page's guidance
    // band: what the user tried, and that turning generation on is the way.
    void show_generation(const QString& guidance);

signals:
    // Relayed from the switchboard, for the composition root to persist.
    void toggles_changed();
    // Generation was turned on or off; the page already persisted it.
    void generation_changed();

private:
    QTabWidget* tabs_ = nullptr;
    FunctionSwitchboard* switchboard_ = nullptr;
    GenerationSettingsPage* generation_ = nullptr;
};

}  // namespace ui
