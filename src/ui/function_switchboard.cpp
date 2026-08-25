#include "ui/function_switchboard.h"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "ui/theme.h"

namespace ui {
namespace {

QString qs(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// The eyebrow above each territory, matching the sidebar's section headers.
QLabel* territory_header(assistant::Territory territory, QWidget* parent) {
    auto* label = new QLabel(qs(assistant::territory_label(territory)).toUpper(), parent);
    label->setStyleSheet(QString("color: %1; font-size: 10px; letter-spacing: 1px;"
                                 " padding: 14px 0 4px 0;")
                             .arg(theme::kTextSecondary.name()));
    return label;
}

// The class label. Text carries the meaning; colour only echoes it, so a
// user who cannot tell amber from grey still reads the same thing.
QLabel* class_pill(const assistant::FunctionDescriptor& function, QWidget* parent) {
    bool rework = function.function_class == assistant::FunctionClass::Rework;
    auto* pill = new QLabel(qs(assistant::class_label(function.function_class)), parent);
    pill->setToolTip(rework ? "Sends the notes of one Pattern to the model."
                            : "Sends your prompt only. No Project content leaves.");
    QColor colour = rework ? theme::kAccent : theme::kTextSecondary;
    pill->setStyleSheet(QString("color: %1; border: 1px solid %1; border-radius: 3px;"
                                " padding: 1px 6px; font-size: 10px; letter-spacing: 1px;")
                            .arg(colour.name()));
    pill->setAlignment(Qt::AlignCenter);
    return pill;
}

QString switch_style() {
    return QString("QCheckBox { spacing: 0; }"
                   "QCheckBox::indicator { width: 30px; height: 16px; border-radius: 8px;"
                   " background: %1; border: 1px solid %2; }"
                   "QCheckBox::indicator:checked { background: %3; border-color: %3; }"
                   "QCheckBox::indicator:focus { border-color: %4; }"
                   "QCheckBox:focus { outline: none; }")
        .arg(theme::kGridBar.name(), theme::kTextSecondary.name(), theme::kAccent.name(),
             theme::kTextPrimary.name());
}

}  // namespace

FunctionSwitchboard::FunctionSwitchboard(assistant::FunctionToggles& toggles,
                                         assistant::Capabilities capabilities, QWidget* parent)
    : QWidget(parent), toggles_(toggles), capabilities_(capabilities) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* intro = new QLabel(
        "The Assistant works the Studio through these Functions. Switch one off and the "
        "Assistant no longer has it; you keep it. Directive Functions send your prompt only. "
        "Rework Functions send the notes of one Pattern to the model.",
        this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QString("color: %1; font-size: 12px; padding: 0 0 12px 0;")
                             .arg(theme::kTextPrimary.name()));
    outer->addWidget(intro);

    // --- the banner: what the built file says about this machine ----------
    banner_ = new QFrame(this);
    banner_->setObjectName("local_only_banner");
    auto* banner_layout = new QGridLayout(banner_);
    banner_layout->setContentsMargins(12, 10, 12, 10);
    banner_layout->setHorizontalSpacing(10);
    banner_layout->setVerticalSpacing(2);
    banner_dot_ = new QLabel("●", banner_);
    banner_dot_->setStyleSheet("font-size: 14px;");
    banner_title_ = new QLabel(banner_);
    banner_title_->setStyleSheet(
        QString("color: %1; font-size: 13px; font-weight: 600;").arg(theme::kTextPrimary.name()));
    banner_detail_ = new QLabel(banner_);
    banner_detail_->setWordWrap(true);
    banner_detail_->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(theme::kTextSecondary.name()));
    banner_layout->addWidget(banner_dot_, 0, 0, 2, 1, Qt::AlignTop);
    banner_layout->addWidget(banner_title_, 0, 1);
    banner_layout->addWidget(banner_detail_, 1, 1);
    banner_layout->setColumnStretch(1, 1);
    outer->addWidget(banner_);

    // --- the rows, one per registry entry, grouped by territory -----------
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    auto* body = new QWidget(scroll);
    body->setStyleSheet(QString("background: %1;").arg(theme::kPanel.name()));
    auto* rows = new QGridLayout(body);
    rows->setContentsMargins(0, 0, 8, 0);
    rows->setHorizontalSpacing(12);
    rows->setVerticalSpacing(6);
    rows->setColumnStretch(1, 1);

