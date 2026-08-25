#include "ui/step_sequencer.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "core/sample_functions.h"
#include "ui/provenance.h"
#include "ui/theme.h"

namespace ui {
namespace {

constexpr int kHeaderWidth = 140;
constexpr int kRowHeight = 30;
constexpr int kMinCellWidth = 22;
constexpr int kMaxCellWidth = 44;
constexpr int kFooterHeight = 26;  // the "add a lane" hint under the rows
constexpr int kStepsPerBeat = 4;
constexpr core::Velocity kMaxVelocity = 127;

using core::functions::clear_part_steps;
using core::functions::create_pattern;
using core::functions::kStepTicks;
using core::functions::remove_part;
using core::functions::rename_pattern;
using core::functions::set_part_muted;
using core::functions::set_part_step_velocities;
using core::functions::set_part_steps;

bool on_grid(const core::Event& e) { return e.start % kStepTicks == 0; }

const core::Event* step_event(const core::Part& part, int step) {
    core::Ticks start = step * kStepTicks;
    for (const core::Event& e : part.events)
        if (e.start == start) return &e;
    return nullptr;
}

std::vector<int> steps_of(const core::Part& part) {
    std::vector<int> steps;
    for (const core::Event& e : part.events)
        if (on_grid(e)) steps.push_back(static_cast<int>(e.start / kStepTicks));
    return steps;
}

}  // namespace

// ---------------------------------------------------------------------------
// StepGrid

StepGrid::StepGrid(core::ProjectHistory& history, QWidget* parent)
    : QWidget(parent), history_(history) {
    setMouseTracking(false);
    setFocusPolicy(Qt::ClickFocus);
    history_.observe([this] {
        if (pattern_ && !current()) pattern_.reset();
        updateGeometry();
        update();
    });
}

void StepGrid::set_pattern(std::optional<core::Id> pattern) {
    pattern_ = pattern;
    if (pattern_ && !current()) pattern_.reset();
    drag_ = Drag::None;
    updateGeometry();
    update();
}

const core::Pattern* StepGrid::current() const {
    return pattern_ ? history_.read().patterns.find(*pattern_) : nullptr;
}

int StepGrid::step_count() const {
    const core::Pattern* pat = current();
    return pat ? static_cast<int>(pat->length / kStepTicks) : 0;
}

int StepGrid::cell_width() const {
    int steps = std::max(1, step_count());
    return std::clamp((width() - kHeaderWidth) / steps, kMinCellWidth, kMaxCellWidth);
}

QSize StepGrid::sizeHint() const {
    const core::Pattern* pat = current();
    int lanes = pat ? static_cast<int>(pat->parts.size()) : 0;
    return QSize(kHeaderWidth + step_count() * kMinCellWidth,
                 lanes * kRowHeight + kFooterHeight);
}

QRect StepGrid::cell_rect(int lane, int step) const {
    return QRect(kHeaderWidth + step * cell_width(), lane * kRowHeight, cell_width(), kRowHeight);
}

QRect StepGrid::header_rect(int lane) const {
    return QRect(0, lane * kRowHeight, kHeaderWidth, kRowHeight);
}

QRect StepGrid::mute_dot_rect(int lane) const {
    QRect header = header_rect(lane);
    return QRect(header.right() - 26, header.top(), 26, header.height());
}

int StepGrid::lane_at(int y) const {
    const core::Pattern* pat = current();
    if (!pat || y < 0) return -1;
    int lane = y / kRowHeight;
    return lane < static_cast<int>(pat->parts.size()) ? lane : -1;
}

std::optional<StepGrid::Cell> StepGrid::cell_at(const QPoint& pos) const {
    int lane = lane_at(pos.y());
    if (lane < 0 || pos.x() < kHeaderWidth) return std::nullopt;
    int step = (pos.x() - kHeaderWidth) / cell_width();
    if (step >= step_count()) return std::nullopt;
    return Cell{lane, step};
}

QString StepGrid::lane_name(const core::Part& part) const {
    const core::Instrument* inst = history_.read().instruments.find(part.instrument);
    return inst ? QString::fromStdString(inst->name) : "(missing Instrument)";
}

// --- painting --------------------------------------------------------------

void StepGrid::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), theme::kCanvas);
    const core::Pattern* pat = current();

    int lanes = pat ? static_cast<int>(pat->parts.size()) : 0;
    for (int i = 0; i < lanes; ++i) paint_lane(painter, i, pat->parts[static_cast<std::size_t>(i)]);

    // The footer says how a lane gets here, since the grid itself offers no
    // way to add one.
    QRect footer(0, lanes * kRowHeight, width(), kFooterHeight);
    painter.fillRect(footer, theme::kPanel);
    painter.setPen(theme::kTextSecondary);
    QFont font = painter.font();
    font.setPointSizeF(9.0);
    painter.setFont(font);
    QString footer_text =
        !pat ? "No Pattern open. Press + Pattern, or click one in the Patterns list."
             : "Add a lane: pick a Sample on the left and choose Place in Pattern.";
    painter.drawText(footer.adjusted(12, 0, -8, 0), Qt::AlignVCenter, footer_text);
}

