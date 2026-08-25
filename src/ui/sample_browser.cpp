#include "ui/sample_browser.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/sample_functions.h"
#include "ui/provenance.h"
#include "ui/theme.h"

namespace ui {
namespace {

constexpr int kThumbWidth = 36;
constexpr int kThumbHeight = 22;

QString duration_text(const core::AudioData& audio) {
    double seconds = audio.sample_rate > 0.0
                         ? static_cast<double>(audio.frame_count()) / audio.sample_rate
                         : 0.0;
    return QString::number(seconds, 'f', seconds < 10.0 ? 2 : 1) + " s";
}

// The peak envelope of the first channel, one column per pixel. Bounded per
// column so a long Sample costs no more than a short one.
void paint_waveform(QPainter& painter, const core::AudioData& audio, const QRect& rect,
                    const QColor& colour) {
    std::int64_t frames = audio.frame_count();
    if (frames < 2 || rect.width() < 1) return;
    double frames_per_column = static_cast<double>(frames) / rect.width();
    std::int64_t stride = std::max<std::int64_t>(1, static_cast<std::int64_t>(frames_per_column / 64));
    int mid = rect.top() + rect.height() / 2;
    painter.setPen(colour);
    for (int x = 0; x < rect.width(); ++x) {
        std::int64_t begin = static_cast<std::int64_t>(x * frames_per_column);
        std::int64_t end = std::min(frames, static_cast<std::int64_t>((x + 1) * frames_per_column));
        float peak = 0.0f;
        for (std::int64_t f = begin; f < end; f += stride) {
            peak = std::max(peak, std::fabs(audio.frames[static_cast<std::size_t>(f) *
                                                         static_cast<std::size_t>(audio.channels)]));
        }
        int half = std::max(1, static_cast<int>(peak * (rect.height() / 2 - 1)));
        painter.drawLine(rect.left() + x, mid - half, rect.left() + x, mid + half);
    }
}

QPixmap thumbnail(const core::Sample& sample, const QColor& colour) {
    // Ids are never reused and sources are immutable, so the Id plus the
    // mark identifies the picture for good; a pointer key would not, since
    // a freed source's address can come back under another Sample.
    struct Cached {
        bool marked;
        QPixmap pixmap;
    };
    static std::unordered_map<core::Id, Cached> cache;
    bool marked = !sample.provenance.is_human();
    auto cached = cache.find(sample.id);
    if (cached != cache.end() && cached->second.marked == marked) return cached->second.pixmap;

    QPixmap pixmap(kThumbWidth, kThumbHeight);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setBrush(theme::kCanvas);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(0, 0, kThumbWidth, kThumbHeight, 3, 3);
        if (sample.source) {
            paint_waveform(painter, *sample.source, QRect(2, 2, kThumbWidth - 4, kThumbHeight - 4),
                           colour);
        }
        if (marked) {
            provenance::paint_mark(painter, QRect(kThumbWidth - 13, kThumbHeight - 10, 12, 9));
        }
    }
    cache[sample.id] = Cached{marked, pixmap};
    return pixmap;
}

// "When it is opened up" (section 6.3): the Sample at full width with its
// provenance stated in words.
void show_details(const core::Sample& sample, const QColor& colour, QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QString::fromStdString(sample.name));
    dialog->setStyleSheet(QString("QDialog { background: %1; } QLabel { color: %2; }")
                              .arg(theme::kPanel.name(), theme::kTextPrimary.name()));

    QPixmap picture(320, 72);
    picture.fill(theme::kCanvas);
    {
        QPainter painter(&picture);
        if (sample.source) paint_waveform(painter, *sample.source, picture.rect().adjusted(4, 4, -4, -4), colour);
        if (!sample.provenance.is_human()) provenance::paint_mark(painter, QRect(300, 56, 16, 12));
    }
    auto* waveform = new QLabel(dialog);
    waveform->setPixmap(picture);

    auto* name = new QLabel(QString::fromStdString(sample.name), dialog);
    name->setStyleSheet("font-size: 15px; font-weight: 600;");

