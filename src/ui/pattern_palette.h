// The Pattern palette: the Project's Patterns, one row each, with the colour
// they carry onto the Arrangement. Clicking a row arms it for placing;
// clicking it again disarms. Pattern editing itself belongs to the step
// sequencer and piano roll (issues #9 and #10), not here.
#pragma once

#include <QListWidget>
#include <optional>

#include "core/history.h"

namespace ui {

class PatternPalette : public QListWidget {
    Q_OBJECT

public:
    explicit PatternPalette(core::ProjectHistory& history, QWidget* parent = nullptr);

    std::optional<core::Id> armed() const { return armed_; }

signals:
    void armed_changed();

private:
    void reload();
    void toggle_row(int row);

    core::ProjectHistory& history_;
    std::optional<core::Id> armed_;
};

}  // namespace ui
