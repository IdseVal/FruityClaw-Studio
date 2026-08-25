#include "ui/assistant_panel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/theme.h"

namespace ui {

AssistantPanel::AssistantPanel(core::ProjectHistory& history, assistant::AssistantSession* session,
                               std::function<assistant::Focus()> focus, QWidget* parent)
    : QWidget(parent), history_(history), session_(session), focus_(std::move(focus)) {
    setStyleSheet(QString("QWidget { background: %1; color: %2; }"
                          "QTextEdit { border: none; padding: 6px; font-size: 13px; }"
                          "QLineEdit { background: %3; border: 1px solid %4; border-radius: 4px;"
                          " padding: 6px 8px; font-size: 13px; }"
                          "QLineEdit:focus { border-color: %5; }"
                          "QToolButton { border: 1px solid %4; border-radius: 4px;"
                          " padding: 5px 10px; }"
                          "QToolButton:hover { border-color: %5; color: %5; }"
                          "QToolButton:disabled { color: %6; }")
                      .arg(theme::kPanel.name(), theme::kTextPrimary.name(),
                           theme::kCanvas.name(), theme::kGridBar.name(), theme::kAccent.name(),
                           theme::kTextSecondary.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    transcript_ = new QTextEdit(this);
    transcript_->setReadOnly(true);
    transcript_->setFrameStyle(0);
    layout->addWidget(transcript_, 1);

    // The disambiguation strip: hidden until a name matches several things.
    choices_ = new QWidget(this);
    auto* choices_layout = new QVBoxLayout(choices_);
    choices_layout->setContentsMargins(8, 0, 8, 0);
    choices_layout->setSpacing(4);
    choices_->hide();
    layout->addWidget(choices_);

    auto* row = new QWidget(this);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(8, 0, 8, 8);
    row_layout->setSpacing(6);
    input_ = new QLineEdit(row);
    input_->setPlaceholderText("Ask for a Pattern, a Sample, an Instrument...");
    send_button_ = new QToolButton(row);
    send_button_->setText("Send");
    row_layout->addWidget(input_, 1);
    row_layout->addWidget(send_button_);
    layout->addWidget(row);

    connect(input_, &QLineEdit::returnPressed, this, &AssistantPanel::send);
    connect(send_button_, &QToolButton::clicked, this, &AssistantPanel::send);

    if (!session_) {
        say("Assistant",
            "An API key is required for the Assistant. The rest of the Studio works without one.");
        input_->setEnabled(false);
        send_button_->setEnabled(false);
    }
}

AssistantPanel::~AssistantPanel() {
    if (worker_.joinable()) worker_.join();
}

void AssistantPanel::send() {
    QString prompt = input_->text().trimmed();
    if (prompt.isEmpty() || !session_ || busy_) return;
    input_->clear();
    say("You", prompt);
    set_busy(true);

    // The request blocks; it runs off the UI thread against a snapshot, and
    // its Deltas come back here to be applied in order.
    if (worker_.joinable()) worker_.join();
    core::Project snapshot = history_.read();
    assistant::Focus focus = focus_ ? focus_() : assistant::Focus{};
    assistant::Toggles toggles = toggles_;
    worker_ = std::thread([this, prompt = prompt.toStdString(), snapshot = std::move(snapshot),
                           toggles = std::move(toggles), focus] {
        assistant::TurnOutcome outcome = session_->run_turn(prompt, snapshot, toggles, focus);
        QMetaObject::invokeMethod(
            this, [this, outcome = std::move(outcome)]() mutable { finish(std::move(outcome)); },
            Qt::QueuedConnection);
    });
}

void AssistantPanel::finish(assistant::TurnOutcome outcome) {
    for (const core::Delta& delta : outcome.deltas) {
        if (history_.apply(delta) == core::ApplyResult::Failed) {
            say("Assistant", "That change could not be made; the Project changed meanwhile.");
        }
    }
    if (outcome.pending) {
        ask(*outcome.pending);
        return;
    }
    if (!outcome.text.empty()) say("Assistant", QString::fromStdString(outcome.text));
    set_busy(false);
}

void AssistantPanel::ask(const assistant::PendingChoice& pending) {
    pending_ = pending;
    say("Assistant", QString::fromStdString(pending.question));

    auto* layout = choices_->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (std::size_t i = 0; i < pending.candidates.size(); ++i) {
        auto* button = new QToolButton(choices_);
        button->setText(QString::fromStdString(pending.candidates[i].name));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(button, &QToolButton::clicked, this, [this, i] { choose(i); });
        layout->addWidget(button);
    }
    choices_->show();
}

void AssistantPanel::choose(std::size_t index) {
    if (!pending_ || index >= pending_->candidates.size()) return;
    assistant::PendingChoice pending = std::move(*pending_);
    pending_.reset();
    choices_->hide();
    say("You", QString::fromStdString(pending.candidates[index].name));
    core::Id chosen = pending.candidates[index].id;
    // Local: no request is made, so this is quick enough for the UI thread.
    finish(session_->resume(std::move(pending), chosen, history_.read()));
}

void AssistantPanel::say(const QString& who, const QString& text) {
    QColor colour = who == "You" ? theme::kAccent : theme::kTextSecondary;
    transcript_->append(QString("<span style='color:%1; font-size:10px; letter-spacing:1px;'>%2"
                                "</span><br>%3<br>")
                            .arg(colour.name(), who.toUpper(), text.toHtmlEscaped()));
}

void AssistantPanel::set_busy(bool busy) {
    busy_ = busy;
    input_->setEnabled(!busy);
    send_button_->setEnabled(!busy);
    if (!busy) input_->setFocus();
}

}  // namespace ui
