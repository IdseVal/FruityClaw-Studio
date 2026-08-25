#include "ui/main_window.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <utility>

#include "ui/arrangement_view.h"
#include "ui/pattern_palette.h"
#include "ui/theme.h"

namespace ui {

MainWindow::MainWindow(core::ProjectHistory& history, core::TransportPort& transport,
                       ProjectFilePort files, const QString& product_name, QWidget* parent)
    : QMainWindow(parent),
      history_(history),
      transport_(transport),
      files_(std::move(files)),
      product_name_(product_name) {
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

    // --- file menu ----------------------------------------------------------
    auto* file_menu = menuBar()->addMenu("&File");
    auto* open_action = file_menu->addAction("&Open...", QKeySequence::Open, this,
                                             [this] { open(); });
    auto* save_action = file_menu->addAction("&Save", QKeySequence::Save, this,
                                             [this] { save(); });
    auto* save_as_action = file_menu->addAction("Save &As...", QKeySequence::SaveAs, this,
                                                [this] { save_as(); });
    for (QAction* action : {open_action, save_action, save_as_action}) addAction(action);

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

    history_.observe([this] {
        refresh_undo_redo();
        refresh_title();
    });
    refresh_undo_redo();
    refresh_title();
    refresh_transport();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirm_discard_changes()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::refresh_title() {
    QString file = current_file_.empty()
                       ? "Untitled"
                       : QString::fromStdU16String(current_file_.filename().u16string());
    QString dirty = history_.state().is_dirty ? "*" : "";
    setWindowTitle(QString("%1%2 - %3").arg(file, dirty, product_name_));
}

bool MainWindow::save() {
    if (current_file_.empty()) return save_as();
    std::string error = files_.save(current_file_);
    if (!error.empty()) {
        QMessageBox::critical(this, "Save failed", QString::fromStdString(error));
        return false;
    }
    // Only an explicit user save moves the saved cursor (history contract 8.3).
    history_.mark_saved();
    refresh_title();
    statusBar()->showMessage("Saved " + QString::fromStdU16String(current_file_.u16string()));
    return true;
}

bool MainWindow::save_as() {
    QString chosen = QFileDialog::getSaveFileName(
        this, "Save Project", QString::fromStdU16String(current_file_.u16string()));
    if (chosen.isEmpty()) return false;
    // Paths cross to and from Qt as UTF-16: on MSVC the narrow path
    // constructor is the ANSI code page, which mangles non-ASCII names.
    std::filesystem::path previous = std::exchange(current_file_, chosen.toStdU16String());
    if (save()) return true;
    current_file_ = previous;  // a failed first save names no file
    return false;
}

void MainWindow::open() {
    if (!confirm_discard_changes()) return;
    QString chosen = QFileDialog::getOpenFileName(this, "Open Project");
    if (chosen.isEmpty()) return;
    std::filesystem::path path(chosen.toStdU16String());
    std::string error = files_.open(path);
    if (!error.empty()) {
        QMessageBox::critical(this, "Open failed", QString::fromStdString(error));
        return;
    }
    current_file_ = path;
    refresh_title();
    statusBar()->showMessage("Opened " + chosen);
}

bool MainWindow::confirm_discard_changes() {
    if (!history_.state().is_dirty) return true;
    auto choice = QMessageBox::question(
        this, "Unsaved changes", "The Project has unsaved changes. Save them?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Save) return save();
    return choice == QMessageBox::Discard;
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
