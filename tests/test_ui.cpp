// Interaction tests for the Qt layer, from the confirmed FMEA table
// (tests/FMEA-arrangement-view.md, PR #30 interview): ArrangementView
// geometry and hit-testing, drag commit atomicity, rename-editor failure
// routing, MainWindow's undo/redo and transport bindings, and the Sample
// sidebar (issue #12): audition, provenance mark, placing into a Pattern.
//
// Runs on the offscreen platform; simulated input only. The tests replicate
// the view's layout constants (header 160 px, ruler 28 px, track 56 px,
// 28 px per beat at default zoom) — if the layout changes deliberately,
// these numbers change with it.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest/QtTest>

#include "core/arrangement_functions.h"
#include "core/history.h"
#include "core/playback.h"
#include "test_support.h"
#include "ui/arrangement_view.h"
#include "ui/main_window.h"
#include "ui/sample_browser.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

namespace {

// Layout constants mirrored from arrangement_view.cpp.
constexpr int kHeaderWidth = 160;
constexpr int kRulerHeight = 28;
constexpr int kTrackHeight = 56;
constexpr double kPxPerBeat = 28.0;  // default zoom

int beat_x(double beats) {
    return kHeaderWidth + static_cast<int>(beats * kPxPerBeat);
}

int track_y(int index) { return kRulerHeight + index * kTrackHeight + kTrackHeight / 2; }

struct StubTransport : TransportPort {
    PlaybackStatus current;
    Ticks last_seek = -1;
    void play() override { current.playing = true; }
    void stop() override { current.playing = false; }
    void seek(Ticks position) override {
        last_seek = position;
        current.position = position;
    }
    PlaybackStatus status() const override { return current; }
};

struct StubAudition : AuditionPort {
    std::vector<SampleSource> played;
    void audition(SampleSource audio) override { played.push_back(std::move(audio)); }
};

// The sidebar over the fixture plus one AI-generated Sample (section 6.3).
struct BrowserFixture {
    test_support::Fixture ids = make_fixture();
    Id generated_sample;
    ProjectHistory history;
    StubAudition audition;
    ui::SampleBrowser browser;
    QListWidget* list = nullptr;

    static Project with_generated(Project project, Id& id) {
        Sample generated{new_id(), "Riser", test_support::make_tone(0.2, 440.0),
                         Provenance::generated("test-model", 1700000000)};
        id = generated.id;
        project.musical.samples.items.push_back(generated);
        return project;
    }

    BrowserFixture()
        : history(with_generated(std::move(ids.project), generated_sample)),
          browser(history, audition) {
        browser.resize(220, 400);
        browser.show();
        (void)QTest::qWaitForWindowExposed(&browser);
        list = browser.findChild<QListWidget*>();
        REQUIRE(list);
    }