    int row = 0;
    std::optional<assistant::Territory> current;
    for (const assistant::FunctionDescriptor& function : assistant::registry()) {
        if (current != function.territory) {
            current = function.territory;
            rows->addWidget(territory_header(function.territory, body), row++, 0, 1, 4);
        }

        auto* toggle = new QCheckBox(body);
        toggle->setObjectName(qs(function.name));
        toggle->setAccessibleName(qs(function.effect));
        toggle->setStyleSheet(switch_style());
        toggle->setCursor(Qt::PointingHandCursor);
        switches_.push_back(toggle);
        rows->addWidget(toggle, row, 0, Qt::AlignTop);

        auto* effect = new QLabel(qs(function.effect), body);
        effect->setWordWrap(true);
        effect->setStyleSheet(QString("color: %1; font-size: 12px;").arg(theme::kTextPrimary.name()));
        auto* name = new QLabel(qs(function.name), body);
        name->setStyleSheet(QString("color: %1; font-family: Consolas, monospace;"
                                    " font-size: 11px;")
                                .arg(theme::kTextSecondary.name()));
        auto* text = new QWidget(body);
        auto* text_layout = new QVBoxLayout(text);
        text_layout->setContentsMargins(0, 0, 0, 0);
        text_layout->setSpacing(1);
        text_layout->addWidget(effect);
        text_layout->addWidget(name);
        if (function.requires_music_generation && !capabilities_.music_generation_enabled) {
            auto* gated = new QLabel("Not offered until you enable music generation.", body);
            gated->setStyleSheet(
                QString("color: %1; font-size: 11px;").arg(theme::kTextSecondary.name()));
            text_layout->addWidget(gated);
        }
        rows->addWidget(text, row, 1);

        rows->addWidget(class_pill(function, body), row, 2, Qt::AlignTop);

        // O-17.2: which provider a call reaches, per row.
        auto* provider = new QLabel(qs(assistant::provider_label(function.provider)), body);
        provider->setStyleSheet(
            QString("color: %1; font-size: 11px;").arg(theme::kTextSecondary.name()));
        rows->addWidget(provider, row, 3, Qt::AlignTop);

        connect(toggle, &QCheckBox::toggled, this, [this, &function](bool on) {
            // Section 4.3 defaults an unset Rework Function off once any is
            // off, so that an update cannot re-open a closed privacy floor.
            // That rule is for Functions the store has never heard of; the
            // rows on this board are known, so pin what they show before
            // the write — one click changes one row.
            if (function.function_class == assistant::FunctionClass::Rework) {
                for (const assistant::FunctionDescriptor& other : assistant::registry()) {
                    if (other.function_class == assistant::FunctionClass::Rework &&
                        !toggles_.state(other.name)) {
                        toggles_.set_enabled(other.name, assistant::toggle_allows(other, toggles_));
                    }
                }
            }
            toggles_.set_enabled(function.name, on);
            refresh();
            emit changed();
        });
        ++row;
    }
    rows->setRowStretch(row, 1);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    refresh();
}

void FunctionSwitchboard::refresh() {
    file_ = assistant::build(assistant::registry(), toggles_, capabilities_);

    // Switch positions follow the store's defaults (spec section 4.3), so a
    // Rework row that defaults off after an update shows off.
    std::span<const assistant::FunctionDescriptor> functions = assistant::registry();
    for (std::size_t i = 0; i < switches_.size(); ++i) {
        QSignalBlocker quiet(switches_[i]);
        switches_[i]->setChecked(assistant::toggle_allows(functions[i], toggles_));
    }

    std::size_t offered = file_.entries.size();
    if (file_.local_only()) {
        banner_dot_->setStyleSheet(QString("color: %1; font-size: 14px;").arg(theme::kAccent.name()));
        banner_title_->setText("Local only — no Project content can leave this machine.");
        banner_detail_->setText(
            QString("All Rework Functions are off. %1 Directive Functions are offered; they send "
                    "your prompt only.")
                .arg(offered));
        banner_->setStyleSheet(QString("QFrame#local_only_banner { background: %1;"
                                       " border: 1px solid %2; border-radius: 4px; }")
                                   .arg(theme::kCanvas.name(), theme::kAccent.name()));
    } else {
        banner_dot_->setStyleSheet(
            QString("color: %1; font-size: 14px;").arg(theme::kTextSecondary.name()));
        banner_title_->setText(QString("%1 Rework %2 on — one Pattern's notes can be sent "
                                       "to the model when the Assistant uses %3.")
                                   .arg(file_.rework_count)
                                   .arg(file_.rework_count == 1 ? "Function is" : "Functions are")
                                   .arg(file_.rework_count == 1 ? "it" : "one"));
        banner_detail_->setText(
            QString("%1 Functions are offered. Switch every Rework Function off and nothing "
                    "from the Project leaves this machine.")
                .arg(offered));
        banner_->setStyleSheet(QString("QFrame#local_only_banner { background: %1;"
                                       " border: 1px solid %2; border-radius: 4px; }")
                                   .arg(theme::kCanvas.name(), theme::kGridBar.name()));
    }
    banner_->setAccessibleName(banner_title_->text());
}

}  // namespace ui
