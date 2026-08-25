#include "ui/pattern_palette.h"

#include <QPainter>

#include "ui/provenance.h"
#include "ui/theme.h"

namespace ui {
namespace {

// Wide enough for the provenance mark to sit in a corner of the swatch.
constexpr int kSwatchWidth = 26;
constexpr int kSwatchHeight = 14;

QPixmap swatch(const QColor& colour, bool marked) {
    QPixmap pixmap(kSwatchWidth, kSwatchHeight);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(colour);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(0, 0, kSwatchWidth, kSwatchHeight, 3, 3);
    if (marked) {
        paint_provenance_mark(painter, QRect(kSwatchWidth - 13, kSwatchHeight - 10, 12, 9));
    }
    return pixmap;
}

}  // namespace

PatternPalette::PatternPalette(core::ProjectHistory& history, QWidget* parent)
    : QListWidget(parent), history_(history) {
    setSelectionMode(QAbstractItemView::NoSelection);
    setIconSize(QSize(kSwatchWidth, kSwatchHeight));
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
        bool marked = !pattern.provenance.is_human();
        auto* item = new QListWidgetItem(QIcon(swatch(colour, marked)),
                                         QString::fromStdString(pattern.name), this);
        // The mark must reach a screen reader as well as the eye.
        item->setData(Qt::AccessibleDescriptionRole,
                      marked ? kProvenanceMarkLabel : QString());
        item->setToolTip(provenance_text(pattern.provenance));
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