    QString facts = sample.source
                        ? QString("%1 · %2 · %3 Hz")
                              .arg(duration_text(*sample.source))
                              .arg(sample.source->channels == 1 ? "mono" : "stereo")
                              .arg(sample.source->sample_rate)
                        : "No audio";
    auto* details = new QLabel(facts, dialog);
    details->setStyleSheet(QString("color: %1;").arg(theme::kTextSecondary.name()));

    auto* provenance_label = new QLabel(provenance::text(sample.provenance), dialog);
    provenance_label->setWordWrap(true);
    provenance_label->setObjectName("provenance");
    if (!sample.provenance.is_human()) {
        provenance_label->setStyleSheet(QString("color: %1;").arg(theme::kAccent.name()));
    }

    auto* layout = new QVBoxLayout(dialog);
    layout->setSpacing(8);
    layout->addWidget(name);
    layout->addWidget(waveform);
    layout->addWidget(details);
    layout->addWidget(provenance_label);
    dialog->show();
}

}  // namespace

SampleBrowser::SampleBrowser(core::ProjectHistory& history, core::AuditionPort& audition,
                             QWidget* parent)
    : QWidget(parent), history_(history), audition_(audition) {
    setStyleSheet(QString("QWidget { background: %1; color: %2; }"
                          "QLineEdit { background: %3; border: none; border-radius: 3px;"
                          " padding: 4px 6px; }"
                          "QListWidget { border: none; outline: none; }"
                          "QListWidget::item { height: 32px; padding-left: 2px; }"
                          "QListWidget::item:selected { background: %4; color: %5; }"
                          "QToolButton { border: none; padding: 5px 8px; text-align: left; }"
                          "QToolButton:hover { color: %5; }"
                          "QToolButton:disabled { color: %6; }"
                          "QToolButton::menu-indicator { image: none; }")
                      .arg(theme::kPanel.name(), theme::kTextPrimary.name(),
                           theme::kCanvas.name(), theme::kGridBar.name(), theme::kAccent.name(),
                           theme::kTextSecondary.name()));

    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText("Filter Samples");
    filter_->setClearButtonEnabled(true);
    connect(filter_, &QLineEdit::textChanged, this, [this] { reload(); });

    list_ = new QListWidget(this);
    list_->setIconSize(QSize(kThumbWidth, kThumbHeight));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        audition_row(item);
    });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        open_row(item);
    });
    connect(list_, &QListWidget::itemSelectionChanged, this,
            [this] { place_button_->setEnabled(selected().has_value()); });
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = list_->itemAt(pos);
        if (!item) return;
        list_->setCurrentItem(item);
        QMenu menu(this);
        menu.addAction("Play", this, [this, item] { audition_row(item); });
        menu.addAction("Open", this, [this, item] { open_row(item); });
        QMenu* place = menu.addMenu("Place in Pattern");
        place->addActions(place_menu_->actions());
        menu.exec(list_->viewport()->mapToGlobal(pos));
    });

    place_menu_ = new QMenu(this);
    place_button_ = new QToolButton(this);
    place_button_->setText("Place in Pattern ▾");
    place_button_->setMenu(place_menu_);
    place_button_->setPopupMode(QToolButton::InstantPopup);
    place_button_->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(filter_);
    layout->addWidget(list_, 1);
    layout->addWidget(place_button_);

    history_.observe([this] { reload(); });
    reload();
}

std::optional<core::Id> SampleBrowser::selected() const {
    const core::Sample* sample = sample_at(list_->currentItem());
    if (!sample || !list_->currentItem()->isSelected()) return std::nullopt;
    return sample->id;
}

const core::Sample* SampleBrowser::sample_at(QListWidgetItem* item) const {
    if (!item) return nullptr;
    const auto& samples = history_.read().samples.items;
    int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= static_cast<int>(samples.size())) return nullptr;
    return &samples[static_cast<std::size_t>(index)];
}

