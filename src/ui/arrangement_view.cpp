#include "ui/arrangement_view.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "ui/theme.h"

namespace ui {

namespace {

constexpr int kHeaderWidth = 160;
constexpr int kRulerHeight = 28;
constexpr int kTrackHeight = 56;
constexpr int kAddTrackHeight = 26;
constexpr int kResizeGrip = 8;
constexpr int kScrollBarHeight = 14;
constexpr int kDragThreshold = 4;

}  // namespace

using core::functions::add_placement;
using core::functions::create_track;
using core::functions::delete_track;
using core::functions::move_placement;
using core::functions::remove_placement;
using core::functions::rename_track;
using core::functions::resize_placement;
using core::functions::set_track_muted;

ArrangementView::ArrangementView(core::ProjectHistory& history,
                                 core::TransportPort& transport, QWidget* parent)
    : QWidget(parent), history_(history), transport_(transport) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(480, 240);

    hscroll_ = new QScrollBar(Qt::Horizontal, this);
    connect(hscroll_, &QScrollBar::valueChanged, this, [this](int value) {
        scroll_x_ = value;
        update();
    });

    playhead_timer_ = new QTimer(this);
    playhead_timer_->setInterval(33);
    connect(playhead_timer_, &QTimer::timeout, this, [this] {
        core::PlaybackStatus status = transport_.status();
        if (status.playing || status.position != last_playhead_) {
            last_playhead_ = status.position;
            update();
        }
    });
    playhead_timer_->start();

    history_.observe([this] {
        update_scrollbar();
        update();
    });
    update_scrollbar();
}

// --------------------------------------------------------------------------
// geometry

const core::Arrangement* ArrangementView::arrangement() const {
    const auto& items = history_.read().arrangements.items;
    return items.empty() ? nullptr : &items.front();
}

core::Ticks ArrangementView::beat_ticks() const { return core::kPpq; }

core::Ticks ArrangementView::bar_ticks() const {
    return static_cast<core::Ticks>(history_.read().time_signature.first) * core::kPpq;
}

int ArrangementView::tick_to_x(core::Ticks tick) const {
    return kHeaderWidth +
           static_cast<int>(std::llround(static_cast<double>(tick) * px_per_beat_ /
                                         static_cast<double>(core::kPpq))) -
           scroll_x_;
}

core::Ticks ArrangementView::x_to_tick(int x) const {
    double beats = static_cast<double>(x - kHeaderWidth + scroll_x_) / px_per_beat_;
    return std::max<core::Ticks>(
        0, static_cast<core::Ticks>(std::llround(beats * static_cast<double>(core::kPpq))));
}

core::Ticks ArrangementView::snap(core::Ticks tick, core::Ticks grid) const {
    return (tick + grid / 2) / grid * grid;
}

QRect ArrangementView::placement_rect(int track_index,
                                      const core::Placement& placement) const {
    int x0 = tick_to_x(placement.start);
    int x1 = tick_to_x(placement.start + placement.length);
    int y = kRulerHeight + track_index * kTrackHeight;
    return QRect(x0, y + 3, std::max(x1 - x0, 2), kTrackHeight - 6);
}

QRect ArrangementView::track_header_rect(int track_index) const {
    return QRect(0, kRulerHeight + track_index * kTrackHeight, kHeaderWidth, kTrackHeight);
}

QRect ArrangementView::add_track_rect() const {
    const core::Arrangement* a = arrangement();
    int count = a ? static_cast<int>(a->tracks.size()) : 0;
    return QRect(0, kRulerHeight + count * kTrackHeight, kHeaderWidth, kAddTrackHeight);
}

int ArrangementView::track_index_at(int y) const {
    const core::Arrangement* a = arrangement();
    if (!a || y < kRulerHeight) return -1;
    int index = (y - kRulerHeight) / kTrackHeight;
    return index < static_cast<int>(a->tracks.size()) ? index : -1;
}

