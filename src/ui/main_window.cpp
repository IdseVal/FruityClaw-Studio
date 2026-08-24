#include "ui/main_window.h"

#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include "ui/arrangement_view.h"
#include "ui/pattern_palette.h"
#include "ui/theme.h"

namespace ui {

MainWindow::MainWindow(core::ProjectHistory& history, core::TransportPort& transport,
                       const QString& product_name, QWidget* parent)
    : QMainWindow(parent), history_(history), transport_(transport) {
    setWindowTitle(product_name);
    resize(1200, 640);
    setStyleSheet(QString("QMainWindow, QToolBar, QStatusBar { background: %1; "
                          "color: %2; border: none; }"
                          "QToolButton { color: %2; background: transparent; "
                          "border: none; padding: 6px 10px; font-size: 13px; }"
                          "QToolButton:hover { color: %3; }"
                          "QToolButton:disabled { color: %4; }"
                          "QSplitter::handle { background: %5; }")
                      .arg(theme::kPanel.name(), theme::kTextPrimary.name(),
                           theme::kAccent.name(), theme::kTextSecondary.name(),
                           theme::kGridBar.name()));

    // --- transport bar ------------------------------------------------------
    auto* bar = addToolBar("Transport");
    bar->setMovable(false);

    play_button_ = new QToolButton(bar);
    play_button_->setText("Play");
    play_button_->setShortcut(QKeySequence(Qt::Key_Space));
    connect(play_button_, &QToolButton::clicked, this, [this] {
        if (transport_.status().playing) {
            transport_.stop();
        } else {
            transport_.play();
        }
        refresh_transport();
    });
    bar->addWidget(play_button_);

    auto* rewind = new QToolButton(bar);
    rewind->setText("|<");
    rewind->setToolTip("Back to the start");
    connect(rewind, &QToolButton::clicked, this, [this] { transport_.seek(0); });
    bar->addWidget(rewind);

    position_label_ = new QLabel("  001.1  ", bar);
    position_label_->setStyleSheet(
        QString("color: %1; font-family: Consolas, monospace; font-size: 14px;")
            .arg(theme::kAccent.name()));
    bar->addWidget(position_label_);

    tempo_label_ = new QLabel(bar);
    tempo_label_->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(theme::kTextSecondary.name()));
    bar->addWidget(tempo_label_);

    auto* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    undo_action_ = new QAction("Undo", this);
    undo_action_->setShortcut(QKeySequence::Undo);
    connect(undo_action_, &QAction::triggered, this, [this] { history_.undo(); });
    addAction(undo_action_);
    auto* undo_button = new QToolButton(bar);
    undo_button->setDefaultAction(undo_action_);
    bar->addWidget(undo_button);

    redo_action_ = new QAction("Redo", this);
    redo_action_->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Y")});
    connect(redo_action_, &QAction::triggered, this, [this] { history_.redo(); });
    addAction(redo_action_);
    auto* redo_button = new QToolButton(bar);
    redo_button->setDefaultAction(redo_action_);
    bar->addWidget(redo_button);

    // --- centre: palette | arrangement -------------------------------------
    auto* splitter = new QSplitter(this);
    palette_ = new PatternPalette(history_, splitter);
    arrangement_view_ = new ArrangementView(history_, transport_, splitter);
    splitter->addWidget(palette_);
    splitter->addWidget(arrangement_view_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({170, 1030});
    setCentralWidget(splitter);

    connect(palette_, &PatternPalette::armed_changed, this,
            [this] { arrangement_view_->set_armed_pattern(palette_->armed()); });
    connect(arrangement_view_, &ArrangementView::hint_changed, this,
            [this](const QString& hint) { statusBar()->showMessage(hint); });

    statusBar()->showMessage(
        "Click a Pattern on the left, then click a lane to place it. Space plays.");

    // --- polling ------------------------------------------------------------
    auto* timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [this] { refresh_transport(); });
    timer->start();

    history_.observe([this] { refresh_undo_redo(); });
    refresh_undo_redo();
    refresh_transport();
}

void MainWindow::refresh_transport() {
    core::PlaybackStatus status = transport_.status();
    const core::Project& project = history_.read();
    core::Ticks bar_len =
        static_cast<core::Ticks>(project.time_signature.first) * core::kPpq;
    long long bar = status.position / bar_len + 1;
    long long beat = status.position % bar_len / core::kPpq + 1;
    position_label_->setText(QString("  %1.%2  ")
                                 .arg(bar, 3, 10, QChar('0'))
                                 .arg(beat));
    tempo_label_->setText(QString("%1 BPM").arg(project.tempo));
    play_button_->setText(status.playing ? "Stop" : "Play");
}

void MainWindow::refresh_undo_redo() {
    core::HistoryState state = history_.state();
    undo_action_->setEnabled(state.can_undo);
    redo_action_->setEnabled(state.can_redo);
    undo_action_->setText(state.undo_label ? QString("Undo %1").arg(
                                                 QString::fromStdString(*state.undo_label))
                                           : "Undo");
    redo_action_->setText(state.redo_label ? QString("Redo %1").arg(
                                                 QString::fromStdString(*state.redo_label))
                                           : "Redo");
}

}  // namespace ui
