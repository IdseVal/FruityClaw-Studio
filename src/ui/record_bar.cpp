#include "ui/record_bar.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>

#include "core/sample_functions.h"
#include "ui/theme.h"

namespace ui {
namespace {

constexpr int kNoInput = -1;

// The meter's floor. Quieter than this reads as silence.
constexpr double kMeterFloorDb = -60.0;

int meter_percent(float peak) {
    if (peak <= 0.0f) return 0;
    double db = 20.0 * std::log10(static_cast<double>(peak));
    return static_cast<int>(std::clamp(100.0 * (1.0 - db / kMeterFloorDb), 0.0, 100.0));
}

QString format_length(std::int64_t frames, double sample_rate) {
    double seconds = sample_rate > 0.0 ? static_cast<double>(frames) / sample_rate : 0.0;
    int minutes = static_cast<int>(seconds / 60.0);
    return QString("%1:%2").arg(minutes).arg(seconds - minutes * 60.0, 4, 'f', 1, QChar('0'));
}

}  // namespace

RecordBar::RecordBar(core::ProjectHistory& history, core::RecorderPort& recorder,
                     QWidget* parent)
    : QWidget(parent), history_(history), recorder_(recorder) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(6);

    auto* label = new QLabel("Input", this);
    label->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(theme::kTextSecondary.name()));
    layout->addWidget(label);

    inputs_ = new QComboBox(this);
    inputs_->setToolTip("The audio input a take is recorded from");
    inputs_->setMinimumWidth(160);
    connect(inputs_, &QComboBox::activated, this, [this](int index) { choose_input(index); });
    layout->addWidget(inputs_);

    meter_ = new QProgressBar(this);
    meter_->setRange(0, 100);
    meter_->setTextVisible(false);
    meter_->setFixedSize(80, 10);
    meter_->setToolTip("Input level");
    meter_->setStyleSheet(QString("QProgressBar { background: %1; border: none; }"
                                  "QProgressBar::chunk { background: %2; }")
                              .arg(theme::kGridBar.name(), theme::kAccent.name()));
    layout->addWidget(meter_);

    record_button_ = new QToolButton(this);
    record_button_->setText("Record");
    record_button_->setToolTip("Record a take from the input (R)");
    record_button_->setShortcut(QKeySequence(Qt::Key_R));
    connect(record_button_, &QToolButton::clicked, this, [this] { toggle_recording(); });
    layout->addWidget(record_button_);

    length_label_ = new QLabel(this);
    length_label_->setStyleSheet(
        QString("color: %1; font-family: Consolas, monospace; font-size: 13px;")
            .arg(theme::kAccent.name()));
    length_label_->setMinimumWidth(48);
    layout->addWidget(length_label_);

    reload_inputs();

    auto* timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [this] { poll(); });
    timer->start();
    poll();
}

void RecordBar::reload_inputs() {
    inputs_->clear();
    std::vector<core::InputInfo> inputs = recorder_.inputs();
    if (inputs.empty()) {
        inputs_->addItem("No input available", kNoInput);
        inputs_->setEnabled(false);
        return;
    }
    inputs_->addItem("No input", kNoInput);
    for (const core::InputInfo& input : inputs) {
        QString text = QString::fromStdString(input.name);
        if (!input.backend_name.empty())
            text += QString(" (%1)").arg(QString::fromStdString(input.backend_name));
        inputs_->addItem(text, input.id);
    }
    inputs_->setEnabled(true);
    int selected = recorder_.selected_input();
    inputs_->setCurrentIndex(std::max(inputs_->findData(selected), 0));
}

void RecordBar::choose_input(int index) {
    int id = inputs_->itemData(index).toInt();
    std::string error = recorder_.select_input(id);
    if (error.empty()) {
        emit hint_changed(id == kNoInput ? "Input released."
                                         : QString("Recording from %1. Press R to record.")
                                               .arg(inputs_->itemText(index)));
    } else {
        emit hint_changed(QString::fromStdString(error));
        inputs_->setCurrentIndex(std::max(inputs_->findData(recorder_.selected_input()), 0));
    }
    poll();
}

void RecordBar::toggle_recording() {
    if (recorder_.status().recording) {
        finish_take();
    } else if (recorder_.start_recording()) {
        emit hint_changed("Recording. Press R to stop.");
    } else {
        emit hint_changed("Choose an input before recording.");
    }
    poll();
}

void RecordBar::finish_take() {
    core::RecorderStatus last = recorder_.status();
    core::SampleSource take = recorder_.stop_recording();
    if (!take) {
        emit hint_changed("Nothing was recorded.");
        return;
    }

    std::string name = next_take_name();
    auto added = core::functions::add_sample(history_.read(), name, take);
    if (!added.ok()) {
        emit hint_changed(QString::fromStdString(added.error));
        return;
    }
    history_.apply(std::move(added->delta));

    QString hint = QString("Recorded '%1' (%2 s).")
                       .arg(QString::fromStdString(name))
                       .arg(static_cast<double>(take->frame_count()) / take->sample_rate, 0, 'f', 1);
    if (last.dropped_frames > 0) {
        // Said plainly: a take with holes in it is not a usable take.
        hint += QString(" %1 frames were dropped; try a larger buffer.").arg(last.dropped_frames);
    }
    emit hint_changed(hint);
}

std::string RecordBar::next_take_name() const {
    const core::Project& project = history_.read();
    for (int n = 1;; ++n) {
        std::string candidate = "Take " + std::to_string(n);
        bool taken = std::any_of(project.samples.items.begin(), project.samples.items.end(),
                                 [&](const core::Sample& s) { return s.name == candidate; });
        if (!taken) return candidate;
    }
}

void RecordBar::poll() {
    core::RecorderStatus status = recorder_.status();
    meter_->setValue(meter_percent(status.peak));
    record_button_->setEnabled(status.input_open);
    record_button_->setText(status.recording ? "Stop Rec" : "Record");
    length_label_->setText(status.recording ? format_length(status.frames, status.sample_rate)
                                            : QString());
}

}  // namespace ui