std::optional<ArrangementView::PlacementRef> ArrangementView::placement_at(
    const QPoint& pos) const {
    const core::Arrangement* a = arrangement();
    if (!a) return std::nullopt;
    int index = track_index_at(pos.y());
    if (index < 0 || pos.x() < kHeaderWidth) return std::nullopt;
    const core::Track& track = a->tracks[static_cast<std::size_t>(index)];
    // Later placements draw on top; hit-test in reverse draw order.
    for (auto it = track.placements.rbegin(); it != track.placements.rend(); ++it) {
        if (placement_rect(index, *it).contains(pos)) {
            return PlacementRef{track.id, it->id};
        }
    }
    return std::nullopt;
}

void ArrangementView::update_scrollbar() {
    const core::Arrangement* a = arrangement();
    core::Ticks end = 0;
    if (a) {
        for (const core::Track& track : a->tracks) {
            for (const core::Placement& placement : track.placements) {
                end = std::max(end, placement.start + placement.length);
            }
        }
    }
    core::Ticks span = end + 32 * bar_ticks();
    int content = static_cast<int>(static_cast<double>(span) * px_per_beat_ /
                                   static_cast<double>(core::kPpq));
    int viewport = std::max(width() - kHeaderWidth, 1);
    hscroll_->setRange(0, std::max(content - viewport, 0));
    hscroll_->setPageStep(viewport);
}

// --------------------------------------------------------------------------
// painting

void ArrangementView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), theme::kCanvas);

    const core::Arrangement* a = arrangement();
    int content_height =
        kRulerHeight + (a ? static_cast<int>(a->tracks.size()) : 0) * kTrackHeight;

    paint_grid(painter, content_height);
    paint_tracks(painter);
    paint_ruler(painter);
    paint_playhead(painter, content_height);
}

void ArrangementView::paint_grid(QPainter& painter, int content_height) {
    core::Ticks bar = bar_ticks();
    core::Ticks first = x_to_tick(kHeaderWidth) / bar * bar;
    core::Ticks last = x_to_tick(width());
    bool draw_beats = px_per_beat_ >= 14.0;

    for (core::Ticks tick = first; tick <= last; tick += beat_ticks()) {
        bool is_bar = tick % bar == 0;
        if (!is_bar && !draw_beats) continue;
        int x = tick_to_x(tick);
        if (x < kHeaderWidth) continue;
        painter.setPen(is_bar ? theme::kGridBar : theme::kGridBeat);
        painter.drawLine(x, kRulerHeight, x, std::max(content_height, height()));
    }
}

void ArrangementView::paint_ruler(QPainter& painter) {
    painter.fillRect(QRect(0, 0, width(), kRulerHeight), theme::kRuler);
    painter.setPen(theme::kGridBar);
    painter.drawLine(0, kRulerHeight, width(), kRulerHeight);

    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);

    core::Ticks bar = bar_ticks();
    core::Ticks first = x_to_tick(kHeaderWidth) / bar * bar;
    core::Ticks last = x_to_tick(width());
    for (core::Ticks tick = first; tick <= last; tick += bar) {
        int x = tick_to_x(tick);
        if (x < kHeaderWidth) continue;
        painter.setPen(theme::kTextSecondary);
        painter.drawText(x + 4, kRulerHeight - 9,
                         QString::number(tick / bar + 1));
        painter.setPen(theme::kGridBar);
        painter.drawLine(x, kRulerHeight - 6, x, kRulerHeight);
    }
}

