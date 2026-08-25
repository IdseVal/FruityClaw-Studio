// The main window shell around the Arrangement view: transport bar with
// play/stop, position readout, tempo, the recording controls, undo/redo
// driven by the History, and the sidebar — Samples above Patterns — on the
// left.
#pragma once

#include <QMainWindow>

#include "core/generation.h"
#include "core/history.h"
#include "core/playback.h"

class QLabel;
class QToolButton;
class QAction;

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
               core::GenerationSettingsPort& generation, const QString& product_name,
               QWidget* parent = nullptr);

    // Opens the music-generation settings page; `guidance`, when given, tells
    // the user what they tried and why they landed here.
    void open_generation_settings(const QString& guidance = {});

    // What a surface that wants to generate does: with generation on it
    // proceeds, with it off the user is guided to the settings page rather
    // than shown a dead control (core document 6.4: gated, not absent).
    void request_generation();

private:
    void refresh_transport();
    void refresh_undo_redo();

    core::ProjectHistory& history_;
    core::TransportPort& transport_;
    core::GenerationSettingsPort& generation_;

    ArrangementView* arrangement_view_ = nullptr;
    PatternPalette* palette_ = nullptr;
    SampleBrowser* samples_ = nullptr;
    RecordBar* record_bar_ = nullptr;
    QToolButton* play_button_ = nullptr;
    QLabel* position_label_ = nullptr;
    QLabel* tempo_label_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* generate_action_ = nullptr;
};

}  // namespace ui