void StepGrid::paint_lane(QPainter& painter, int index, const core::Part& part) {
    QColor hue = theme::hue(index);
    int steps = step_count();
    QRect header = header_rect(index);

    // Cells: beats alternate in shade so a bar reads as four groups of four.
    for (int step = 0; step < steps; ++step) {
        QRect cell = cell_rect(index, step);
        bool odd_beat = (step / kStepsPerBeat) % 2 == 1;
        painter.fillRect(cell, odd_beat ? theme::kGridBeat : theme::kCanvas);
        painter.setPen(step % (kStepsPerBeat * 4) == 0 ? theme::kGridBar : theme::kGridBeat);
        painter.drawLine(cell.left(), cell.top(), cell.left(), cell.bottom());
    }

    painter.setOpacity(part.muted ? 0.35 : 1.0);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    for (const core::Event& e : part.events) {
        if (on_grid(e)) {
            int step = static_cast<int>(e.start / kStepTicks);
            if (step >= steps) continue;  // beyond the Pattern's length: retained, not shown
            // Velocity is the fill's weight, so a ghost note reads quieter.
            QColor fill = hue;
            fill.setAlphaF(0.3f + 0.7f * (static_cast<float>(e.velocity) / kMaxVelocity));
            painter.setBrush(fill);
            painter.drawRoundedRect(cell_rect(index, step).adjusted(3, 5, -3, -5), 3, 3);
        } else {
            // Off the grid: a note the piano roll owns. Shown, never edited here.
            int x = kHeaderWidth + static_cast<int>(e.start * cell_width() / kStepTicks);
            painter.setBrush(theme::kTextSecondary);
            painter.drawRect(QRect(x, header.top() + 8, 2, kRowHeight - 16));
        }
    }
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setOpacity(1.0);

    // Row separator.
    painter.setPen(theme::kGridBeat);
    painter.drawLine(0, header.bottom(), width(), header.bottom());

    // Header: the Instrument's name and the mute dot.
    painter.fillRect(header, theme::kPanel);
    painter.fillRect(QRect(header.left(), header.top(), 4, header.height()), hue);
    painter.setPen(theme::kGridBeat);
    painter.drawLine(header.right(), header.top(), header.right(), header.bottom());
    painter.drawLine(header.left(), header.bottom(), header.right(), header.bottom());
    painter.setPen(part.muted ? theme::kTextSecondary : theme::kTextPrimary);
    QFont font = painter.font();
    font.setPointSizeF(9.5);
    painter.setFont(font);
    painter.drawText(header.adjusted(12, 0, -30, 0), Qt::AlignVCenter, lane_name(part));

    QRect dot(header.right() - 22, header.center().y() - 5, 10, 10);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(part.muted ? theme::kTextSecondary : hue);
    painter.setBrush(part.muted ? Qt::NoBrush : QBrush(hue));
    painter.drawEllipse(dot);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
}

// --- mutations -------------------------------------------------------------

void StepGrid::apply_or_hint(core::functions::Expected<core::Delta> result) {
    if (result.ok()) {
        history_.apply(std::move(*result));
    } else {
        emit hint_changed(QString::fromStdString(result.error));
    }
}

void StepGrid::draw_step(int lane, int step, bool on) {
    const core::Pattern* pat = current();
    if (!pat || lane < 0 || lane >= static_cast<int>(pat->parts.size())) return;
    const core::Part& part = pat->parts[static_cast<std::size_t>(lane)];
    if ((step_event(part, step) != nullptr) == on) return;
    std::vector<int> steps = steps_of(part);
    if (on) {
        steps.push_back(step);
    } else {
        steps.erase(std::remove(steps.begin(), steps.end(), step), steps.end());
    }
    apply_or_hint(set_part_steps(history_.read(), pat->id, part.id, std::move(steps)));
}

void StepGrid::set_velocity(int lane, int step, core::Velocity velocity) {
    const core::Pattern* pat = current();
    if (!pat || lane < 0 || lane >= static_cast<int>(pat->parts.size())) return;
    const core::Part& part = pat->parts[static_cast<std::size_t>(lane)];
    apply_or_hint(set_part_step_velocities(history_.read(), pat->id, part.id, {{step, velocity}}));
    emit hint_changed(QString("Velocity %1").arg(velocity));
}