void ArrangementView::paint_tracks(QPainter& painter) {
    const core::Arrangement* a = arrangement();
    if (!a) return;
    const core::Project& project = history_.read();

    for (int i = 0; i < static_cast<int>(a->tracks.size()); ++i) {
        const core::Track& track = a->tracks[static_cast<std::size_t>(i)];
        QRect header = track_header_rect(i);
        QColor track_hue =
            track.colour ? theme::from_colour(*track.colour) : theme::hue(i);

        // Lane separator.
        painter.setPen(theme::kGridBeat);
        painter.drawLine(kHeaderWidth, header.bottom() + 1, width(), header.bottom() + 1);

        // Placements, dimmed on a muted Track.
        for (const core::Placement& placement : track.placements) {
            const core::Pattern* pattern = project.patterns.find(placement.pattern);
            int pattern_index = 0;
            for (std::size_t p = 0; p < project.patterns.items.size(); ++p) {
                if (pattern && project.patterns.items[p].id == pattern->id) {
                    pattern_index = static_cast<int>(p);
                    break;
                }
            }
            QColor colour = pattern && pattern->colour
                                ? theme::from_colour(*pattern->colour)
                                : theme::hue(pattern_index);
            bool selected = selected_ == PlacementRef{track.id, placement.id};
            bool is_dragged = drag_mode_ != DragMode::None && drag_moved_ &&
                              drag_target_ == PlacementRef{track.id, placement.id};
            QRect box = placement_rect(i, placement);
            if (is_dragged) {
                painter.setOpacity(0.25);
            } else if (track.muted || placement.muted) {
                painter.setOpacity(0.35);
            }
            paint_placement(painter, pattern, box, colour, selected, track.muted,
                            placement.length);
            painter.setOpacity(1.0);
        }

        // Header on top of anything scrolled under it.
        painter.fillRect(header, theme::kPanel);
        painter.fillRect(QRect(header.left(), header.top(), 4, header.height()),
                         track_hue);
        painter.setPen(theme::kGridBeat);
        painter.drawLine(header.left(), header.bottom() + 1, header.right(),
                         header.bottom() + 1);
        painter.drawLine(header.right(), header.top(), header.right(), header.bottom());

        painter.setPen(track.muted ? theme::kTextSecondary : theme::kTextPrimary);
        QFont font = painter.font();
        font.setPointSizeF(9.5);
        painter.setFont(font);
        painter.drawText(header.adjusted(14, 0, -30, 0), Qt::AlignVCenter,
                         QString::fromStdString(track.name));

        // Mute dot: filled while audible, hollow while muted.
        QRect dot(header.right() - 22, header.center().y() - 5, 10, 10);
        painter.setPen(track.muted ? theme::kTextSecondary : track_hue);
        painter.setBrush(track.muted ? Qt::NoBrush : QBrush(track_hue));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawEllipse(dot);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setBrush(Qt::NoBrush);
    }

    // Drag preview ghost.
    if (drag_mode_ != DragMode::None && drag_moved_ && drag_target_) {
        const core::Pattern* pattern = nullptr;
        for (const core::Track& track : a->tracks) {
            for (const core::Placement& placement : track.placements) {
                if (placement.id == drag_target_->placement) {
                    pattern = project.patterns.find(placement.pattern);
                }
            }
        }
        core::Placement ghost;
        ghost.start = drag_preview_start_;
        ghost.length = drag_preview_length_;
        QRect box = placement_rect(drag_preview_track_, ghost);
        painter.setOpacity(0.8);
        paint_placement(painter, pattern, box, theme::kAccent, true, false,
                        drag_preview_length_);
        painter.setOpacity(1.0);
    }

    // The add-track affordance.
    QRect add = add_track_rect();
    painter.fillRect(add, theme::kPanel);
    painter.setPen(theme::kTextSecondary);
    painter.drawText(add.adjusted(14, 0, 0, 0), Qt::AlignVCenter, "+ Track");
    painter.setPen(theme::kGridBeat);
    painter.drawLine(add.left(), add.bottom() + 1, add.right(), add.bottom() + 1);
    painter.drawLine(add.right(), add.top(), add.right(), add.bottom());

    // Panel column above the ruler corner.
    painter.fillRect(QRect(0, 0, kHeaderWidth, kRulerHeight), theme::kPanel);
    painter.setPen(theme::kGridBar);
    painter.drawLine(kHeaderWidth, 0, kHeaderWidth, height());
    painter.drawLine(0, kRulerHeight, kHeaderWidth, kRulerHeight);
}