    QPoint row_centre(int row) const { return list->visualItemRect(list->item(row)).center(); }
};

// A move event with the left button held, which QTest::mouseMove cannot send.
void drag_to(QWidget& widget, const QPoint& pos) {
    QMouseEvent move(QEvent::MouseMove, QPointF(pos), QPointF(widget.mapToGlobal(pos)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);
}

struct ViewFixture {
    test_support::Fixture ids = make_fixture();
    ProjectHistory history;
    StubTransport transport;
    ui::ArrangementView view;

    ViewFixture()
        : history(std::move(ids.project)), view(history, transport) {
        view.resize(1200, 400);
        view.show();
        (void)QTest::qWaitForWindowExposed(&view);
    }

    const Track& track(int index) const {
        return history.read().musical.arrangements.items.front().tracks[
            static_cast<std::size_t>(index)];
    }

    Id place(Id track_id, Ticks start) {
        auto placed = add_placement(history.read().musical, ids.arrangement, track_id,
                                    ids.drum_pattern, start);
        REQUIRE(placed.ok());
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
        return placed->id;
    }
};

}  // namespace

TEST_CASE("clicking an empty lane with an armed Pattern places it bar-snapped") {
    ViewFixture f;
    f.view.set_armed_pattern(f.ids.drum_pattern);

    // 8.36 beats in; the bar grid must pull it back to bar 3 (beat 8).
    QTest::mouseClick(&f.view, Qt::LeftButton, {}, QPoint(beat_x(8.36), track_y(0)));

    const Track& t = f.track(0);
    REQUIRE(t.placements.size() == 1);
    CHECK(t.placements[0].start == 8 * kPpq);
    CHECK(t.placements[0].length == 4 * kPpq);  // the Pattern's own length
    CHECK(t.placements[0].pattern == f.ids.drum_pattern);
    CHECK(f.history.state().undo_label == "Place 'Drums A' on 'Track 1'");

    REQUIRE(f.history.undo());
    CHECK(f.track(0).placements.empty());
}

TEST_CASE("clicking a lane with nothing armed places nothing and records nothing") {
    ViewFixture f;
    QTest::mouseClick(&f.view, Qt::LeftButton, {}, QPoint(beat_x(4), track_y(0)));
    CHECK(f.track(0).placements.empty());
    CHECK_FALSE(f.history.state().can_undo);
}

TEST_CASE("a drag commits exactly one Delta matching the preview") {
    ViewFixture f;
    f.place(f.ids.track_a, 0);  // 4 beats long: pixels 160..272 on row 0
    MusicalContent before = f.history.read().musical;

    SECTION("move within the Track lands on the previewed beat") {
        QTest::mousePress(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
        drag_to(f.view, QPoint(beat_x(4), track_y(0)));
        QTest::mouseRelease(&f.view, Qt::LeftButton, {}, QPoint(beat_x(4), track_y(0)));

        REQUIRE(f.track(0).placements.size() == 1);
        CHECK(f.track(0).placements[0].start == 2 * kPpq);
        CHECK(f.track(0).placements[0].length == 4 * kPpq);

        // Exactly one entry beyond the setup placement.
        REQUIRE(f.history.undo());
        CHECK(f.history.read().musical == before);
    }

    SECTION("move across Tracks is one undoable step") {
        QTest::mousePress(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
        drag_to(f.view, QPoint(beat_x(4), track_y(1)));
        QTest::mouseRelease(&f.view, Qt::LeftButton, {}, QPoint(beat_x(4), track_y(1)));

        CHECK(f.track(0).placements.empty());
        REQUIRE(f.track(1).placements.size() == 1);
        CHECK(f.track(1).placements[0].start == 2 * kPpq);

        REQUIRE(f.history.undo());
        CHECK(f.history.read().musical == before);
    }

    SECTION("dragging the right edge resizes to the previewed length") {
        // Placement ends at x=272; press inside the 8 px grip, pull to 6 beats.
        QTest::mousePress(&f.view, Qt::LeftButton, {}, QPoint(266, track_y(0)));
        drag_to(f.view, QPoint(beat_x(6), track_y(0)));
        QTest::mouseRelease(&f.view, Qt::LeftButton, {}, QPoint(beat_x(6), track_y(0)));

        REQUIRE(f.track(0).placements.size() == 1);
        CHECK(f.track(0).placements[0].start == 0);
        CHECK(f.track(0).placements[0].length == 6 * kPpq);
        // The referenced Pattern is untouched.
        CHECK(f.history.read().musical.patterns.find(f.ids.drum_pattern)->length == 4 * kPpq);

        REQUIRE(f.history.undo());
        CHECK(f.history.read().musical == before);
    }

    SECTION("a click without movement commits nothing") {
        QTest::mousePress(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
        QTest::mouseRelease(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
        CHECK(f.history.read().musical == before);
        CHECK(f.history.state().undo_label == "Place 'Drums A' on 'Track 1'");
    }
}

TEST_CASE("Delete removes the selected Placement through the catalogue") {
    ViewFixture f;
    f.place(f.ids.track_a, 0);
    // A no-move click selects without committing.
    QTest::mousePress(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
    QTest::mouseRelease(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));

    QTest::keyClick(&f.view, Qt::Key_Delete);
    CHECK(f.track(0).placements.empty());
    CHECK(f.history.state().undo_label == "Remove 'Drums A' from 'Track 1'");
}

TEST_CASE("the ruler seeks beat-snapped through the transport port") {
    ViewFixture f;
    QTest::mouseClick(&f.view, Qt::LeftButton, {}, QPoint(beat_x(8), 10));
    CHECK(f.transport.last_seek == 8 * kPpq);
}

TEST_CASE("the mute dot toggles the Track through set_track_muted") {
    ViewFixture f;
    // The dot strip is the header's right 26 px.
    QTest::mouseClick(&f.view, Qt::LeftButton, {}, QPoint(kHeaderWidth - 20, track_y(0)));
    CHECK(f.track(0).muted);
    CHECK(f.history.state().undo_label == "Mute Track 'Track 1'");

    QTest::mouseClick(&f.view, Qt::LeftButton, {}, QPoint(kHeaderWidth - 20, track_y(0)));
    CHECK_FALSE(f.track(0).muted);
}

TEST_CASE("the add-track affordance appends one Track") {
    ViewFixture f;
    int add_y = kRulerHeight + 2 * kTrackHeight + 13;  // centre of the 26 px strip
    QTest::mouseClick(&f.view, Qt::LeftButton, {}, QPoint(80, add_y));

    const auto& tracks = f.history.read().musical.arrangements.items.front().tracks;
    REQUIRE(tracks.size() == 3);
    CHECK(tracks.back().name == "Track 3");
    CHECK(f.history.state().undo_label == "Add Track 'Track 3'");
}

TEST_CASE("the rename editor commits through rename_track") {
    ViewFixture f;
    QTest::mouseDClick(&f.view, Qt::LeftButton, {}, QPoint(60, track_y(0)));
    QLineEdit* editor = f.view.findChild<QLineEdit*>();
    REQUIRE(editor);

    editor->setText("Lead");
    QTest::keyClick(editor, Qt::Key_Return);

    CHECK(f.track(0).name == "Lead");
    CHECK(f.history.state().undo_label == "Rename Track to 'Lead'");
}

TEST_CASE("a rename landing on a deleted Track becomes a hint, not a mutation") {
    ViewFixture f;
    QSignalSpy hints(&f.view, &ui::ArrangementView::hint_changed);

    QTest::mouseDClick(&f.view, Qt::LeftButton, {}, QPoint(60, track_y(0)));
    QLineEdit* editor = f.view.findChild<QLineEdit*>();
    REQUIRE(editor);
    editor->setText("Ghost");

    // The Track vanishes behind the open editor (an Assistant could do this).
    auto del = delete_track(f.history.read().musical, f.ids.arrangement, f.ids.track_a);
    REQUIRE(del.ok());
    REQUIRE(f.history.apply(std::move(*del)) == ApplyResult::Applied);
    MusicalContent after_delete = f.history.read().musical;

    QTest::keyClick(editor, Qt::Key_Return);

    CHECK(f.history.read().musical == after_delete);
    CHECK(f.history.state().undo_label == "Delete Track 'Track 1'");
    REQUIRE(!hints.isEmpty());
    CHECK(hints.last().at(0).toString().contains("No such Track"));
}

TEST_CASE("Escape disarms the armed Pattern and clears the hint") {
    ViewFixture f;
    f.view.set_armed_pattern(f.ids.drum_pattern);
    QSignalSpy hints(&f.view, &ui::ArrangementView::hint_changed);

    QTest::keyClick(&f.view, Qt::Key_Escape);
    REQUIRE(!hints.isEmpty());
    CHECK(hints.last().at(0).toString().isEmpty());

    // Nothing armed any more: a lane click places nothing.
    QTest::mouseClick(&f.view, Qt::LeftButton, {}, QPoint(beat_x(4), track_y(0)));
    CHECK(f.track(0).placements.empty());
}

TEST_CASE("MainWindow's undo and redo actions follow HistoryState") {
    auto ids = make_fixture();
    ProjectHistory history(std::move(ids.project));
    StubTransport transport;
    StubAudition audition;
    ui::MainWindow window(history, transport, audition, "Test");
    window.show();
    (void)QTest::qWaitForWindowExposed(&window);

    QAction* undo_action = nullptr;
    QAction* redo_action = nullptr;
    for (QAction* action : window.actions()) {
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo)))
            undo_action = action;
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Redo)))
            redo_action = action;
    }
    REQUIRE(undo_action);
    REQUIRE(redo_action);
    CHECK_FALSE(undo_action->isEnabled());
    CHECK_FALSE(redo_action->isEnabled());

    auto created = create_track(history.read().musical, ids.arrangement, "Bass", std::nullopt);
    REQUIRE(created.ok());
    REQUIRE(history.apply(std::move(created->delta)) == ApplyResult::Applied);

    CHECK(undo_action->isEnabled());
    CHECK(undo_action->text() == "Undo Add Track 'Bass'");

    undo_action->trigger();
    CHECK_FALSE(undo_action->isEnabled());
    CHECK(redo_action->isEnabled());
    CHECK(redo_action->text() == "Redo Add Track 'Bass'");

    redo_action->trigger();
    CHECK(history.read().musical.arrangements.items.front().tracks.size() == 3);
}

