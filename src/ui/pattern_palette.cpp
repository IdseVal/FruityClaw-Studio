#include "ui/pattern_palette.h"

#include <QPainter>

#include "ui/theme.h"

namespace ui {

PatternPalette::PatternPalette(core::ProjectHistory& history, QWidget* parent)
    : QListWidget(parent), history_(history) {
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(QString("QListWidget { background: %1; color: %2; border: none;"
                          " padding: 4px; }"
                          "QListWidget::item { height: 28px; padding-left: 4px; }")
                      .arg(theme::kPanel.name(), theme::kTextPrimary.name()));

    connect(this, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { toggle_row(row(item)); });
    history_.observe([this] { reload(); });
    reload();
}

void PatternPalette::reload() {
    clear();
    const auto& patterns = history_.read().patterns.items;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const core::Pattern& pattern = patterns[i];
        QColor colour = pattern.colour ? theme::from_colour(*pattern.colour)
                                       : theme::hue(static_cast<int>(i));
        QPixmap swatch(12, 12);
        swatch.fill(Qt::transparent);
        {
            QPainter painter(&swatch);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setBrush(colour);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(0, 0, 12, 12, 3, 3);
        }
        auto* item = new QListWidgetItem(QIcon(swatch),
                                         QString::fromStdString(pattern.name), this);
        if (armed_ && *armed_ == pattern.id) {
            item->setForeground(theme::kAccent);
        }
    }

    // A deleted armed Pattern disarms.
    if (armed_ && !history_.read().patterns.find(*armed_)) {
        armed_.reset();
        emit armed_changed();
    }
}

void PatternPalette::toggle_row(int row) {
    const auto& patterns = history_.read().patterns.items;
    if (row < 0 || row >= static_cast<int>(patterns.size())) return;
    core::Id id = patterns[static_cast<std::size_t>(row)].id;
    if (armed_ && *armed_ == id) {
        armed_.reset();
    } else {
        armed_ = id;
    }
    reload();
    emit armed_changed();
}

}  // namespace ui
