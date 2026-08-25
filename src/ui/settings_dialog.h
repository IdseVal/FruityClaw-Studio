// The Studio's settings, one tab per concern. Settings are permanently
// outside the Assistant's reach (core document 9.1): nothing here is
// observable from a Function. The Assistant Functions tab is the
// switchboard; later tabs (audio device, provider key, generation model)
// arrive with their own issues.
#pragma once

#include <QDialog>

#include "assistant/function_file.h"

namespace ui {

class FunctionSwitchboard;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(assistant::FunctionToggles& toggles, assistant::Capabilities capabilities,
                   QWidget* parent = nullptr);

    FunctionSwitchboard* switchboard() const { return switchboard_; }

signals:
    // Relayed from the switchboard, for the composition root to persist.
    void toggles_changed();

private:
    FunctionSwitchboard* switchboard_ = nullptr;
};

}  // namespace ui