void StepGrid::mousePressEvent(QMouseEvent* event) {
    const core::Pattern* pat = current();
    if (!pat) return;
    QPoint pos = event->pos();

    // Row header: the dot mutes.
    int lane = lane_at(pos.y());
    if (lane >= 0 && pos.x() < kHeaderWidth) {
        if (mute_dot_rect(lane).contains(pos) && event->button() == Qt::LeftButton) {
            const core::Part& part = pat->parts[static_cast<std::size_t>(lane)];
            apply_or_hint(set_part_muted(history_.read(), pat->id, part.id, !part.muted));
        }
        return;
    }

    std::optional<Cell> cell = cell_at(pos);
    if (!cell) return;
    const core::Part& part = pat->parts[static_cast<std::size_t>(cell->lane)];
    const core::Event* existing = step_event(part, cell->step);
    QString name = lane_name(part);

    // Shift on a step that is on: its velocity follows the drag.
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) {
        if (!existing) {
            emit hint_changed("Shift-drag a step that is on to set its velocity.");
            return;
        }
        drag_ = Drag::Velocity;
        drag_lane_ = cell->lane;
        velocity_step_ = cell->step;
        velocity_start_ = existing->velocity;
        drag_origin_ = pos;
        history_.begin_gesture("Set velocity of '" + name.toStdString() + "' in '" + pat->name +
                               "'");
        emit hint_changed(QString("Velocity %1 - drag up or down").arg(existing->velocity));
        return;
    }

    // Left draws, right erases; a drag paints the same state along the row.
    if (event->button() == Qt::LeftButton) {
        draw_on_ = existing == nullptr;
    } else if (event->button() == Qt::RightButton) {
        draw_on_ = false;
    } else {
        return;
    }
    drag_ = Drag::Draw;
    drag_lane_ = cell->lane;
    history_.begin_gesture((draw_on_ ? "Draw steps of '" : "Erase steps of '") +
                           name.toStdString() + "' in '" + pat->name + "'");
    draw_step(cell->lane, cell->step, draw_on_);
    emit hint_changed("Drag along the row to paint - right-click erases - "
                      "Shift-drag a step for velocity");
}

void StepGrid::mouseMoveEvent(QMouseEvent* event) {
    if (drag_ == Drag::Draw) {
        // Painting stays on the row the drag began in; the lane above or
        // below is not touched by a wobble of the hand.
        int x = std::clamp(event->pos().x(), kHeaderWidth, kHeaderWidth + step_count() * cell_width() - 1);
        std::optional<Cell> cell = cell_at(QPoint(x, drag_lane_ * kRowHeight + 1));
        if (cell) draw_step(drag_lane_, cell->step, draw_on_);
    } else if (drag_ == Drag::Velocity) {
        int delta = drag_origin_.y() - event->pos().y();
        int value = std::clamp(static_cast<int>(velocity_start_) + delta, 1,
                               static_cast<int>(kMaxVelocity));
        set_velocity(drag_lane_, velocity_step_, static_cast<core::Velocity>(value));
    }
}

void StepGrid::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ == Drag::None) return;
    drag_ = Drag::None;
    history_.end_gesture();
}

void StepGrid::contextMenuEvent(QContextMenuEvent* event) {
    // Over the cells the right button erases (mousePressEvent); the menu is
    // for the row itself.
    const core::Pattern* pat = current();
    int lane = pat ? lane_at(event->pos().y()) : -1;
    if (lane < 0 || event->pos().x() >= kHeaderWidth) {
        event->accept();
        return;
    }
    core::Id pattern = pat->id;
    core::Id part = pat->parts[static_cast<std::size_t>(lane)].id;
    QMenu menu(this);
    menu.addAction("Clear steps", this, [this, pattern, part] {
        apply_or_hint(clear_part_steps(history_.read(), pattern, part));
    });
    menu.addAction("Remove lane", this, [this, pattern, part] {
        apply_or_hint(remove_part(history_.read(), pattern, part));
    });
    menu.exec(event->globalPos());
}

// ---------------------------------------------------------------------------
// StepSequencer