TEST_CASE("MainWindow's transport poll reflects the port's status") {
    auto ids = make_fixture();
    ProjectHistory history(std::move(ids.project));
    StubTransport transport;
    StubAudition audition;
    ui::MainWindow window(history, transport, audition, "Test");
    window.show();
    (void)QTest::qWaitForWindowExposed(&window);

    QToolButton* play_button = nullptr;
    for (QToolButton* button : window.findChildren<QToolButton*>()) {
        if (button->text() == "Play") play_button = button;
    }
    REQUIRE(play_button);

    QLabel* position_label = nullptr;
    for (QLabel* label : window.findChildren<QLabel*>()) {
        if (label->text().trimmed() == "001.1") position_label = label;
    }
    REQUIRE(position_label);

    // Bar 2, beat 3 under the fixture's 4/4: one bar plus two beats.
    transport.current.playing = true;
    transport.current.position = 4 * kPpq + 2 * kPpq;
    QTest::qWait(150);  // > one 50 ms poll

    CHECK(play_button->text() == "Stop");
    CHECK(position_label->text().trimmed() == "002.3");
}

TEST_CASE("the sidebar lists every Sample and clicking one auditions it") {
    BrowserFixture f;
    REQUIRE(f.list->count() == 3);
    CHECK(f.list->item(0)->text().startsWith("hit"));
    CHECK(f.list->item(0)->text().endsWith("0.10 s"));

    QTest::mouseClick(f.list->viewport(), Qt::LeftButton, {}, f.row_centre(1));
    REQUIRE(f.audition.played.size() == 1);
    CHECK(f.audition.played[0] == f.history.read().musical.samples.items[1].source);
    CHECK(f.browser.selected() == f.history.read().musical.samples.items[1].id);
    CHECK_FALSE(f.history.state().can_undo);  // hearing is not a mutation
}

