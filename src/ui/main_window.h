// The main window shell around the Arrangement view: transport bar with
// play/stop, position readout, tempo, undo/redo driven by the History, and
// the sidebar — Samples above Patterns — on the left, and the Assistant
// panel on the right.
#pragma once

#include <QMainWindow>

#include "assistant/session.h"
#include "core/history.h"
#include "core/playback.h"

class QLabel;
class QToolButton;

namespace ui {

class ArrangementView;
class AssistantPanel;
class PatternPalette;
class SampleBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // `session` may be null: no provider is configured (core document 1.1a).
    MainWindow(core::ProjectHistory& history, core::TransportPort& transport,
               core::AuditionPort& audition, assistant::AssistantSession* session,
               const QString& product_name, QWidget* parent = nullptr);

    AssistantPanel* assistant() const { return assistant_; }

private:
    void refresh_transport();
    void refresh_undo_redo();

    core::ProjectHistory& history_;
    core::TransportPort& transport_;

    ArrangementView* arrangement_view_ = nullptr;
    PatternPalette* palette_ = nullptr;
    SampleBrowser* samples_ = nullptr;
    AssistantPanel* assistant_ = nullptr;
    QToolButton* play_button_ = nullptr;
    QLabel* position_label_ = nullptr;
    QLabel* tempo_label_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
};

}  // namespace ui
