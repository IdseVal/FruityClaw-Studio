// The main window shell around the Arrangement view: transport bar with
// play/stop, position readout, tempo, the recording controls, undo/redo
// driven by the History, and the sidebar — Samples above Patterns — on the
// left.
#pragma once

#include <QMainWindow>

#include "assistant/function_file.h"
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
               assistant::FunctionToggles& toggles,
               core::GenerationSettingsPort& generation, const QString& product_name,
               QWidget* parent = nullptr);

    // Opens Settings. `guidance`, when given, opens on the music-generation
    // tab and tells the user what they tried and why they landed here.
    void open_settings(const QString& guidance = {});

    // What a surface that wants to generate does: with generation on it
    // proceeds, with it off the user is guided to the settings page rather
    // than shown a dead control (core document 6.4: gated, not absent).
    void request_generation();

signals:
    // A Function toggle was written; the composition root persists the store.
    void toggles_changed();

private:
    void refresh_transport();
    void refresh_undo_redo();
    // What the Assistant may be offered, derived from the generation store.
    assistant::Capabilities capabilities() const;

    core::ProjectHistory& history_;
    core::TransportPort& transport_;
    assistant::FunctionToggles& toggles_;
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
