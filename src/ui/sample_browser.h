// The Sample sidebar: the Project's Samples, one row each, browsable and
// auditionable (core document section 3.7). Clicking a row plays it through
// the AuditionPort; "Place in Pattern" adds it to a Pattern as a new lane
// through the sample_functions catalogue, so the placement is one undoable
// step the Assistant could equally have taken. Opening a row shows the
// Sample's details.
//
// AI-generated Samples carry the section 6.3 provenance mark: a small badge
// in the corner of the row's waveform, and a full provenance line when the
// Sample is opened. The mark is derived from Provenance, never stored here.
#pragma once

#include <QWidget>
#include <optional>

#include "core/history.h"
#include "core/playback.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QToolButton;

namespace ui {

class SampleBrowser : public QWidget {
    Q_OBJECT

public:
    SampleBrowser(core::ProjectHistory& history, core::AuditionPort& audition,
                  QWidget* parent = nullptr);

    // The Sample of the selected row, if any.
    std::optional<core::Id> selected() const;

    // Adds the selected Sample to `pattern` as a new lane, creating a Sampler
    // Instrument for it when the Project has none. A Sample already in that
    // Pattern is reported as a hint; nothing is recorded.
    void place_selected_in(core::Id pattern);

signals:
    // A one-line contextual hint for the status bar.
    void hint_changed(const QString& hint);

private:
    void reload();
    void audition_row(QListWidgetItem* item);
    void open_row(QListWidgetItem* item);
    void rebuild_place_menu();
    const core::Sample* sample_at(QListWidgetItem* item) const;

    core::ProjectHistory& history_;
    core::AuditionPort& audition_;

    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    QToolButton* place_button_ = nullptr;
    QMenu* place_menu_ = nullptr;
};

}  // namespace ui
