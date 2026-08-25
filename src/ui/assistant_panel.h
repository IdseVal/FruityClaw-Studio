// The Assistant panel: where the user prompts the Assistant and where the
// result lands — in the Studio, as undoable changes. The transcript shows
// the user's words and the Assistant's prose only; which Function ran is
// never shown (core document NON-scope 6). When a Selector matches several
// entities the panel asks the user to pick, locally (O-16.2).
//
// With no provider configured the panel says a key is required and offers
// nothing else: the Studio stands on its own (core document 1.1a).
//
// The request runs on a worker thread against a snapshot of the Project;
// the Deltas it returns are applied through ProjectHistory on the UI thread,
// in order, one History entry per Function.
#pragma once

#include <QWidget>
#include <functional>
#include <optional>
#include <thread>

#include "assistant/session.h"
#include "core/history.h"

class QLineEdit;
class QTextEdit;
class QToolButton;

namespace ui {

class AssistantPanel : public QWidget {
    Q_OBJECT

public:
    // `session` may be null: no provider is configured. `focus` is read at
    // the start of each turn.
    AssistantPanel(core::ProjectHistory& history, assistant::AssistantSession* session,
                   std::function<assistant::Focus()> focus, QWidget* parent = nullptr);
    ~AssistantPanel() override;

    // The switchboard (#17) writes these; they bind at the next turn start.
    void set_toggles(assistant::Toggles toggles) { toggles_ = std::move(toggles); }

    // True while a request is in flight or a choice is pending.
    bool busy() const { return busy_; }

    QLineEdit* input() const { return input_; }
    QTextEdit* transcript() const { return transcript_; }

private:
    void send();
    void finish(assistant::TurnOutcome outcome);
    void ask(const assistant::PendingChoice& pending);
    void choose(std::size_t index);
    void say(const QString& who, const QString& text);
    void set_busy(bool busy);

    core::ProjectHistory& history_;
    assistant::AssistantSession* session_;
    std::function<assistant::Focus()> focus_;
    assistant::Toggles toggles_;

    QTextEdit* transcript_ = nullptr;
    QWidget* choices_ = nullptr;
    QLineEdit* input_ = nullptr;
    QToolButton* send_button_ = nullptr;

    std::optional<assistant::PendingChoice> pending_;
    std::thread worker_;
    bool busy_ = false;
};

}  // namespace ui
