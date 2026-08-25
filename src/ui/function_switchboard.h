// The Function switchboard (core document 3.10): one row per registry
// entry, labelled Directive or Rework, with a switch that removes the
// Function from what the Assistant is offered. The user's own editing is
// never touched by any switch here — the toggle is Assistant-only.
//
// The rows are a projection of assistant::registry() (obligation O-17.1);
// the banner is derived from the Function file the builder produces for
// the current toggles, not from the toggles themselves (O-17.3), so it
// cannot say something other than what would go on the wire.
#pragma once

#include <QWidget>
#include <vector>

#include "assistant/function_file.h"

class QCheckBox;
class QFrame;
class QLabel;

namespace ui {

class FunctionSwitchboard : public QWidget {
    Q_OBJECT

public:
    FunctionSwitchboard(assistant::FunctionToggles& toggles,
                        assistant::Capabilities capabilities, QWidget* parent = nullptr);

    // The file the Assistant would be offered at the next turn start.
    const assistant::FunctionFile& file() const { return file_; }

signals:
    // A toggle was written. The composition root persists the store.
    void changed();

private:
    void refresh();

    assistant::FunctionToggles& toggles_;
    assistant::Capabilities capabilities_;
    assistant::FunctionFile file_;

    QFrame* banner_ = nullptr;
    QLabel* banner_dot_ = nullptr;
    QLabel* banner_title_ = nullptr;
    QLabel* banner_detail_ = nullptr;
    std::vector<QCheckBox*> switches_;  // registry order
};

}  // namespace ui
