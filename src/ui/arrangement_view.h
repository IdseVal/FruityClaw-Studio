// The Arrangement view: the timeline where Patterns are assembled into the
// finished piece (core document section 3.7).
//
// Reads the Project through ProjectHistory::read() and the transport through
// the TransportPort seam; every mutation goes through the single-purpose
// functions of core/arrangement_functions.h and lands as one Delta on the
// history, so the view cannot make a change the Assistant could not, and
// cannot make one that undo cannot take back. Drags preview locally and
// commit one Delta on release.
#pragma once

#include <QWidget>
#include <optional>

#include "core/arrangement_functions.h"
#include "core/history.h"
#include "core/playback.h"

class QLineEdit;
class QScrollBar;

namespace ui {

class ArrangementView : public QWidget {
    Q_OBJECT

public:
    ArrangementView(core::ProjectHistory& history, core::TransportPort& transport,
                    QWidget* parent = nullptr);

    // The Pattern the palette has armed for placing; nullopt places nothing.
    void set_armed_pattern(std::optional<core::Id> pattern);

signals:
    // A one-line contextual hint for the status bar.
    void hint_changed(const QString& hint);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct PlacementRef {
        core::Id track;
        core::Id placement;
        bool operator==(const PlacementRef&) const = default;
    };

    enum class DragMode { None, Move, Resize };

    // --- geometry ----------------------------------------------------------
    const core::Arrangement* arrangement() const;
    int tick_to_x(core::Ticks tick) const;
    core::Ticks x_to_tick(int x) const;
    core::Ticks snap(core::Ticks tick, core::Ticks grid) const;
    QRect placement_rect(int track_index, const core::Placement& placement) const;
    QRect track_header_rect(int track_index) const;
    QRect add_track_rect() const;
    int track_index_at(int y) const;
    std::optional<PlacementRef> placement_at(const QPoint& pos) const;
    core::Ticks beat_ticks() const;
    core::Ticks bar_ticks() const;
    void update_scrollbar();

    // --- painting ----------------------------------------------------------
    void paint_grid(QPainter& painter, int content_height);
    void paint_ruler(QPainter& painter);
    void paint_tracks(QPainter& painter);
    void paint_placement(QPainter& painter, const core::Pattern* pattern,
                         const QRect& rect, const QColor& colour, bool selected,
                         bool dimmed, core::Ticks placement_length);
    void paint_playhead(QPainter& painter, int content_height);

    // --- mutations ---------------------------------------------------------
    void apply_or_hint(core::functions::Expected<core::Delta> result);
    void begin_rename(int track_index);
    void commit_rename();

    core::ProjectHistory& history_;
    core::TransportPort& transport_;

    double px_per_beat_ = 28.0;
    int scroll_x_ = 0;  // pixels into the timeline
    QScrollBar* hscroll_ = nullptr;

    std::optional<core::Id> armed_pattern_;
    std::optional<PlacementRef> selected_;

    DragMode drag_mode_ = DragMode::None;
    std::optional<PlacementRef> drag_target_;
    QPoint drag_origin_;
    core::Ticks drag_start_tick_ = 0;    // placement start at drag begin
    core::Ticks drag_length_ = 0;        // placement length at drag begin
    int drag_track_index_ = 0;
    core::Ticks drag_preview_start_ = 0;
    core::Ticks drag_preview_length_ = 0;
    int drag_preview_track_ = 0;
    bool drag_moved_ = false;

    QLineEdit* rename_editor_ = nullptr;
    core::Id rename_track_;

    QTimer* playhead_timer_ = nullptr;
    core::Ticks last_playhead_ = 0;
};

}  // namespace ui