void ArrangementView::paint_placement(QPainter& painter, const core::Pattern* pattern,
                                      const QRect& rect, const QColor& colour,
                                      bool selected, bool dimmed,
                                      core::Ticks placement_length) {
    QColor fill = colour;
    fill.setAlpha(dimmed ? 40 : 70);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(selected ? theme::kAccent : colour, selected ? 2.0 : 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(rect.adjusted(0, 0, -1, -1), 3, 3);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);

    // The signature detail: a faint tick at every Pattern-length repeat, so
    // looping (and trimming) is visible on the block itself.
    if (pattern && pattern->length > 0) {
        QColor tick = colour;
        tick.setAlpha(110);
        painter.setPen(tick);
        for (core::Ticks loop = pattern->length; loop < placement_length;
             loop += pattern->length) {
            int x = rect.left() +
                    static_cast<int>(std::llround(static_cast<double>(loop) * px_per_beat_ /
                                                  static_cast<double>(core::kPpq)));
            if (x > rect.left() + 2 && x < rect.right() - 2) {
                painter.drawLine(x, rect.top() + 3, x, rect.bottom() - 3);
            }
        }
    }

    if (pattern && rect.width() > 40) {
        painter.setPen(theme::kTextPrimary);
        QFont font = painter.font();
        font.setPointSizeF(8.5);
        painter.setFont(font);
        painter.drawText(rect.adjusted(6, 2, -4, 0), Qt::AlignTop | Qt::AlignLeft,
                         QString::fromStdString(pattern->name));
    }
}

void ArrangementView::paint_playhead(QPainter& painter, int content_height) {
    int x = tick_to_x(transport_.status().position);
    if (x < kHeaderWidth) return;
    painter.setPen(QPen(theme::kAccent, 1.0));
    painter.drawLine(x, 0, x, std::max(content_height, height()));
    // A small pennant on the ruler.
    QPolygon pennant;
    pennant << QPoint(x, 0) << QPoint(x + 7, 5) << QPoint(x, 10);
    painter.setBrush(theme::kAccent);
    painter.setPen(Qt::NoPen);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawPolygon(pennant);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
}

// --------------------------------------------------------------------------
// interaction

void ArrangementView::mousePressEvent(QMouseEvent* event) {
    commit_rename();
    setFocus();
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    QPoint pos = event->pos();

    // Ruler: seek, snapped to the beat.
    if (pos.y() < kRulerHeight && pos.x() >= kHeaderWidth) {
        transport_.seek(snap(x_to_tick(pos.x()), beat_ticks()));
        update();
        return;
    }

    // Add-track affordance.
    if (add_track_rect().contains(pos)) {
        const core::Arrangement* a = arrangement();
        if (a) {
            auto created = create_track(history_.read(), a->id,
                                        "Track " + std::to_string(a->tracks.size() + 1),
                                        std::nullopt);
            if (created.ok()) history_.apply(std::move(created->delta));
        }
        return;
    }

    // Track header: the mute dot toggles; the rest selects the lane.
    int index = track_index_at(pos.y());
    if (index >= 0 && pos.x() < kHeaderWidth) {
        const core::Arrangement* a = arrangement();
        const core::Track& track = a->tracks[static_cast<std::size_t>(index)];
        QRect header = track_header_rect(index);
        QRect dot(header.right() - 26, header.top(), 26, header.height());
        if (dot.contains(pos)) {
            apply_or_hint(set_track_muted(history_.read(), a->id, track.id, !track.muted));
        }
        return;
    }

    // A Placement: select, and prepare a move or resize drag.
    if (auto hit = placement_at(pos)) {
        selected_ = hit;
        const core::Arrangement* a = arrangement();
        for (int i = 0; i < static_cast<int>(a->tracks.size()); ++i) {
            const core::Track& track = a->tracks[static_cast<std::size_t>(i)];
            if (track.id != hit->track) continue;
            for (const core::Placement& placement : track.placements) {
                if (placement.id != hit->placement) continue;
                QRect box = placement_rect(i, placement);
                drag_mode_ = (box.right() - pos.x() <= kResizeGrip) ? DragMode::Resize
                                                                    : DragMode::Move;
                drag_target_ = hit;
                drag_origin_ = pos;
                drag_start_tick_ = placement.start;
                drag_length_ = placement.length;
                drag_track_index_ = i;
                drag_preview_start_ = placement.start;
                drag_preview_length_ = placement.length;
                drag_preview_track_ = i;
                drag_moved_ = false;
            }
        }
        emit hint_changed("Drag to move - drag the right edge to loop or trim - "
                          "Delete removes");
        update();
        return;
    }

    // Empty lane with an armed Pattern: place it, snapped to the bar.
    if (index >= 0 && armed_pattern_) {
        const core::Arrangement* a = arrangement();
        const core::Track& track = a->tracks[static_cast<std::size_t>(index)];
        core::Ticks start = snap(x_to_tick(pos.x()), bar_ticks());
        auto placed =
            add_placement(history_.read(), a->id, track.id, *armed_pattern_, start);
        if (placed.ok()) {
            history_.apply(std::move(placed->delta));
            selected_ = PlacementRef{track.id, placed->id};
        } else {
            emit hint_changed(QString::fromStdString(placed.error));
        }
        update();
        return;
    }

    selected_.reset();
    update();
}

