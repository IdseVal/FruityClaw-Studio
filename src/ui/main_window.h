// The main window shell around the Arrangement view: transport bar with
// play/stop, position readout, tempo, the recording controls, undo/redo
// driven by the History, and the sidebar — Samples above Patterns — on the
// left.
#pragma once

#include <QMainWindow>

#include "core/history.h"
#include "core/playback.h"

class QLabel;
class QToolButton;

namespace ui {

class ArrangementView;
class PatternPalette;
class RecordBar;
class SampleBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(core::ProjectHistory& history, core::TransportPort& transport,
               core::AuditionPort& audition, core::RecorderPort& recorder,
               const QString& product_name, QWidget* parent = nullptr);

private:
    void refresh_transport();
    void refresh_undo_redo();

    core::ProjectHistory& history_;
    core::TransportPort& transport_;

    ArrangementView* arrangement_view_ = nullptr;
    PatternPalette* palette_ = nullptr;
    SampleBrowser* samples_ = nullptr;
    RecordBar* record_bar_ = nullptr;
    QToolButton* play_button_ = nullptr;
    QLabel* position_label_ = nullptr;
    QLabel* tempo_label_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
};

}  // namespace ui
