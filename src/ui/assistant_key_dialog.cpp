#include "ui/assistant_key_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme.h"

namespace ui {

AssistantKeyDialog::AssistantKeyDialog(core::AssistantKeyPort& keys, QWidget* parent)
    : QDialog(parent), keys_(keys) {
    setWindowTitle("Assistant");
    setModal(true);
    setMinimumWidth(520);
    setStyleSheet(
        QString("QDialog { background: %1; }"
                "QLabel { color: %2; font-size: 13px; }"
                "QLineEdit { background: %3; color: %2; border: 1px solid %4;"
                " padding: 8px 10px; font-family: Consolas, monospace; font-size: 13px; }"
                "QLineEdit:focus { border-color: %5; }"
                "QPushButton { color: %2; background: transparent; border: 1px solid %4;"
                " padding: 8px 16px; font-size: 13px; }"
                "QPushButton:hover { border-color: %5; color: %5; }"
                "QPushButton:disabled { color: %6; border-color: %3; }")
            .arg(theme::kPanel.name(), theme::kTextPrimary.name(), theme::kCanvas.name(),
                 theme::kGridBar.name(), theme::kAccent.name(), theme::kTextSecondary.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto* eyebrow = new QLabel("ASSISTANT", this);
    eyebrow->setStyleSheet(QString("color: %1; font-size: 10px; letter-spacing: 1px;")
                               .arg(theme::kTextSecondary.name()));
    layout->addWidget(eyebrow);

    // The decline path is the headline, not the fine print.
    auto* heading = new QLabel("Everything works. The Assistant is optional.", this);
    heading->setStyleSheet("font-size: 18px; font-weight: 600;");
    heading->setWordWrap(true);
    layout->addWidget(heading);

    auto* body = new QLabel(
        "Editing, Patterns, Effects, Samples and recording are ready now, with nothing "
        "to set up.\n\n"
        "The Assistant is the one part that needs something from you: a private API key "
        "from an AI provider, which it uses to run. Without a key the Assistant stays off "
        "and nothing else changes.",
        this);
    body->setWordWrap(true);
    layout->addWidget(body);
    layout->addSpacing(6);

    key_field_ = new QLineEdit(this);
    key_field_->setObjectName("assistant_key_field");
    key_field_->setEchoMode(QLineEdit::Password);
    key_field_->setPlaceholderText("Paste your API key");
    key_field_->setAccessibleName("API key");
    layout->addWidget(key_field_);

    // Reached again from the Studio menu: say that a key is already kept, so
    // the user is not left wondering whether the first save took.
    const bool has_key = keys_.has_key();
    if (has_key) {
        auto* saved = new QLabel("A key is saved. Paste a new one to replace it.", this);
        saved->setObjectName("assistant_key_saved");
        saved->setStyleSheet(QString("color: %1;").arg(theme::kAccent.name()));
        layout->addWidget(saved);
    }

    auto* note = new QLabel(
        "Kept on this machine for your account only. Never shown again, never written "
        "to a log. Add or change it later from Studio ▸ Assistant key.",
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QString("color: %1; font-size: 11px;").arg(theme::kTextSecondary.name()));
    layout->addWidget(note);

    problem_label_ = new QLabel(this);
    problem_label_->setObjectName("assistant_key_problem");
    problem_label_->setWordWrap(true);
    problem_label_->setStyleSheet(QString("color: %1;").arg(theme::kAccent.name()));
    problem_label_->hide();
    layout->addWidget(problem_label_);
    layout->addSpacing(8);

    // Two answers of equal weight: declining is first-class (core document
    // 1.1a), so it gets a real button, not a link in the corner.
    auto* buttons = new QHBoxLayout;
    auto* decline = new QPushButton(has_key ? "Keep the saved key" : "Continue without a key", this);
    decline->setObjectName("assistant_key_decline");
    decline->setAutoDefault(false);
    connect(decline, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(decline);
    buttons->addStretch(1);

    save_button_ = new QPushButton("Save key", this);
    save_button_->setObjectName("assistant_key_save");
    save_button_->setEnabled(false);
    save_button_->setDefault(true);
    connect(save_button_, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(save_button_);
    layout->addLayout(buttons);

    connect(key_field_, &QLineEdit::textChanged, this, [this](const QString& text) {
        save_button_->setEnabled(!text.trimmed().isEmpty());
        problem_label_->hide();
    });
    key_field_->setFocus();
}

void AssistantKeyDialog::accept() {
    // Pasted keys arrive with newlines and spaces around them.
    std::string key = key_field_->text().trimmed().toStdString();
    if (key.empty()) return;
    if (!keys_.store_key(key)) {
        problem_label_->setText(
            "The key could not be saved. Check that your user settings folder is writable, "
            "or continue without a key and try again later.");
        problem_label_->show();
        return;
    }
    key_field_->clear();
    QDialog::accept();
}

void AssistantKeyDialog::done(int result) {
    keys_.record_offer();
    QDialog::done(result);
}

}  // namespace ui