void ArrangementView::mouseMoveEvent(QMouseEvent* event) {
    QPoint pos = event->pos();

    if (drag_mode_ == DragMode::None) {
        // Cursor affordance over placement edges.
        if (auto hit = placement_at(pos)) {
            const core::Arrangement* a = arrangement();
            for (int i = 0; i < static_cast<int>(a->tracks.size()); ++i) {
                const core::Track& track = a->tracks[static_cast<std::size_t>(i)];
                if (track.id != hit->track) continue;
                for (const core::Placement& placement : track.placements) {
                    if (placement.id != hit->placement) continue;
                    QRect box = placement_rect(i, placement);
                    setCursor(box.right() - pos.x() <= kResizeGrip ? Qt::SizeHorCursor
                                                                   : Qt::OpenHandCursor);
                    return;
                }
            }
        }
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (!drag_moved_ && (pos - drag_origin_).manhattanLength() < kDragThreshold) return;
    drag_moved_ = true;

    if (drag_mode_ == DragMode::Move) {
        core::Ticks delta = x_to_tick(pos.x()) - x_to_tick(drag_origin_.x());
        drag_preview_start_ =
            std::max<core::Ticks>(0, snap(drag_start_tick_ + delta, beat_ticks()));
        int index = track_index_at(pos.y());
        if (index >= 0) drag_preview_track_ = index;
    } else {
        core::Ticks end = x_to_tick(pos.x());
        core::Ticks length = snap(end - drag_start_tick_, beat_ticks());
        drag_preview_length_ = std::max<core::Ticks>(beat_ticks(), length);
    }
    update();
}

void ArrangementView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || drag_mode_ == DragMode::None) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    DragMode mode = drag_mode_;
    drag_mode_ = DragMode::None;
    setCursor(Qt::ArrowCursor);

    if (!drag_moved_ || !drag_target_) {
        update();
        return;
    }

    const core::Arrangement* a = arrangement();
    if (!a) return;

    if (mode == DragMode::Move) {
        core::Id to_track = a->tracks[static_cast<std::size_t>(drag_preview_track_)].id;
        if (drag_preview_start_ != drag_start_tick_ || to_track != drag_target_->track) {
            apply_or_hint(move_placement(history_.read(), a->id, drag_target_->track,
                                         drag_target_->placement, drag_preview_start_,
                                         to_track));
            selected_ = PlacementRef{to_track, drag_target_->placement};
        }
    } else if (drag_preview_length_ != drag_length_) {
        apply_or_hint(resize_placement(history_.read(), a->id, drag_target_->track,
                                       drag_target_->placement, drag_preview_length_));
    }
    drag_target_.reset();
    update();
}

