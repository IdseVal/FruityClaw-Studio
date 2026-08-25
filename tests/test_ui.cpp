// Interaction tests for the Qt layer, from the confirmed FMEA table
// (tests/FMEA-arrangement-view.md, PR #30 interview): ArrangementView
// geometry and hit-testing, drag commit atomicity, rename-editor failure
// routing, and MainWindow's undo/redo and transport bindings.
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
        return history.read().arrangements.items.front().tracks[
            static_cast<std::size_t>(index)];
    }

    Id place(Id track_id, Ticks start) {
        auto placed = add_placement(history.read(), ids.arrangement, track_id,
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
    Project before = f.history.read();

    SECTION("move within the Track lands on the previewed beat") {
        QTest::mousePress(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
        drag_to(f.view, QPoint(beat_x(4), track_y(0)));
        QTest::mouseRelease(&f.view, Qt::LeftButton, {}, QPoint(beat_x(4), track_y(0)));

        REQUIRE(f.track(0).placements.size() == 1);
        CHECK(f.track(0).placements[0].start == 2 * kPpq);
        CHECK(f.track(0).placements[0].length == 4 * kPpq);

        // Exactly one entry beyond the setup placement.
        REQUIRE(f.history.undo());
        CHECK(f.history.read() == before);
    }

    SECTION("move across Tracks is one undoable step") {
        QTest::mousePress(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
        drag_to(f.view, QPoint(beat_x(4), track_y(1)));
        QTest::mouseRelease(&f.view, Qt::LeftButton, {}, QPoint(beat_x(4), track_y(1)));

        CHECK(f.track(0).placements.empty());
        REQUIRE(f.track(1).placements.size() == 1);
        CHECK(f.track(1).placements[0].start == 2 * kPpq);

        REQUIRE(f.history.undo());
        CHECK(f.history.read() == before);
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
        CHECK(f.history.read().patterns.find(f.ids.drum_pattern)->length == 4 * kPpq);

        REQUIRE(f.history.undo());
        CHECK(f.history.read() == before);
    }

    SECTION("a click without movement commits nothing") {
        QTest::mousePress(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
        QTest::mouseRelease(&f.view, Qt::LeftButton, {}, QPoint(beat_x(2), track_y(0)));
        CHECK(f.history.read() == before);
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

    const auto& tracks = f.history.read().arrangements.items.front().tracks;
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
    auto del = delete_track(f.history.read(), f.ids.arrangement, f.ids.track_a);
    REQUIRE(del.ok());
    REQUIRE(f.history.apply(std::move(*del)) == ApplyResult::Applied);
    Project after_delete = f.history.read();

    QTest::keyClick(editor, Qt::Key_Return);

    CHECK(f.history.read() == after_delete);
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
    ui::MainWindow window(history, transport, {}, "Test");
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

    auto created = create_track(history.read(), ids.arrangement, "Bass", std::nullopt);
    REQUIRE(created.ok());
    REQUIRE(history.apply(std::move(created->delta)) == ApplyResult::Applied);

    CHECK(undo_action->isEnabled());
    CHECK(undo_action->text() == "Undo Add Track 'Bass'");

    undo_action->trigger();
    CHECK_FALSE(undo_action->isEnabled());
    CHECK(redo_action->isEnabled());
    CHECK(redo_action->text() == "Redo Add Track 'Bass'");

    redo_action->trigger();
    CHECK(history.read().arrangements.items.front().tracks.size() == 3);
}

TEST_CASE("MainWindow's transport poll reflects the port's status") {
    auto ids = make_fixture();
    ProjectHistory history(std::move(ids.project));
    StubTransport transport;
    ui::MainWindow window(history, transport, {}, "Test");
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

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("MainWindow's title carries the dirty marker and the File actions exist") {
    auto f = make_fixture();
    core::ProjectHistory history(std::move(f.project));
    StubTransport transport;
    ui::MainWindow window(history, transport, {}, "Test");

    CHECK(window.windowTitle() == "Untitled - Test");

    auto rename = rename_track(history.read(), f.arrangement, f.track_a, "Renamed");
    REQUIRE(rename.ok());
    REQUIRE(history.apply(std::move(*rename)) == core::ApplyResult::Applied);
    CHECK(window.windowTitle() == "Untitled* - Test");

    history.mark_saved();
    history.undo();  // an observed change refreshes the title
    history.redo();
    CHECK(window.windowTitle() == "Untitled - Test");

    QStringList texts;
    for (QAction* action : window.actions()) texts << action->text();
    CHECK(texts.contains("&Open..."));
    CHECK(texts.contains("&Save"));
    CHECK(texts.contains("Save &As..."));
}