TEST_CASE("only an AI-generated Sample carries the provenance mark") {
    BrowserFixture f;
    CHECK(f.list->item(0)->data(Qt::AccessibleDescriptionRole).toString().isEmpty());
    CHECK(f.list->item(1)->data(Qt::AccessibleDescriptionRole).toString().isEmpty());
    CHECK(f.list->item(2)->data(Qt::AccessibleDescriptionRole).toString() == "AI-generated");
    CHECK(f.list->item(2)->toolTip().contains("test-model"));
    CHECK(f.list->item(2)->toolTip().contains("may not be licenseable"));
    CHECK(f.list->item(0)->toolTip() == "Made by hand");

    // The corner badge is amber (theme accent) on the generated row's icon only.
    auto accent_pixels = [](const QImage& image) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == QColor(0xE8, 0xA1, 0x3C)) ++count;
        return count;
    };
    CHECK(accent_pixels(f.list->item(2)->icon().pixmap(36, 22).toImage()) > 20);
    CHECK(accent_pixels(f.list->item(0)->icon().pixmap(36, 22).toImage()) == 0);
}

TEST_CASE("opening a generated Sample states its provenance in words") {
    BrowserFixture f;
    f.list->setCurrentRow(2);
    emit f.list->itemActivated(f.list->item(2));
    QLabel* provenance = f.browser.findChild<QLabel*>("provenance");
    REQUIRE(provenance);
    CHECK(provenance->text().startsWith("AI-generated by test-model on 2023-11-14"));
}