void ArrangementView::mouseDoubleClickEvent(QMouseEvent* event) {
    int index = track_index_at(event->pos().y());
    if (index >= 0 && event->pos().x() < kHeaderWidth) {
        begin_rename(index);
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ArrangementView::contextMenuEvent(QContextMenuEvent* event) {
    const core::Arrangement* a = arrangement();
    if (!a) return;
    QPoint pos = event->pos();

    if (auto hit = placement_at(pos)) {
        QMenu menu(this);
        QAction* remove = menu.addAction("Remove");
        if (menu.exec(event->globalPos()) == remove) {
            apply_or_hint(
                remove_placement(history_.read(), a->id, hit->track, hit->placement));
            if (selected_ == hit) selected_.reset();
        }
        return;
    }

    int index = track_index_at(pos.y());
    if (index >= 0 && pos.x() < kHeaderWidth) {
        const core::Track& track = a->tracks[static_cast<std::size_t>(index)];
        QMenu menu(this);
        QAction* rename = menu.addAction("Rename...");
        QAction* remove = menu.addAction("Delete Track");
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == rename) {
            begin_rename(index);
        } else if (chosen == remove) {
            apply_or_hint(delete_track(history_.read(), a->id, track.id));
        }
    }
}

void ArrangementView::keyPressEvent(QKeyEvent* event) {
    const core::Arrangement* a = arrangement();
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (a && selected_) {
            apply_or_hint(remove_placement(history_.read(), a->id, selected_->track,
                                           selected_->placement));
            selected_.reset();
        }
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (armed_pattern_) {
            set_armed_pattern(std::nullopt);
            emit hint_changed("");
        } else {
            selected_.reset();
        }
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ArrangementView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom around the cursor.
        double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
        double anchor_tick = static_cast<double>(x_to_tick(
            static_cast<int>(event->position().x())));
        px_per_beat_ = std::clamp(px_per_beat_ * factor, 4.0, 200.0);
        int anchor_x = static_cast<int>(event->position().x());
        scroll_x_ = std::max(
            0, static_cast<int>(std::llround(anchor_tick * px_per_beat_ /
                                             static_cast<double>(core::kPpq))) -
                   (anchor_x - kHeaderWidth));
        update_scrollbar();
        hscroll_->setValue(scroll_x_);
        update();
    } else {
        hscroll_->setValue(hscroll_->value() -
                           event->angleDelta().y() / 2);
    }
}

void ArrangementView::resizeEvent(QResizeEvent* event) {
    hscroll_->setGeometry(kHeaderWidth, height() - kScrollBarHeight,
                          width() - kHeaderWidth, kScrollBarHeight);
    update_scrollbar();
    QWidget::resizeEvent(event);
}

// --------------------------------------------------------------------------
// mutations

void ArrangementView::set_armed_pattern(std::optional<core::Id> pattern) {
    armed_pattern_ = pattern;
    if (pattern) {
        emit hint_changed("Click a lane to place the Pattern - Esc cancels");
    }
}

void ArrangementView::apply_or_hint(core::functions::Expected<core::Delta> result) {
    if (result.ok()) {
        history_.apply(std::move(*result));
    } else {
        emit hint_changed(QString::fromStdString(result.error));
    }
}

void ArrangementView::begin_rename(int track_index) {
    const core::Arrangement* a = arrangement();
    if (!a) return;
    const core::Track& track = a->tracks[static_cast<std::size_t>(track_index)];

    commit_rename();
    rename_track_ = track.id;
    rename_editor_ = new QLineEdit(QString::fromStdString(track.name), this);
    QRect header = track_header_rect(track_index);
    rename_editor_->setGeometry(header.adjusted(8, 14, -30, -14));
    rename_editor_->setStyleSheet(
        QString("background: %1; color: %2; border: 1px solid %3;")
            .arg(theme::kCanvas.name(), theme::kTextPrimary.name(),
                 theme::kAccent.name()));
    connect(rename_editor_, &QLineEdit::editingFinished, this,
            &ArrangementView::commit_rename);
    rename_editor_->show();
    rename_editor_->setFocus();
    rename_editor_->selectAll();
}

void ArrangementView::commit_rename() {
    if (!rename_editor_) return;
    QLineEdit* editor = rename_editor_;
    rename_editor_ = nullptr;  // re-entrancy: editingFinished fires on focus-out
    QString name = editor->text().trimmed();
    editor->deleteLater();

    const core::Arrangement* a = arrangement();
    if (a && !name.isEmpty()) {
        apply_or_hint(
            rename_track(history_.read(), a->id, rename_track_, name.toStdString()));
    }
    update();
}

}  // namespace ui