void SampleBrowser::reload() {
    std::optional<core::Id> keep = selected();
    list_->clear();
    QString needle = filter_->text().trimmed();
    const auto& samples = history_.read().samples.items;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const core::Sample& sample = samples[i];
        QString name = QString::fromStdString(sample.name);
        if (!needle.isEmpty() && !name.contains(needle, Qt::CaseInsensitive)) continue;

        QString label = name;
        if (sample.source) label += "   " + duration_text(*sample.source);
        auto* item = new QListWidgetItem(QIcon(thumbnail(sample, theme::hue(static_cast<int>(i)))),
                                         label, list_);
        item->setData(Qt::UserRole, static_cast<int>(i));
        // The mark must reach a screen reader as well as the eye.
        item->setData(Qt::AccessibleDescriptionRole,
                      sample.provenance.is_human() ? QString() : QString("AI-generated"));
        item->setToolTip(provenance::text(sample.provenance));
        if (keep && *keep == sample.id) {
            list_->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    rebuild_place_menu();
    place_button_->setEnabled(selected().has_value());
}

void SampleBrowser::rebuild_place_menu() {
    place_menu_->clear();
    for (const core::Pattern& pattern : history_.read().patterns.items) {
        core::Id id = pattern.id;
        place_menu_->addAction(QString::fromStdString(pattern.name), this,
                               [this, id] { place_selected_in(id); });
    }
    if (place_menu_->isEmpty()) place_menu_->addAction("No Patterns yet")->setEnabled(false);
}

void SampleBrowser::audition_row(QListWidgetItem* item) {
    const core::Sample* sample = sample_at(item);
    if (!sample) return;
    audition_.audition(sample->source);
    emit hint_changed(QString("Playing '%1'. Right-click or use the button below to place it in a Pattern.")
                          .arg(QString::fromStdString(sample->name)));
}

void SampleBrowser::open_row(QListWidgetItem* item) {
    const core::Sample* sample = sample_at(item);
    if (!sample) return;
    show_details(*sample, theme::hue(item->data(Qt::UserRole).toInt()), this);
}

void SampleBrowser::place_selected_in(core::Id pattern) {
    using namespace core::functions;
    std::optional<core::Id> sample_id = selected();
    if (!sample_id) return;
    const core::Project& project = history_.read();
    const core::Sample* sample = project.samples.find(*sample_id);
    const core::Pattern* pat = project.patterns.find(pattern);
    if (!sample || !pat) {
        emit hint_changed("That Sample or Pattern is no longer in the Project.");
        return;
    }

    // Reuse the Instrument already playing this Sample; a Pattern lane is
    // per Instrument, so placing the same Sample twice would be the same lane.
    std::optional<core::Id> instrument;
    for (const core::Instrument& inst : project.instruments.items) {
        if (inst.params.sample == *sample_id) {
            instrument = inst.id;
            break;
        }
    }
    if (instrument) {
        for (const core::Part& part : pat->parts) {
            if (part.instrument == *instrument) {
                emit hint_changed(QString("'%1' is already in '%2'.")
                                      .arg(QString::fromStdString(sample->name),
                                           QString::fromStdString(pat->name)));
                return;
            }
        }
    }

    history_.begin_gesture("Place '" + sample->name + "' in '" + pat->name + "'");
    if (!instrument) {
        auto created = create_instrument(history_.read(), sample->name, *sample_id,
                                         core::SamplerMode::OneShot);
        if (created.ok() && history_.apply(std::move(created->delta)) == core::ApplyResult::Applied) {
            instrument = created->id;
        }
    }
    QString hint;
    if (instrument) {
        auto added = add_part(history_.read(), pattern, *instrument);
        if (added.ok()) {
            history_.apply(std::move(added->delta));
            hint = QString("Placed '%1' in '%2'.")
                       .arg(QString::fromStdString(sample->name), QString::fromStdString(pat->name));
        } else {
            hint = QString::fromStdString(added.error);
        }
    } else {
        hint = "Could not add an Instrument for that Sample.";
    }
    history_.end_gesture();
    emit hint_changed(hint);
}

}  // namespace ui