TEST_CASE("the filter narrows the list by name") {
    BrowserFixture f;
    QLineEdit* filter = f.browser.findChild<QLineEdit*>();
    REQUIRE(filter);
    filter->setText("TON");
    REQUIRE(f.list->count() == 1);
    CHECK(f.list->item(0)->text().startsWith("tone"));
    filter->clear();
    CHECK(f.list->count() == 3);
}

TEST_CASE("placing a Sample into a Pattern is one undoable step") {
    BrowserFixture f;
    MusicalContent before = f.history.read().musical;
    f.list->setCurrentRow(2);  // the Riser: no Instrument plays it yet

    SECTION("a Sample without an Instrument gets one, then a lane") {
        f.browser.place_selected_in(f.ids.drum_pattern);

        const MusicalContent& p = f.history.read().musical;
        REQUIRE(p.instruments.items.size() == 3);
        CHECK(p.instruments.items.back().name == "Riser");
        CHECK(p.instruments.items.back().params.sample == f.generated_sample);
        const Pattern* drums = p.patterns.find(f.ids.drum_pattern);
        REQUIRE(drums->parts.size() == 2);
        CHECK(drums->parts.back().instrument == p.instruments.items.back().id);
        CHECK(drums->parts.back().events.empty());
        CHECK(f.history.state().undo_label == "Place 'Riser' in 'Drums A'");

        REQUIRE(f.history.undo());
        CHECK(f.history.read().musical == before);
    }

    SECTION("a Sample already played by an Instrument reuses it") {
        f.list->setCurrentRow(1);  // tone: played by 'Keys'
        f.browser.place_selected_in(f.ids.drum_pattern);
        const MusicalContent& p = f.history.read().musical;
        CHECK(p.instruments.items.size() == 2);
        const Pattern* drums = p.patterns.find(f.ids.drum_pattern);
        REQUIRE(drums->parts.size() == 2);
        CHECK(drums->parts.back().instrument == p.instruments.items[1].id);
    }

    SECTION("a Sample already in the Pattern is a hint, not a Delta") {
        QSignalSpy hints(&f.browser, &ui::SampleBrowser::hint_changed);
        f.list->setCurrentRow(0);  // hit: already the drum Pattern's lane
        f.browser.place_selected_in(f.ids.drum_pattern);
        CHECK(f.history.read().musical == before);
        CHECK_FALSE(f.history.state().can_undo);
        REQUIRE(!hints.isEmpty());
        CHECK(hints.last().at(0).toString() == "'hit' is already in 'Drums A'.");
    }

    SECTION("the place menu offers every Pattern and acts on the selection") {
        QToolButton* button = nullptr;
        for (QToolButton* candidate : f.browser.findChildren<QToolButton*>()) {
            if (candidate->menu()) button = candidate;
        }
        REQUIRE(button);
        CHECK(button->isEnabled());
        REQUIRE(button->menu()->actions().size() == 2);
        CHECK(button->menu()->actions()[1]->text() == "Melody A");
        button->menu()->actions()[1]->trigger();
        CHECK(f.history.read().musical.patterns.find(f.ids.melody_pattern)->parts.size() == 2);
        CHECK(f.history.state().undo_label == "Place 'Riser' in 'Melody A'");
    }
}

