// The step sequencer: the surface for building drum Patterns (core document
// section 3.7), FL Studio in workflow, not in look. One row per Part of the
// Pattern being edited, sixteen steps per bar; left-click draws a step,
// right-click erases, a drag paints along the row, Shift-drag sets a step's
// velocity, the dot on the row header mutes the lane. Lanes come from the
// Sample sidebar ("Place in Pattern").
//
// Reads the Project through ProjectHistory::read(); every mutation goes
// through core/pattern_functions.h and lands as one Delta, so the surface
// cannot make a change the Assistant could not, and cannot make one that
// undo cannot take back. A drag is one gesture and one undo entry.
//
// The step grid is a property of this surface, not of the data
// (project-data-model spec section 3.4): a step is an Event on the sixteenth
// grid. Events off the grid are drawn as ticks and left alone.
#pragma once

#include <QWidget>
#include <optional>
#include <vector>

#include "core/history.h"
#include "core/pattern_functions.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace ui {

// The grid alone: rows of steps for one Pattern.
class StepGrid : public QWidget {
    Q_OBJECT

public:
    explicit StepGrid(core::ProjectHistory& history, QWidget* parent = nullptr);

    void set_pattern(std::optional<core::Id> pattern);
    std::optional<core::Id> pattern() const { return pattern_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    // A one-line contextual hint for the status bar.
    void hint_changed(const QString& hint);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Cell {
        int lane;
        int step;
    };
    enum class Drag { None, Draw, Velocity };

    const core::Pattern* current() const;
    int step_count() const;
    int cell_width() const;
    QRect cell_rect(int lane, int step) const;
    QRect header_rect(int lane) const;
    QRect mute_dot_rect(int lane) const;
    std::optional<Cell> cell_at(const QPoint& pos) const;
    int lane_at(int y) const;
    QString lane_name(const core::Part& part) const;

    void paint_lane(QPainter& painter, int index, const core::Part& part);
    void draw_step(int lane, int step, bool on);
    void set_velocity(int lane, int step, core::Velocity velocity);
    void apply_or_hint(core::functions::Expected<core::Delta> result);

    core::ProjectHistory& history_;
    std::optional<core::Id> pattern_;

    Drag drag_ = Drag::None;
    int drag_lane_ = -1;
    bool draw_on_ = true;             // the state a Draw drag paints
    int velocity_step_ = -1;          // the step a Velocity drag edits
    core::Velocity velocity_start_ = 0;
    QPoint drag_origin_;
};

// The panel: which Pattern is open, its name, its provenance, a way to make
// a new one, and the grid.
class StepSequencer : public QWidget {
    Q_OBJECT

public:
    explicit StepSequencer(core::ProjectHistory& history, QWidget* parent = nullptr);

    // Opens a Pattern in the grid; nullopt opens none. A Pattern that is not
    // in the Project opens none.
    void set_pattern(std::optional<core::Id> pattern);
    std::optional<core::Id> pattern() const { return grid_->pattern(); }
    StepGrid* grid() const { return grid_; }

signals:
    void hint_changed(const QString& hint);

private:
    void reload();
    void add_pattern();
    void commit_rename();

    core::ProjectHistory& history_;
    QComboBox* picker_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* badge_ = nullptr;  // the section 6.3 mark on an AI-generated Pattern
    QToolButton* add_ = nullptr;
    StepGrid* grid_ = nullptr;
};

}  // namespace ui
