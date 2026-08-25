// The first-open offer (core document 1.1a): paste a private key so the
// Assistant can work, or continue without one. Both answers are complete.
// Shown once by the composition root; reachable again from the Studio menu
// so a user who declined can change their mind without being asked.
#pragma once

#include <QDialog>

#include "core/assistant_key.h"

class QLabel;
class QLineEdit;
class QPushButton;

namespace ui {

class AssistantKeyDialog : public QDialog {
    Q_OBJECT

public:
    explicit AssistantKeyDialog(core::AssistantKeyPort& keys, QWidget* parent = nullptr);

    // Stores the key. Stays open, saying why, if the key could not be kept.
    void accept() override;

protected:
    // Either answer is the offer being made (core document 1.1a: no nag, no
    // second prompt), so it is recorded here rather than by the caller.
    void done(int result) override;

private:
    core::AssistantKeyPort& keys_;
    QLineEdit* key_field_ = nullptr;
    QPushButton* save_button_ = nullptr;
    QLabel* problem_label_ = nullptr;
};

}  // namespace ui