TEST_CASE("placing under an active filter acts on the Sample shown, not the row number") {
    BrowserFixture f;
    QLineEdit* filter = f.browser.findChild<QLineEdit*>();
    REQUIRE(filter);
    filter->setText("ris");  // only the Riser remains, now at row 0
    REQUIRE(f.list->count() == 1);
    f.list->setCurrentRow(0);
    CHECK(f.browser.selected() == f.generated_sample);

    QSignalSpy hints(&f.browser, &ui::SampleBrowser::hint_changed);
    f.browser.place_selected_in(f.ids.melody_pattern);

    const MusicalContent& p = f.history.read().musical;
    REQUIRE(p.instruments.items.size() == 3);
    CHECK(p.instruments.items.back().params.sample == f.generated_sample);
    const Pattern* melody = p.patterns.find(f.ids.melody_pattern);
    REQUIRE(melody->parts.size() == 2);
    CHECK(melody->parts.back().instrument == p.instruments.items.back().id);
    CHECK(hints.last().at(0).toString() == "Placed 'Riser' in 'Melody A'.");

    // The filtered view survives the reload the placement triggered, with
    // the placed Sample still selected, and clears back to the full list.
    CHECK(f.list->count() == 1);
    CHECK(f.browser.selected() == f.generated_sample);
    filter->clear();
    CHECK(f.list->count() == 3);
    CHECK(f.browser.selected() == f.generated_sample);
}

TEST_CASE("the sidebar follows the History: undo takes a placed lane back, selection kept") {
    BrowserFixture f;
    f.list->setCurrentRow(1);
    f.browser.place_selected_in(f.ids.drum_pattern);
    REQUIRE(f.history.read().musical.patterns.find(f.ids.drum_pattern)->parts.size() == 2);

    REQUIRE(f.history.undo());
    CHECK(f.history.read().musical.patterns.find(f.ids.drum_pattern)->parts.size() == 1);
    CHECK(f.list->count() == 3);
    CHECK(f.browser.selected() == f.history.read().musical.samples.items[1].id);

    // Nothing selected: placing is a no-op and the button is disabled.
    f.list->clearSelection();
    f.list->setCurrentItem(nullptr);
    CHECK_FALSE(f.browser.selected().has_value());
    MusicalContent before = f.history.read().musical;
    f.browser.place_selected_in(f.ids.drum_pattern);
    CHECK(f.history.read().musical == before);
    QToolButton* button = nullptr;
    for (QToolButton* candidate : f.browser.findChildren<QToolButton*>()) {
        if (candidate->menu()) button = candidate;
    }
    REQUIRE(button);
    CHECK_FALSE(button->isEnabled());
}

TEST_CASE("a Sample without audio lists, opens, and is handed to the port as absent") {
    auto ids = make_fixture();
    ids.project.musical.samples.items.push_back(Sample{new_id(), "silent", nullptr, Provenance::human()});
    ProjectHistory history(std::move(ids.project));
    StubAudition audition;
    ui::SampleBrowser browser(history, audition);
    browser.show();
    (void)QTest::qWaitForWindowExposed(&browser);
    QListWidget* list = browser.findChild<QListWidget*>();
    REQUIRE(list);
    REQUIRE(list->count() == 3);
    CHECK(list->item(2)->text() == "silent");

    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualItemRect(list->item(2)).center());
    REQUIRE(audition.played.size() == 1);
    CHECK(audition.played[0] == nullptr);
    emit list->itemActivated(list->item(2));  // opening must not dereference the source
    QLabel* provenance = browser.findChild<QLabel*>("provenance");
    REQUIRE(provenance);
    CHECK(provenance->text() == "Made by hand");
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
