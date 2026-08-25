// The recording controls in the transport bar (core document section 3.7:
// recording from audio inputs): an input picker, an input level meter, a
// record toggle and the running length of the take.
//
// Stopping a take adds it to the Project as a Sample through the
// sample_functions catalogue, so the take is one undoable step and shows up
// in the Sample sidebar like any other Sample. Its provenance is Human — a
// recording is the user's own work (section 6.3 applies to AI content only).
#pragma once

#include <QWidget>

#include "core/history.h"
#include "core/playback.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QToolButton;

namespace ui {

class RecordBar : public QWidget {
    Q_OBJECT

public:
    RecordBar(core::ProjectHistory& history, core::RecorderPort& recorder,
              QWidget* parent = nullptr);

signals:
    // A one-line contextual hint for the status bar.
    void hint_changed(const QString& hint);

private:
    void reload_inputs();
    void choose_input(int index);
    void toggle_recording();
    void finish_take();
    void poll();
    std::string next_take_name() const;

    core::ProjectHistory& history_;
    core::RecorderPort& recorder_;

    QComboBox* inputs_ = nullptr;
    QProgressBar* meter_ = nullptr;
    QToolButton* record_button_ = nullptr;
    QLabel* length_label_ = nullptr;
};

}  // namespace ui