StepSequencer::StepSequencer(core::ProjectHistory& history, QWidget* parent)
    : QWidget(parent), history_(history) {
    setStyleSheet(QString("QWidget { background: %1; color: %2; }"
                          "QComboBox, QLineEdit { background: %3; border: none;"
                          " border-radius: 3px; padding: 3px 6px; }"
                          "QComboBox::drop-down { border: none; }"
                          "QComboBox QAbstractItemView { background: %3; color: %2;"
                          " selection-background-color: %4; }"
                          "QToolButton { border: none; padding: 4px 8px; }"
                          "QToolButton:hover { color: %5; }"
                          "QScrollArea { border: none; }")
                      .arg(theme::kPanel.name(), theme::kTextPrimary.name(),
                           theme::kCanvas.name(), theme::kGridBar.name(),
                           theme::kAccent.name()));

    picker_ = new QComboBox(this);
    picker_->setToolTip("The Pattern open in the sequencer");
    connect(picker_, &QComboBox::activated, this, [this](int index) {
        const auto& patterns = history_.read().patterns.items;
        if (index >= 0 && index < static_cast<int>(patterns.size()))
            set_pattern(patterns[static_cast<std::size_t>(index)].id);
    });

    name_ = new QLineEdit(this);
    name_->setPlaceholderText("Pattern name");
    name_->setToolTip("Rename the open Pattern");
    connect(name_, &QLineEdit::editingFinished, this, [this] { commit_rename(); });

    badge_ = new QLabel("AI", this);
    badge_->setObjectName("provenance_badge");
    badge_->setStyleSheet(QString("background: %1; color: %2; font-size: 9px; font-weight: 700;"
                                  " padding: 2px 4px; border-radius: 3px;")
                              .arg(theme::kAccent.name(), theme::kCanvas.name()));
    badge_->hide();

    add_ = new QToolButton(this);
    add_->setText("+ Pattern");
    add_->setToolTip("Add an empty one-bar Pattern");
    connect(add_, &QToolButton::clicked, this, [this] { add_pattern(); });

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->setSpacing(6);
    bar->addWidget(picker_, 2);
    bar->addWidget(name_, 3);
    bar->addWidget(badge_);
    bar->addStretch(1);
    bar->addWidget(add_);

    grid_ = new StepGrid(history_, this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(grid_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    connect(grid_, &StepGrid::hint_changed, this, &StepSequencer::hint_changed);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bar);
    layout->addWidget(scroll, 1);

    history_.observe([this] { reload(); });
    const auto& patterns = history_.read().patterns.items;
    if (!patterns.empty()) grid_->set_pattern(patterns.front().id);
    reload();
}

void StepSequencer::set_pattern(std::optional<core::Id> pattern) {
    grid_->set_pattern(pattern);
    reload();
    if (const core::Pattern* pat = pattern ? history_.read().patterns.find(*pattern) : nullptr) {
        if (!pat->provenance.is_human()) emit hint_changed(provenance::text(pat->provenance));
    }
}

void StepSequencer::reload() {
    const core::Project& project = history_.read();
    std::optional<core::Id> open = grid_->pattern();
    const core::Pattern* pat = open ? project.patterns.find(*open) : nullptr;

    picker_->blockSignals(true);
    picker_->clear();
    int current = -1;
    for (std::size_t i = 0; i < project.patterns.items.size(); ++i) {
        const core::Pattern& candidate = project.patterns.items[i];
        picker_->addItem(QString::fromStdString(candidate.name));
        if (pat && candidate.id == pat->id) current = static_cast<int>(i);
    }
    picker_->setCurrentIndex(current);
    picker_->blockSignals(false);

    name_->setEnabled(pat != nullptr);
    if (!name_->hasFocus()) name_->setText(pat ? QString::fromStdString(pat->name) : QString());

    // Section 6.3: "when it is opened up" — the mark sits beside the name.
    bool generated = pat && !pat->provenance.is_human();
    badge_->setVisible(generated);
    badge_->setToolTip(pat ? provenance::text(pat->provenance) : QString());
    name_->setAccessibleDescription(generated ? "AI-generated" : QString());
}

void StepSequencer::add_pattern() {
    const core::Project& project = history_.read();
    std::string name = "Pattern " + std::to_string(project.patterns.items.size() + 1);
    auto created = create_pattern(project, name, 1);
    if (!created.ok()) {
        emit hint_changed(QString::fromStdString(created.error));
        return;
    }
    history_.apply(std::move(created->delta));
    set_pattern(created->id);
    name_->setFocus();
    name_->selectAll();
    emit hint_changed("Type a name, then add lanes from the Samples list.");
}

void StepSequencer::commit_rename() {
    std::optional<core::Id> open = grid_->pattern();
    const core::Pattern* pat = open ? history_.read().patterns.find(*open) : nullptr;
    if (!pat) return;
    std::string name = name_->text().trimmed().toStdString();
    if (name == pat->name) return;
    auto renamed = rename_pattern(history_.read(), pat->id, name);
    if (renamed.ok()) {
        history_.apply(std::move(*renamed));
    } else {
        emit hint_changed(QString::fromStdString(renamed.error));
        name_->setText(QString::fromStdString(pat->name));
    }
}

}  // namespace ui
