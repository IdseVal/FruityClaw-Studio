// The settings page where music generation is turned on (core document
// section 6.4). It opens on a Studio that is complete without it: off, no
// model, nothing downloaded. The user picks one entry from the handpicked
// list, imports weights or pastes a key, confirms the section 6.2 warning,
// and turns generation on — every one of those rules is enforced by
// core::enable_problem, not here, so the page cannot enable by accident.
//
// Each entry is laid out as the same four-row rights ledger (runs where,
// what leaves the machine, output rights, conditions) so the user compares
// trade-offs row by row instead of reading a ranking (ADR-003).
//
// Reached from the Studio menu, and from any surface that gates on
// generation: when a user tries to generate with it off they are guided
// here, told why, rather than shown a dead control.
#pragma once

#include <QDialog>
#include <vector>

#include "core/generation.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace ui {

class GenerationSettingsPage : public QDialog {
    Q_OBJECT

public:
    explicit GenerationSettingsPage(core::GenerationSettingsPort& store,
                                    QWidget* parent = nullptr);

    // Shown above the list when the user was sent here from somewhere
    // else: what they tried, and that turning generation on is the way.
    void show_guidance(const QString& text);

signals:
    // Generation was turned on or off and the change was kept.
    void changed();

private:
    // One handpicked entry's card: its radio, and the credential field its
    // path needs. Only one of the two fields exists per card.
    struct Card {
        const core::GenerationModel* model = nullptr;
        QRadioButton* radio = nullptr;
        QLineEdit* weights_field = nullptr;  // Local
        QLineEdit* key_field = nullptr;      // Remote
        QLabel* key_state = nullptr;         // Remote: whether a key is kept
    };

    QWidget* make_card(const core::GenerationModel& model);
    const Card* selected_card() const;
    void refresh_state();
    void turn_on();
    void turn_off();
    void say(const QString& problem);

    core::GenerationSettingsPort& store_;
    core::GenerationSettings settings_;
    std::vector<Card> cards_;

    QLabel* guidance_ = nullptr;
    QLabel* state_chip_ = nullptr;
    QLabel* headline_ = nullptr;
    QCheckBox* acknowledge_ = nullptr;
    QLabel* problem_ = nullptr;
    QPushButton* off_button_ = nullptr;
    QPushButton* on_button_ = nullptr;
};

}  // namespace ui
