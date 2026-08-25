#include "ui/settings_dialog.h"

#include <QDialogButtonBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "ui/function_switchboard.h"
#include "ui/generation_settings_page.h"
#include "ui/theme.h"

namespace ui {

SettingsDialog::SettingsDialog(assistant::FunctionToggles& toggles,
                               assistant::Capabilities capabilities,
                               core::GenerationSettingsPort& generation, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Settings");
    resize(760, 620);
    setStyleSheet(QString("QDialog, QTabWidget::pane { background: %1; color: %2; border: none; }"
                          "QTabBar::tab { background: %1; color: %3; padding: 8px 16px;"
                          " border-bottom: 2px solid transparent; }"
                          "QTabBar::tab:selected { color: %2; border-bottom: 2px solid %4; }"
                          "QLabel { background: transparent; }"
                          "QPushButton { color: %2; background: %5; border: 1px solid %5;"
                          " border-radius: 3px; padding: 6px 14px; }"
                          "QPushButton:hover, QPushButton:focus { border-color: %4; }")
                      .arg(theme::kPanel.name(), theme::kTextPrimary.name(),
                           theme::kTextSecondary.name(), theme::kAccent.name(),
                           theme::kGridBar.name()));

    tabs_ = new QTabWidget(this);
    QTabWidget* tabs = tabs_;
    auto* functions_tab = new QWidget(tabs);
    auto* functions_layout = new QVBoxLayout(functions_tab);
    functions_layout->setContentsMargins(16, 16, 16, 8);
    switchboard_ = new FunctionSwitchboard(toggles, capabilities, functions_tab);
    functions_layout->addWidget(switchboard_);
    tabs->addTab(functions_tab, "Assistant Functions");

    generation_ = new GenerationSettingsPage(generation, tabs);
    tabs->addTab(generation_, "Music generation");

    // Every switch takes effect as it is flipped; there is nothing to apply.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 12, 12);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);

    connect(switchboard_, &FunctionSwitchboard::changed, this, &SettingsDialog::toggles_changed);
    connect(generation_, &GenerationSettingsPage::changed, this,
            &SettingsDialog::generation_changed);
}

void SettingsDialog::show_generation(const QString& guidance) {
    generation_->show_guidance(guidance);
    tabs_->setCurrentWidget(generation_);
}

}  // namespace ui
