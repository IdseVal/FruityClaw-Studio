#include "ui/main_window.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include "ui/arrangement_view.h"
#include "ui/pattern_palette.h"
#include "ui/record_bar.h"
#include "ui/sample_browser.h"
#include "ui/settings_dialog.h"
#include "ui/theme.h"

namespace ui {
namespace {

// The sidebar's section eyebrow: what the panel below it holds.
QLabel* section_header(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setStyleSheet(QString("color: %1; background: %2; font-size: 10px;"
                                 " letter-spacing: 1px; padding: 6px 8px 2px 8px;")
                             .arg(theme::kTextSecondary.name(), theme::kPanel.name()));
    return label;
}

}  // namespace

MainWindow::MainWindow(core::ProjectHistory& history, core::TransportPort& transport,
                       core::AuditionPort& audition, core::RecorderPort& recorder,
                       assistant::FunctionToggles& toggles,
                       core::GenerationSettingsPort& generation, const QString& product_name,
                       QWidget* parent)
    : QMainWindow(parent), history_(history), transport_(transport), toggles_(toggles),
      generation_(generation) {
    setWindowTitle(product_name);
    resize(1200, 640);
    setStyleSheet(QString("QMainWindow, QToolBar, QStatusBar, QMenuBar, QMenu { "
                          "background: %1; color: %2; border: none; }"
                          "QMenuBar::item:selected, QMenu::item:selected { color: %3; }"
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

    record_bar_ = new RecordBar(history_, recorder, bar);
    bar->addWidget(record_bar_);
    connect(record_bar_, &RecordBar::hint_changed, this,
            [this](const QString& hint) { statusBar()->showMessage(hint); });

    // Present whether or not generation is on: the gate is behind it, not
    // in front of it (core document 6.4).
    generate_action_ = new QAction("Generate", this);
    generate_action_->setToolTip("Generate a Sample from a description");
    connect(generate_action_, &QAction::triggered, this, &MainWindow::request_generation);
    auto* generate_button = new QToolButton(bar);
    generate_button->setDefaultAction(generate_action_);
    bar->addWidget(generate_button);

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

    auto* settings_action = new QAction("Settings", this);
    settings_action->setShortcut(QKeySequence::Preferences);
    connect(settings_action, &QAction::triggered, this, [this] { open_settings(); });
    addAction(settings_action);
    auto* settings_button = new QToolButton(bar);
    settings_button->setDefaultAction(settings_action);
    bar->addWidget(settings_button);

    // --- centre: sidebar (Samples over Patterns) | arrangement ---------------
    auto* splitter = new QSplitter(this);

    auto* sidebar = new QSplitter(Qt::Vertical, splitter);
    auto* samples_section = new QWidget(sidebar);
    samples_ = new SampleBrowser(history_, audition, samples_section);
    auto* samples_layout = new QVBoxLayout(samples_section);
    samples_layout->setContentsMargins(0, 0, 0, 0);
    samples_layout->setSpacing(0);
    samples_layout->addWidget(section_header("Samples", samples_section));
    samples_layout->addWidget(samples_, 1);

    auto* patterns_section = new QWidget(sidebar);
    palette_ = new PatternPalette(history_, patterns_section);
    auto* patterns_layout = new QVBoxLayout(patterns_section);
    patterns_layout->setContentsMargins(0, 0, 0, 0);
    patterns_layout->setSpacing(0);
    patterns_layout->addWidget(section_header("Patterns", patterns_section));
    patterns_layout->addWidget(palette_, 1);

    sidebar->addWidget(samples_section);
    sidebar->addWidget(patterns_section);
    sidebar->setSizes({360, 240});

    arrangement_view_ = new ArrangementView(history_, transport_, splitter);
    splitter->addWidget(sidebar);
    splitter->addWidget(arrangement_view_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 980});
    setCentralWidget(splitter);

    connect(samples_, &SampleBrowser::hint_changed, this,
            [this](const QString& hint) { statusBar()->showMessage(hint); });

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

assistant::Capabilities MainWindow::capabilities() const {
    core::GenerationSettings settings = generation_.read();
    return {settings.enabled && core::find_model(settings.model) != nullptr};
}

// The dialog outlives this call (open, not exec) so a surface that sent the
// user here is not blocked behind it, and so the guidance can be read back.
void MainWindow::open_settings(const QString& guidance) {
    auto* dialog = new SettingsDialog(toggles_, capabilities(), generation_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SettingsDialog::toggles_changed, this, &MainWindow::toggles_changed);
    connect(dialog, &SettingsDialog::generation_changed, this, [this] {
        core::GenerationSettings settings = generation_.read();
        const core::GenerationModel* model = core::find_model(settings.model);
        statusBar()->showMessage(settings.enabled && model
                                     ? QString("Music generation is on, with %1.")
                                           .arg(model->name.c_str())
                                     : "Music generation is off.");
    });
    if (!guidance.isEmpty()) dialog->show_generation(guidance);
    dialog->open();
}

void MainWindow::request_generation() {
    core::GenerationSettings settings = generation_.read();
    const core::GenerationModel* model = core::find_model(settings.model);
    if (!settings.enabled || !model) {
        open_settings(
            "You asked to generate a Sample, and music generation is off. It is off on "
            "every fresh install: nothing is downloaded until you choose a model below.");
        return;
    }
    // The generator behind the chosen model is a later issue; the gate is
    // what this surface owns.
    statusBar()->showMessage(
        QString("Music generation is on, with %1. Generating Samples is not built yet.")
            .arg(model->name.c_str()));
}

void MainWindow::refresh_transport() {
    core::PlaybackStatus status = transport_.status();
    const core::MusicalContent& content = history_.read().musical;
    core::Ticks bar_len =
        static_cast<core::Ticks>(content.time_signature.first) * core::kPpq;
    long long bar = status.position / bar_len + 1;
    long long beat = status.position % bar_len / core::kPpq + 1;
    position_label_->setText(QString("  %1.%2  ")
                                 .arg(bar, 3, 10, QChar('0'))
                                 .arg(beat));
    tempo_label_->setText(QString("%1 BPM").arg(content.tempo));
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
