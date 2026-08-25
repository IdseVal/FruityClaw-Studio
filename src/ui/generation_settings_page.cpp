#include "ui/generation_settings_page.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

#include "ui/theme.h"

namespace ui {
namespace {

// A ledger row: a fixed label in the left column, the entry's own words in
// the right. The labels are identical on every card; that is the point.
void ledger_row(QGridLayout* grid, int row, const QString& label, const std::string& text) {
    auto* key = new QLabel(label);
    key->setStyleSheet(QString("color: %1; font-size: 10px; letter-spacing: 1px;")
                           .arg(theme::kTextSecondary.name()));
    key->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto* value = new QLabel(QString::fromStdString(text));
    value->setWordWrap(true);
    // Wrap to the width given, never widen the page to fit a long sentence.
    value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    value->setStyleSheet("font-size: 12px;");
    grid->addWidget(key, row, 0);
    grid->addWidget(value, row, 1);
}

}  // namespace

GenerationSettingsPage::GenerationSettingsPage(core::GenerationSettingsPort& store,
                                               QWidget* parent)
    : QDialog(parent), store_(store), settings_(store.read()) {
    setWindowTitle("Music generation");
    setModal(true);
    resize(680, 720);
    setStyleSheet(
        QString("QDialog, QScrollArea, QScrollArea > QWidget > QWidget { background: %1; }"
                "QLabel { color: %2; font-size: 13px; }"
                "QLineEdit { background: %3; color: %2; border: 1px solid %4;"
                " padding: 6px 8px; font-family: Consolas, monospace; font-size: 12px; }"
                "QLineEdit:focus { border-color: %5; }"
                "QPushButton { color: %2; background: transparent; border: 1px solid %4;"
                " padding: 8px 16px; font-size: 13px; }"
                "QPushButton:hover { border-color: %5; color: %5; }"
                "QPushButton:disabled { color: %6; border-color: %3; }"
                "QRadioButton { color: %2; font-size: 14px; font-weight: 600; spacing: 8px; }"
                "QRadioButton:disabled { color: %6; }"
                "QCheckBox { color: %2; font-size: 13px; spacing: 8px; }"
                "QFrame#card { background: %3; border: 1px solid %4; }"
                "QFrame#card[chosen=\"true\"] { border-color: %5; }"
                "QFrame#warning { background: %3; border-left: 3px solid %5; }")
            .arg(theme::kPanel.name(), theme::kTextPrimary.name(), theme::kCanvas.name(),
                 theme::kGridBar.name(), theme::kAccent.name(), theme::kTextSecondary.name()));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    // --- guidance, when sent here from elsewhere ----------------------------
    guidance_ = new QLabel(body);
    guidance_->setObjectName("generation_guidance");
    guidance_->setWordWrap(true);
    guidance_->setStyleSheet(QString("color: %1; background: %2; padding: 10px 12px;")
                                 .arg(theme::kAccent.name(), theme::kCanvas.name()));
    guidance_->hide();
    layout->addWidget(guidance_);

    // --- eyebrow and state --------------------------------------------------
    auto* top = new QHBoxLayout;
    auto* eyebrow = new QLabel("MUSIC GENERATION", body);
    eyebrow->setStyleSheet(QString("color: %1; font-size: 10px; letter-spacing: 1px;")
                               .arg(theme::kTextSecondary.name()));
    top->addWidget(eyebrow);
    top->addStretch(1);
    state_chip_ = new QLabel(body);
    state_chip_->setObjectName("generation_state");
    top->addWidget(state_chip_);
    layout->addLayout(top);

    headline_ = new QLabel(body);
    headline_->setStyleSheet("font-size: 18px; font-weight: 600;");
    headline_->setWordWrap(true);
    layout->addWidget(headline_);

    auto* intro = new QLabel(
        "Nothing is downloaded and nothing is generated until you choose a model here. "
        "Every model on this list was checked by the project for whether you can release "
        "what it makes; each one trades something, so read the ledger, not the name.",
        body);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // --- the section 6.2 warning, always visible ---------------------------
    auto* warning = new QFrame(body);
    warning->setObjectName("warning");
    auto* warning_layout = new QVBoxLayout(warning);
    warning_layout->setContentsMargins(14, 10, 14, 10);
    auto* warning_text = new QLabel(
        "What you generate may not be licenseable. Purely AI-generated audio is generally "
        "not copyrightable, whichever model made it. Samples you bring in from the Studio's "
        "own libraries stay safe to release; generated ones carry a mark and no guarantee.",
        warning);
    warning_text->setObjectName("generation_warning");
    warning_text->setWordWrap(true);
    warning_layout->addWidget(warning_text);
    layout->addWidget(warning);
    layout->addSpacing(6);

    // --- the handpicked list -------------------------------------------------
    for (const core::GenerationModel& model : core::handpicked_models()) {
        layout->addWidget(make_card(model));
    }

    // A previously chosen model that has since left the list: say why the
    // Studio switched generation off rather than silently forgetting.
    if (!settings_.model.empty() && !core::find_model(settings_.model)) {
        auto* removed = new QLabel(
            QString("'%1' is no longer on the list, so generation was turned off. "
                    "Choose another model to turn it on again.")
                .arg(QString::fromStdString(settings_.model)),
            body);
        removed->setObjectName("generation_removed");
        removed->setWordWrap(true);
        removed->setStyleSheet(QString("color: %1;").arg(theme::kAccent.name()));
        layout->addWidget(removed);
    }
    layout->addSpacing(6);

    // --- acknowledgement and the two answers ---------------------------------
    acknowledge_ = new QCheckBox(
        "I understand that what I generate may not be licenseable.", body);
    acknowledge_->setObjectName("generation_acknowledge");
    acknowledge_->setChecked(settings_.enabled);
    layout->addWidget(acknowledge_);

    problem_ = new QLabel(body);
    problem_->setObjectName("generation_problem");
    problem_->setWordWrap(true);
    problem_->setStyleSheet(QString("color: %1;").arg(theme::kAccent.name()));
    problem_->hide();
    layout->addWidget(problem_);

    auto* buttons = new QHBoxLayout;
    off_button_ = new QPushButton("Turn off", body);
    off_button_->setObjectName("generation_off");
    off_button_->setAutoDefault(false);
    connect(off_button_, &QPushButton::clicked, this, &GenerationSettingsPage::turn_off);
    buttons->addWidget(off_button_);
    buttons->addStretch(1);

    auto* close = new QPushButton("Close", body);
    close->setAutoDefault(false);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(close);

    on_button_ = new QPushButton(body);
    on_button_->setObjectName("generation_on");
    on_button_->setDefault(true);
    connect(on_button_, &QPushButton::clicked, this, &GenerationSettingsPage::turn_on);
    buttons->addWidget(on_button_);
    layout->addLayout(buttons);
    layout->addStretch(1);

    refresh_state();
}

QWidget* GenerationSettingsPage::make_card(const core::GenerationModel& model) {
    auto* card = new QFrame(this);
    card->setObjectName("card");
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(16, 12, 16, 14);
    card_layout->setSpacing(8);

    Card entry;
    entry.model = &model;

    auto* head = new QHBoxLayout;
    entry.radio = new QRadioButton(QString::fromStdString(model.name), card);
    entry.radio->setObjectName(QString("generation_model_%1").arg(model.id.c_str()));
    entry.radio->setEnabled(model.available);
    entry.radio->setChecked(settings_.model == model.id);
    head->addWidget(entry.radio);
    head->addStretch(1);
    const bool local = model.path == core::GenerationPath::Local;
    auto* path_tag = new QLabel(local ? "ON THIS COMPUTER · NO API KEY"
                                      : "REMOTE · YOUR OWN API KEY",
                                card);
    path_tag->setStyleSheet(QString("color: %1; font-size: 10px; letter-spacing: 1px;")
                                .arg(theme::kTextSecondary.name()));
    head->addWidget(path_tag);
    card_layout->addLayout(head);

    // The ledger. Same four rows on every card, in the same order.
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(4);
    grid->setColumnMinimumWidth(0, 120);
    grid->setColumnStretch(1, 1);
    ledger_row(grid, 0, "RUNS", model.runs);
    ledger_row(grid, 1, "LEAVES THIS MACHINE", model.leaves_machine);
    ledger_row(grid, 2, "OUTPUT RIGHTS", model.output_rights);
    ledger_row(grid, 3, "CONDITIONS", model.conditions);
    card_layout->addLayout(grid);

    // The credential the path needs, on the card that needs it.
    if (model.available) {
        auto* credential = new QHBoxLayout;
        if (local) {
            entry.weights_field = new QLineEdit(card);
            entry.weights_field->setObjectName(
                QString("generation_weights_%1").arg(model.id.c_str()));
            entry.weights_field->setPlaceholderText("Folder holding the downloaded weights");
            entry.weights_field->setAccessibleName(
                QString("%1 weights folder").arg(model.name.c_str()));
            if (settings_.model == model.id) {
                entry.weights_field->setText(QString::fromStdString(settings_.weights_path));
            }
            credential->addWidget(entry.weights_field, 1);
            auto* browse = new QPushButton("Choose folder", card);
            browse->setAutoDefault(false);
            QLineEdit* field = entry.weights_field;
            connect(browse, &QPushButton::clicked, this, [this, field] {
                QString dir = QFileDialog::getExistingDirectory(this, "Weights folder",
                                                                field->text());
                if (!dir.isEmpty()) field->setText(dir);
            });
            credential->addWidget(browse);
        } else {
            entry.key_field = new QLineEdit(card);
            entry.key_field->setObjectName(QString("generation_key_%1").arg(model.id.c_str()));
            entry.key_field->setEchoMode(QLineEdit::Password);
            entry.key_field->setPlaceholderText("Paste your API key");
            entry.key_field->setAccessibleName(QString("%1 API key").arg(model.name.c_str()));
            credential->addWidget(entry.key_field, 1);
            entry.key_state = new QLabel(card);
            entry.key_state->setObjectName(
                QString("generation_key_state_%1").arg(model.id.c_str()));
            entry.key_state->setStyleSheet(
                QString("color: %1; font-size: 11px;").arg(theme::kTextSecondary.name()));
            credential->addWidget(entry.key_state);
        }
        card_layout->addLayout(credential);

        // Typing in a card's field is choosing that card.
        QRadioButton* radio = entry.radio;
        QLineEdit* field = local ? entry.weights_field : entry.key_field;
        connect(field, &QLineEdit::textEdited, this, [radio] { radio->setChecked(true); });
    }

    connect(entry.radio, &QRadioButton::toggled, this, [this] { refresh_state(); });
    cards_.push_back(entry);
    return card;
}

const GenerationSettingsPage::Card* GenerationSettingsPage::selected_card() const {
    for (const Card& card : cards_) {
        if (card.radio->isChecked()) return &card;
    }
    return nullptr;
}

void GenerationSettingsPage::show_guidance(const QString& text) {
    guidance_->setText(text);
    guidance_->show();
}

// Everything derived from settings_ and the current selection.
void GenerationSettingsPage::refresh_state() {
    const core::GenerationModel* on = settings_.enabled ? core::find_model(settings_.model)
                                                        : nullptr;
    if (on) {
        state_chip_->setText(QString("ON · %1").arg(on->name.c_str()));
        state_chip_->setStyleSheet(
            QString("color: %1; font-size: 10px; letter-spacing: 1px; font-weight: 600;")
                .arg(theme::kAccent.name()));
        headline_->setText(QString("Music generation is on, with %1.").arg(on->name.c_str()));
    } else {
        state_chip_->setText("OFF");
        state_chip_->setStyleSheet(
            QString("color: %1; font-size: 10px; letter-spacing: 1px; font-weight: 600;")
                .arg(theme::kTextSecondary.name()));
        headline_->setText("Music generation is off.");
    }
    off_button_->setVisible(settings_.enabled);

    const Card* chosen = selected_card();
    for (const Card& card : cards_) {
        QWidget* frame = card.radio->parentWidget();
        frame->setProperty("chosen", &card == chosen);
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
        if (card.key_state) {
            const bool kept = store_.has_key() && settings_.model == card.model->id;
            card.key_state->setText(kept ? "A key is kept. Paste to replace it." : "");
            card.key_field->setPlaceholderText(kept ? "Key kept" : "Paste your API key");
        }
    }
    on_button_->setText(chosen && on != chosen->model
                            ? QString("Turn on with %1").arg(chosen->model->name.c_str())
                            : "Turn on");
    problem_->hide();
}

void GenerationSettingsPage::say(const QString& problem) {
    problem_->setText(problem);
    problem_->show();
}

void GenerationSettingsPage::turn_on() {
    const Card* chosen = selected_card();
    core::GenerationSettings requested;
    requested.enabled = true;
    if (chosen) {
        requested.model = chosen->model->id;
        if (chosen->weights_field) {
            requested.weights_path = chosen->weights_field->text().trimmed().toStdString();
        }
        // A pasted key is kept before the rules run, so "no key" only means
        // the user pasted nothing. Pasted keys arrive with whitespace around.
        if (chosen->key_field) {
            std::string key = chosen->key_field->text().trimmed().toStdString();
            if (!key.empty() && !store_.store_key(key)) {
                say("The key could not be saved. Check that your user settings folder "
                    "is writable and try again.");
                return;
            }
        }
    }

    // The key counts when it was just pasted, or was kept for this same model.
    const bool has_key =
        chosen && chosen->key_field &&
        (!chosen->key_field->text().trimmed().isEmpty() ||
         (store_.has_key() && settings_.model == requested.model));
    if (std::optional<std::string> problem =
            core::enable_problem(requested, has_key, acknowledge_->isChecked())) {
        say(QString::fromStdString(*problem));
        return;
    }
    // Switching to a Local model leaves no key behind.
    if (!chosen->key_field) store_.clear_key();

    if (!store_.write(requested)) {
        say("The setting could not be saved. Check that your user settings folder is "
            "writable and try again.");
        return;
    }
    settings_ = requested;
    if (chosen->key_field) chosen->key_field->clear();
    refresh_state();
    emit changed();
}

// Off keeps the choice and the key: a user turning it off for a session
// should not have to paste everything again to turn it back on.
void GenerationSettingsPage::turn_off() {
    core::GenerationSettings requested = settings_;
    requested.enabled = false;
    if (!store_.write(requested)) {
        say("The setting could not be saved. Check that your user settings folder is "
            "writable and try again.");
        return;
    }
    settings_ = requested;
    refresh_state();
    emit changed();
}

}  // namespace ui
