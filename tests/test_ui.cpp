// Interaction tests for the Qt layer, from the confirmed FMEA table
// (tests/FMEA-arrangement-view.md, PR #30 interview): ArrangementView
// geometry and hit-testing, drag commit atomicity, rename-editor failure
// routing, MainWindow's undo/redo and transport bindings, the Sample
// sidebar (issue #12): audition, provenance mark, placing into a Pattern,
// the Pattern provenance marks (issue #19) on the palette and on Placements,
// the recording controls (issue #14): input choice, record/stop, the
// take landing in the Project as a Human Sample, and the Function
// switchboard (issue #17): one labelled row per registry entry, removal
// from the file, the Assistant-only rule, the local-only banner derived
// from the built file.
//
// Runs on the offscreen platform; simulated input only. The tests replicate
// the view's layout constants (header 160 px, ruler 28 px, track 56 px,
// 28 px per beat at default zoom) — if the layout changes deliberately,
// these numbers change with it.
#include <algorithm>
#include <string_view>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QCheckBox>
#include <QPushButton>
#include <QRadioButton>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QToolButton>
#include <QtTest/QtTest>

#include <QCheckBox>
#include <QDir>
#include <QFrame>
#include <QStandardPaths>
#include <QTabWidget>

#include "app/toggle_file.h"

#include "assistant/registry.h"
#include "core/arrangement_functions.h"
#include "core/generation.h"
#include "core/history.h"
#include "core/playback.h"
#include "test_support.h"
#include "ui/arrangement_view.h"
#include "ui/assistant_key_dialog.h"
#include "ui/function_switchboard.h"
#include "ui/generation_settings_page.h"
#include "ui/main_window.h"
#include "ui/pattern_palette.h"
#include "ui/settings_dialog.h"
#include "ui/provenance.h"
#include "ui/record_bar.h"
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

// A recorder port with two inputs, one of which refuses to open, that hands
// back a fixed take when a recording stops.
struct StubRecorder : RecorderPort {
    int selected = -1;
    bool recording = false;
    SampleSource take = test_support::make_tone(0.3, 330.0);
    std::vector<int> selections;

    std::vector<InputInfo> inputs() override {
        return {{7, "Mic", "Stub", 1}, {9, "Broken", "Stub", 2}};
    }
    int selected_input() const override { return selected; }
    std::string select_input(int id) override {
        selections.push_back(id);
        if (id == 9) return "Could not open that input: busy";
        selected = id;
        return {};
    }
    bool start_recording() override {
        if (selected < 0 || recording) return false;
        recording = true;
        return true;
    }
    SampleSource stop_recording() override {
        if (!recording) return nullptr;
        recording = false;
        return take;
    }
    RecorderStatus status() override {
        RecorderStatus s;
        s.recording = recording;
        s.input_open = selected >= 0;
        s.frames = recording ? 4800 : 0;
        s.sample_rate = 48000.0;
        s.peak = 0.5f;
        return s;
    }
};

struct RecordFixture {
    test_support::Fixture ids = make_fixture();
    ProjectHistory history;
    StubRecorder recorder;
    ui::RecordBar bar;
    QComboBox* inputs = nullptr;
    QToolButton* record = nullptr;
    QSignalSpy hints;

    RecordFixture()
        : history(std::move(ids.project)), bar(history, recorder),
          hints(&bar, &ui::RecordBar::hint_changed) {
        bar.show();
        (void)QTest::qWaitForWindowExposed(&bar);
        inputs = bar.findChild<QComboBox*>();
        record = bar.findChild<QToolButton*>();
        REQUIRE(inputs);
        REQUIRE(record);
    }

    // The combo's own activation path: what a mouse pick sends.
    void pick(int index) {
        inputs->setCurrentIndex(index);
        emit inputs->activated(index);
    }
    QString last_hint() const {
        return hints.isEmpty() ? QString() : hints.last().at(0).toString();
    }
};

// An in-memory GenerationSettingsPort: a fresh install until written to.
struct StubGenerationStore : GenerationSettingsPort {
    GenerationSettings kept;
    std::optional<std::string> key_kept;
    bool writable = true;
    GenerationSettings read() const override { return kept; }
    bool write(const GenerationSettings& settings) override {
        if (!writable) return false;
        kept = settings;
        return true;
    }
    bool has_key() const override { return key_kept.has_value(); }
    bool store_key(const std::string& key) override {
        key_kept = key;
        return true;
    }
    void clear_key() override { key_kept.reset(); }
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

// Counts pixels in the theme accent, the colour only the provenance mark
// (and the selection/playhead, which these tests keep out of the way) uses.
int accent_pixels(const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y) == QColor(0xE8, 0xA1, 0x3C)) ++count;
    return count;
}

// The fixture's melody Pattern rewritten as AI-generated (section 6.3).
Project with_generated_melody(Project project, Id melody) {
    project.musical.patterns.find(melody)->provenance = Provenance::generated("test-model", 1700000000);
    return project;
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
    StubRecorder recorder;
    assistant::FunctionToggles toggles;
    StubGenerationStore generation;
    ui::MainWindow window(history, transport, audition, recorder, toggles, generation, "Test");
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
    StubRecorder recorder;
    assistant::FunctionToggles toggles;
    StubGenerationStore generation;
    ui::MainWindow window(history, transport, audition, recorder, toggles, generation, "Test");
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

TEST_CASE("only an AI-generated Pattern carries the provenance mark in the palette") {
    test_support::Fixture ids = make_fixture();
    ProjectHistory history(with_generated_melody(std::move(ids.project), ids.melody_pattern));
    ui::PatternPalette palette(history);
    palette.show();
    REQUIRE(palette.count() == 2);

    CHECK(palette.item(0)->data(Qt::AccessibleDescriptionRole).toString().isEmpty());
    CHECK(palette.item(0)->toolTip() == "Made by hand");
    CHECK(palette.item(1)->data(Qt::AccessibleDescriptionRole).toString() == "AI-generated");
    CHECK(palette.item(1)->toolTip().contains("test-model"));
    CHECK(palette.item(1)->toolTip().contains("may not be licenseable"));

    CHECK(accent_pixels(palette.item(1)->icon().pixmap(26, 14).toImage()) > 20);
    CHECK(accent_pixels(palette.item(0)->icon().pixmap(26, 14).toImage()) == 0);
}

TEST_CASE("a Placement of an AI-generated Pattern carries the mark in its corner") {
    test_support::Fixture ids = make_fixture();
    ProjectHistory history(with_generated_melody(std::move(ids.project), ids.melody_pattern));
    StubTransport transport;
    ui::ArrangementView view(history, transport);
    view.resize(1200, 400);
    view.show();
    (void)QTest::qWaitForWindowExposed(&view);

    // Generated melody on Track 1, hand-made drums on Track 2, both at bar 2
    // so the playhead at bar 1 and the block edges never share a pixel.
    for (auto [track, pattern] : {std::pair{ids.track_a, ids.melody_pattern},
                                  std::pair{ids.track_b, ids.drum_pattern}}) {
        auto placed = add_placement(history.read().musical, ids.arrangement, track, pattern,
                                    4 * kPpq);
        REQUIRE(placed.ok());
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    }
    QImage frame = view.grab().toImage();

    // The bottom-right corner of each one-bar block (4 beats wide, 3 px inset).
    auto corner = [&](int track) {
        int right = beat_x(8) - 1;
        int bottom = kRulerHeight + track * kTrackHeight + kTrackHeight - 3 - 1;
        return frame.copy(QRect(right - 16, bottom - 13, 16, 13));
    };
    CHECK(accent_pixels(corner(0)) > 20);
    CHECK(accent_pixels(corner(1)) == 0);
}

TEST_CASE("the shared provenance line is worded once, for Samples and Patterns alike") {
    // Section 6.2: the licence caveat must reach the user in exactly these words,
    // and never for hand-made or bundled content (false positives are as
    // harmful as misses).
    CHECK(ui::provenance_text(Provenance::human()) == "Made by hand");
    CHECK(ui::provenance_text(Provenance::generated("test-model", 1700000000)) ==
          "AI-generated by test-model on 2023-11-14. Music made with it may not be licenseable.");
    CHECK(!ui::provenance_text(Provenance::human()).contains("licenseable"));
    CHECK(ui::kProvenanceMarkLabel == "AI-generated");
}

TEST_CASE("a Placement too narrow for the mark is left unmarked rather than overpainted") {
    test_support::Fixture ids = make_fixture();
    ProjectHistory history(with_generated_melody(std::move(ids.project), ids.melody_pattern));
    StubTransport transport;
    ui::ArrangementView view(history, transport);
    view.resize(1200, 400);
    view.show();
    (void)QTest::qWaitForWindowExposed(&view);

    // A generated melody at bar 2, trimmed to half a beat: 14 px at default
    // zoom, under the 20 px floor the view needs to fit the 14 px logo.
    auto placed = add_placement(history.read().musical, ids.arrangement, ids.track_a,
                                ids.melody_pattern, 4 * kPpq);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    auto trimmed = resize_placement(history.read().musical, ids.arrangement, ids.track_a,
                                    placed->id, kPpq / 2);
    REQUIRE(trimmed.ok());
    REQUIRE(history.apply(std::move(*trimmed)) == ApplyResult::Applied);
    REQUIRE(history.read().musical.arrangements.find(ids.arrangement)
                ->tracks[0]
                .placements[0]
                .length == kPpq / 2);

    QImage frame = view.grab().toImage();
    // The whole of Track 1 from bar 2 to bar 3: no accent pixel anywhere, so the
    // mark neither squeezes into the block nor spills past its edges.
    QRect row(beat_x(4) - 4, kRulerHeight, beat_x(8) - beat_x(4) + 8, kTrackHeight);
    CHECK(accent_pixels(frame.copy(row)) == 0);
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

// --- the first-open Assistant key offer (issue #18) ---------------------

struct StubKeys : core::AssistantKeyPort {
    std::optional<std::string> stored;
    int offers = 0;
    int store_calls = 0;
    bool fail_store = false;
    bool has_key() const override { return stored.has_value(); }
    std::optional<std::string> key() const override { return stored; }
    bool store_key(const std::string& key) override {
        ++store_calls;
        if (fail_store) return false;
        stored = key;
        return true;
    }
    void clear_key() override { stored.reset(); }
    bool offer_made() const override { return offers > 0; }
    void record_offer() override { ++offers; }
};

struct DialogFixture {
    StubKeys keys;
    ui::AssistantKeyDialog dialog{keys};
    QLineEdit* field = dialog.findChild<QLineEdit*>("assistant_key_field");
    QPushButton* save = dialog.findChild<QPushButton*>("assistant_key_save");
    QPushButton* decline = dialog.findChild<QPushButton*>("assistant_key_decline");
    QLabel* problem = dialog.findChild<QLabel*>("assistant_key_problem");
    DialogFixture() {
        REQUIRE(field);
        REQUIRE(save);
        REQUIRE(decline);
        REQUIRE(problem);
        dialog.show();
        (void)QTest::qWaitForWindowExposed(&dialog);
    }
};

// The message the issue asks for: what the key is for, and what happens
// without it, stated in the dialog's own words.
QString all_label_text(const QDialog& dialog) {
    QString text;
    for (QLabel* label : dialog.findChildren<QLabel*>()) text += label->text() + '\n';
    return text;
}

TEST_CASE("the offer says plainly what the key is for and that declining loses nothing") {
    DialogFixture f;
    QString text = all_label_text(f.dialog);
    CHECK(text.contains("The Assistant is optional"));
    CHECK(text.contains("needs something from you: a private API key"));
    CHECK(text.contains("Without a key the Assistant stays off and nothing else changes"));
    CHECK(text.contains("never written to a log"));
    CHECK(f.decline->text() == "Continue without a key");
    CHECK(f.save->text() == "Save key");
}

TEST_CASE("reopened with a key already saved, the dialog says so and keeps it on decline") {
    StubKeys keys;
    keys.stored = "sk-already-here";
    ui::AssistantKeyDialog dialog{keys};
    dialog.show();
    (void)QTest::qWaitForWindowExposed(&dialog);
    QLabel* saved = dialog.findChild<QLabel*>("assistant_key_saved");
    REQUIRE(saved);
    CHECK(saved->text() == "A key is saved. Paste a new one to replace it.");
    CHECK_FALSE(all_label_text(dialog).contains("sk-already-here"));
    QPushButton* decline = dialog.findChild<QPushButton*>("assistant_key_decline");
    REQUIRE(decline);
    CHECK(decline->text() == "Keep the saved key");
    QTest::mouseClick(decline, Qt::LeftButton);
    CHECK(keys.stored == "sk-already-here");
    CHECK(keys.store_calls == 0);
}

TEST_CASE("reopened with a key already saved, pasting a new one replaces it") {
    StubKeys keys;
    keys.stored = "sk-old";
    ui::AssistantKeyDialog dialog{keys};
    dialog.show();
    (void)QTest::qWaitForWindowExposed(&dialog);
    QLineEdit* field = dialog.findChild<QLineEdit*>("assistant_key_field");
    REQUIRE(field);
    // The saved key is never pre-filled: the field starts empty.
    CHECK(field->text().isEmpty());
    field->setText("sk-new");
    QTest::keyClick(field, Qt::Key_Return);
    CHECK(keys.store_calls == 1);
    CHECK(keys.stored == "sk-new");
    CHECK_FALSE(dialog.isVisible());
}

TEST_CASE("on first open nothing claims a key is saved") {
    DialogFixture f;
    CHECK(f.dialog.findChild<QLabel*>("assistant_key_saved") == nullptr);
}

TEST_CASE("declining stores nothing and records the offer, so it is never repeated") {
    DialogFixture f;
    QTest::mouseClick(f.decline, Qt::LeftButton);
    CHECK(f.dialog.result() == QDialog::Rejected);
    CHECK_FALSE(f.keys.has_key());
    CHECK(f.keys.store_calls == 0);
    CHECK(f.keys.offer_made());
    CHECK(f.keys.offers == 1);
}

TEST_CASE("closing the dialog any other way counts as declining") {
    DialogFixture f;
    QTest::keyClick(&f.dialog, Qt::Key_Escape);
    CHECK(f.dialog.result() == QDialog::Rejected);
    CHECK_FALSE(f.keys.has_key());
    CHECK(f.keys.offer_made());
}

TEST_CASE("the key field hides what is pasted and Save waits for a key") {
    DialogFixture f;
    CHECK(f.field->echoMode() == QLineEdit::Password);
    CHECK_FALSE(f.save->isEnabled());
    f.field->setText("   ");
    CHECK_FALSE(f.save->isEnabled());
    f.field->setText("sk-abc");
    CHECK(f.save->isEnabled());
}

TEST_CASE("saving keeps the trimmed key, records the offer and closes") {
    DialogFixture f;
    f.field->setText("  sk-live-key-42 \t ");
    QTest::mouseClick(f.save, Qt::LeftButton);
    CHECK(f.dialog.result() == QDialog::Accepted);
    REQUIRE(f.keys.stored.has_value());
    CHECK(*f.keys.stored == "sk-live-key-42");
    CHECK(f.keys.offer_made());
    CHECK(f.field->text().isEmpty());  // nothing left behind in the widget
}

TEST_CASE("Enter in the key field saves") {
    DialogFixture f;
    f.field->setText("sk-enter");
    QTest::keyClick(f.field, Qt::Key_Return);
    CHECK(f.dialog.result() == QDialog::Accepted);
    CHECK(f.keys.stored == std::optional<std::string>("sk-enter"));
}

TEST_CASE("Enter with only whitespace neither saves nor closes") {
    DialogFixture f;
    f.field->setText("  	 ");
    QTest::keyClick(f.field, Qt::Key_Return);
    CHECK(f.dialog.isVisible());
    CHECK(f.keys.store_calls == 0);
    CHECK_FALSE(f.keys.offer_made());
}

TEST_CASE("the dialog never puts the key into any label") {
    DialogFixture f;
    f.keys.fail_store = true;
    f.field->setText("sk-secret-canary");
    QTest::mouseClick(f.save, Qt::LeftButton);
    CHECK_FALSE(all_label_text(f.dialog).contains("sk-secret-canary"));
    CHECK_FALSE(f.dialog.windowTitle().contains("sk-secret-canary"));
}

TEST_CASE("a key that cannot be kept is said so, and the dialog stays open") {
    DialogFixture f;
    f.keys.fail_store = true;
    f.field->setText("sk-unsaveable");
    QTest::mouseClick(f.save, Qt::LeftButton);
    CHECK(f.dialog.isVisible());
    CHECK(f.problem->isVisible());
    CHECK(f.problem->text().contains("could not be saved"));
    CHECK_FALSE(f.keys.has_key());
    CHECK_FALSE(f.keys.offer_made());  // still unanswered
    f.field->setText("sk-retry");
    CHECK_FALSE(f.problem->isVisible());
}

// ---------------------------------------------------------------------------
// The Function switchboard (issue #17)

namespace {

struct SwitchboardFixture {
    assistant::FunctionToggles toggles;
    ui::FunctionSwitchboard board;
    QSignalSpy changed;

    explicit SwitchboardFixture(assistant::Capabilities capabilities = {})
        : board(toggles, capabilities), changed(&board, &ui::FunctionSwitchboard::changed) {
        board.resize(720, 600);
        board.show();
        (void)QTest::qWaitForWindowExposed(&board);
    }

    QCheckBox* row(const char* name) {
        QCheckBox* box = board.findChild<QCheckBox*>(name);
        REQUIRE(box);
        return box;
    }

    QString banner_title() {
        QFrame* banner = board.findChild<QFrame*>("local_only_banner");
        REQUIRE(banner);
        return banner->accessibleName();
    }

    bool offers(std::string_view name) const {
        const auto& entries = board.file().entries;
        return std::any_of(entries.begin(), entries.end(),
                           [name](auto* d) { return d->name == name; });
    }
};

int labels_reading(QWidget& root, const QString& text) {
    int count = 0;
    for (QLabel* label : root.findChildren<QLabel*>())
        if (label->text() == text) ++count;
    return count;
}

}  // namespace

TEST_CASE("the switchboard shows one row per registry entry, each labelled by class") {
    SwitchboardFixture f;
    auto switches = f.board.findChildren<QCheckBox*>();
    REQUIRE(switches.size() == 43);
    for (const assistant::FunctionDescriptor& d : assistant::registry()) {
        QCheckBox* box = f.row(d.name.data());
        CHECK(box->isChecked());
        CHECK(box->accessibleName() == QString::fromUtf8(d.effect.data(), d.effect.size()));
    }
    CHECK(labels_reading(f.board, "Directive") == 40);
    CHECK(labels_reading(f.board, "Rework") == 3);
    // O-17.2: the provider is stated per row; exactly one row reaches the
    // generation model.
    CHECK(labels_reading(f.board, "Assistant provider") == 42);
    CHECK(labels_reading(f.board, "Generation model") == 1);
}

TEST_CASE("switching a Function off removes it from the file; the user keeps the feature") {
    SwitchboardFixture f;
    REQUIRE(f.offers("rename_track"));

    f.row("rename_track")->click();
    CHECK(f.changed.count() == 1);
    CHECK(f.toggles.state("rename_track") == false);
    CHECK_FALSE(f.offers("rename_track"));
    CHECK(f.board.file().entries.size() == 40);  // 41 offered on a fresh install

    // The toggle is Assistant-only (core document 3.10): the user's own
    // rename goes through the same door as before, unaffected.
    auto ids = make_fixture();
    ProjectHistory history(std::move(ids.project));
    auto renamed = rename_track(history.read().musical, ids.arrangement, ids.track_a, "Bass");
    REQUIRE(renamed.ok());
    CHECK(history.apply(*renamed) == ApplyResult::Applied);
    CHECK(history.read().musical.arrangements.items.front().tracks[0].name == "Bass");

    f.row("rename_track")->click();
    CHECK(f.offers("rename_track"));
    CHECK(f.changed.count() == 2);
}

TEST_CASE("the local-only banner lights when the built file has no Rework Function") {
    SwitchboardFixture f;
    CHECK(f.banner_title().contains("3 Rework Functions are on"));

    // One click changes one row: the other Rework rows stay on and are now
    // recorded on, so the section 4.3 default cannot flip them.
    f.row("rework_pattern")->click();
    CHECK(f.banner_title().contains("2 Rework Functions are on"));
    CHECK(f.row("continue_pattern")->isChecked());
    CHECK(f.toggles.state("continue_pattern") == true);
    CHECK(f.toggles.state("describe_pattern") == true);
    CHECK(f.toggles.to_text() ==
          "continue_pattern=on\ndescribe_pattern=on\nrework_pattern=off\n");

    f.row("continue_pattern")->click();
    CHECK(f.banner_title().contains("1 Rework Function is on"));
    CHECK_FALSE(f.board.file().local_only());

    f.row("describe_pattern")->click();
    CHECK(f.board.file().rework_count == 0);
    CHECK(f.banner_title().startsWith("Local only"));
    for (auto* d : f.board.file().entries) CHECK(d->function_class == assistant::FunctionClass::Directive);

    // Re-enabling one Rework Function takes the guarantee away again.
    f.row("continue_pattern")->click();
    CHECK_FALSE(f.banner_title().startsWith("Local only"));
    CHECK(f.board.file().rework_count == 1);
}

TEST_CASE("a store that closed the privacy floor shows unset Rework rows off (section 4.3)") {
    assistant::FunctionToggles toggles =
        assistant::FunctionToggles::from_text("rework_pattern=off\n");
    ui::FunctionSwitchboard board(toggles, {});
    board.show();
    (void)QTest::qWaitForWindowExposed(&board);
    CHECK_FALSE(board.findChild<QCheckBox*>("rework_pattern")->isChecked());
    CHECK_FALSE(board.findChild<QCheckBox*>("continue_pattern")->isChecked());
    CHECK_FALSE(board.findChild<QCheckBox*>("describe_pattern")->isChecked());
    CHECK(board.findChild<QCheckBox*>("set_tempo")->isChecked());
    CHECK(board.file().local_only());
}

TEST_CASE("gated generation rows say so and stay out of the file until enabled") {
    SwitchboardFixture off;
    CHECK(off.row("generate_sample")->isChecked());  // the user's choice is on
    CHECK_FALSE(off.offers("generate_sample"));      // the capability filter drops it
    CHECK(labels_reading(off.board, "Not offered until you enable music generation.") == 2);

    SwitchboardFixture on{assistant::Capabilities{true}};
    CHECK(on.offers("generate_sample"));
    CHECK(labels_reading(on.board, "Not offered until you enable music generation.") == 0);
}

TEST_CASE("the settings dialog hosts the switchboard in a tab and relays its changes") {
    assistant::FunctionToggles toggles;
    StubGenerationStore generation;
    ui::SettingsDialog dialog(toggles, {}, generation);
    QSignalSpy relayed(&dialog, &ui::SettingsDialog::toggles_changed);
    dialog.show();
    (void)QTest::qWaitForWindowExposed(&dialog);

    QTabWidget* tabs = dialog.findChild<QTabWidget*>();
    REQUIRE(tabs);
    CHECK(tabs->tabText(0) == "Assistant Functions");

    dialog.findChild<QCheckBox*>("set_tempo")->click();
    CHECK(relayed.count() == 1);
    CHECK(toggles.state("set_tempo") == false);
}

TEST_CASE("MainWindow offers a Settings action with the platform preferences shortcut") {
    auto ids = make_fixture();
    ProjectHistory history(std::move(ids.project));
    StubTransport transport;
    StubAudition audition;
    StubRecorder recorder;
    assistant::FunctionToggles toggles;
    StubGenerationStore generation;
    ui::MainWindow window(history, transport, audition, recorder, toggles, generation, "Test");

    QAction* settings = nullptr;
    for (QAction* action : window.actions())
        if (action->text() == "Settings") settings = action;
    REQUIRE(settings);
    CHECK(settings->shortcut() == QKeySequence(QKeySequence::Preferences));
}

TEST_CASE("re-enabling one Rework row from a closed privacy floor changes only that row") {
    // The store closed the floor with one explicit off; the other two Rework
    // rows show off by the section 4.3 default. Clicking one of them on
    // must not re-open the third through the same default.
    assistant::FunctionToggles toggles =
        assistant::FunctionToggles::from_text("rework_pattern=off\n");
    ui::FunctionSwitchboard board(toggles, {});
    QSignalSpy changed(&board, &ui::FunctionSwitchboard::changed);
    board.show();
    (void)QTest::qWaitForWindowExposed(&board);
    REQUIRE(board.file().local_only());

    board.findChild<QCheckBox*>("continue_pattern")->click();
    CHECK(changed.count() == 1);
    CHECK(board.file().rework_count == 1);
    CHECK(board.findChild<QCheckBox*>("continue_pattern")->isChecked());
    CHECK_FALSE(board.findChild<QCheckBox*>("describe_pattern")->isChecked());
    CHECK_FALSE(board.findChild<QCheckBox*>("rework_pattern")->isChecked());
    // What was shown is now recorded, so a later build reads the same file.
    CHECK(toggles.to_text() ==
          "continue_pattern=on\ndescribe_pattern=off\nrework_pattern=off\n");
    CHECK(assistant::build(assistant::registry(), toggles, {}).rework_count == 1);
}

// ---------------------------------------------------------------------------
// The toggle file (src/app/toggle_file.cpp): persistence between sessions

namespace {

// Points QStandardPaths at a scratch tree and starts from no file at all.
struct ToggleFileFixture {
    ToggleFileFixture() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("fcs_toggle_file_test");
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
            .removeRecursively();
    }
    ~ToggleFileFixture() {
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
            .removeRecursively();
        QStandardPaths::setTestModeEnabled(false);
    }
};

}  // namespace

TEST_CASE("the toggle file is an empty store until something is saved") {
    ToggleFileFixture fx;
    assistant::FunctionToggles loaded = app::load_toggles();
    CHECK_FALSE(loaded.state("rework_pattern").has_value());
    CHECK(loaded.to_text().empty());
    CHECK(assistant::build(assistant::registry(), loaded, {}).entries.size() == 41);
}

TEST_CASE("the toggle file round-trips the store and the last save wins") {
    ToggleFileFixture fx;
    assistant::FunctionToggles toggles;
    toggles.set_enabled("rework_pattern", false);
    toggles.set_enabled("set_tempo", false);
    REQUIRE(app::save_toggles(toggles));

    assistant::FunctionToggles first = app::load_toggles();
    CHECK(first.to_text() == "rework_pattern=off\nset_tempo=off\n");
    CHECK(first.state("rework_pattern") == false);
    // Section 4.3 survives the round trip: the floor stays closed.
    CHECK(assistant::build(assistant::registry(), first, {}).local_only());

    // A second save replaces the file rather than appending to it.
    toggles.set_enabled("set_tempo", true);
    toggles.set_enabled("rework_pattern", true);
    REQUIRE(app::save_toggles(toggles));
    assistant::FunctionToggles second = app::load_toggles();
    CHECK(second.to_text() == "rework_pattern=on\nset_tempo=on\n");
    CHECK_FALSE(assistant::build(assistant::registry(), second, {}).local_only());
}

TEST_CASE("a hand-edited toggle file loads what parses and drops the rest") {
    ToggleFileFixture fx;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    REQUIRE(QDir().mkpath(dir));
    QFile file(dir + "/assistant_functions.txt");
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("describe_pattern=off\r\nnot a line\n../x=off\nset_tempo=off\n");
    file.close();

    assistant::FunctionToggles loaded = app::load_toggles();
    CHECK(loaded.to_text() == "describe_pattern=off\nset_tempo=off\n");
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("RecordBar lists the port's inputs and routes the pick to the port") {
    RecordFixture f;
    REQUIRE(f.inputs->count() == 3);
    CHECK(f.inputs->itemText(0) == "No input");
    CHECK(f.inputs->itemText(1) == "Mic (Stub)");
    CHECK(f.inputs->itemText(2) == "Broken (Stub)");
    CHECK_FALSE(f.record->isEnabled());  // nothing to record from yet

    f.pick(1);
    CHECK(f.recorder.selections == std::vector<int>{7});
    CHECK(f.inputs->currentIndex() == 1);
    CHECK(f.record->isEnabled());
    CHECK(f.last_hint() == "Recording from Mic (Stub). Press R to record.");
}

TEST_CASE("an input that will not open is reported and the pick reverts") {
    RecordFixture f;
    f.pick(2);
    CHECK(f.recorder.selections == std::vector<int>{9});
    CHECK(f.inputs->currentIndex() == 0);
    CHECK_FALSE(f.record->isEnabled());
    CHECK(f.last_hint() == "Could not open that input: busy");
}

TEST_CASE("record then stop adds the take as one Human Sample, undoable") {
    RecordFixture f;
    Project before = f.history.read();
    f.pick(1);

    QTest::mouseClick(f.record, Qt::LeftButton);
    CHECK(f.recorder.recording);
    CHECK(f.record->text() == "Stop Rec");
    CHECK(f.history.read().musical.samples.items.size() ==
          before.musical.samples.items.size());
    QLabel* length = nullptr;
    for (QLabel* label : f.bar.findChildren<QLabel*>())
        if (label->text() == "0:00.1") length = label;
    CHECK(length);

    QTest::mouseClick(f.record, Qt::LeftButton);
    CHECK_FALSE(f.recorder.recording);
    CHECK(f.record->text() == "Record");
    const auto& samples = f.history.read().musical.samples.items;
    REQUIRE(samples.size() == before.musical.samples.items.size() + 1);
    CHECK(samples.back().name == "Take 1");
    CHECK(samples.back().source == f.recorder.take);
    CHECK(samples.back().provenance.is_human());
    CHECK(f.history.state().undo_label == "Add Sample 'Take 1'");
    CHECK(f.last_hint() == "Recorded 'Take 1' (0.3 s).");

    // A second take gets the next free name.
    QTest::mouseClick(f.record, Qt::LeftButton);
    QTest::mouseClick(f.record, Qt::LeftButton);
    CHECK(f.history.read().musical.samples.items.back().name == "Take 2");

    REQUIRE(f.history.undo());
    REQUIRE(f.history.undo());
    CHECK(f.history.read() == before);
}

TEST_CASE("recording with no input chosen is refused with a hint") {
    RecordFixture f;
    // The button is disabled, so its shortcut is too; the port must still
    // refuse if asked directly, and nothing reaches the Project.
    CHECK_FALSE(f.recorder.start_recording());
    QTest::keyClick(&f.bar, Qt::Key_R);
    CHECK_FALSE(f.recorder.recording);
    CHECK(f.history.read().musical.samples.items.size() == 2);
}

// ---------------------------------------------------------------------------
// Music generation settings (issue #20, core document 6.4)

namespace {

struct PageFixture {
    StubGenerationStore store;
    std::unique_ptr<ui::GenerationSettingsPage> page;

    PageFixture() { open(); }

    void open() {
        page = std::make_unique<ui::GenerationSettingsPage>(store);
        page->show();
        (void)QTest::qWaitForWindowExposed(page.get());
    }

    template <class T>
    T* child(const QString& name) const {
        T* found = page->findChild<T*>(name);
        REQUIRE(found);
        return found;
    }

    void turn_on() { child<QPushButton>("generation_on")->click(); }
    QString problem() const { return child<QLabel>("generation_problem")->text(); }
};

}  // namespace

TEST_CASE("a fresh install opens the page off, with no model chosen") {
    PageFixture f;
    CHECK(f.child<QLabel>("generation_state")->text() == "OFF");
    for (QRadioButton* radio : f.page->findChildren<QRadioButton*>()) {
        CHECK_FALSE(radio->isChecked());
    }
    CHECK_FALSE(f.child<QCheckBox>("generation_acknowledge")->isChecked());
    CHECK_FALSE(f.child<QPushButton>("generation_off")->isVisible());
    CHECK(f.child<QLabel>("generation_warning")->isVisible());

    f.turn_on();
    CHECK(f.problem() == "Choose a model first.");
    CHECK_FALSE(f.store.kept.enabled);
}

TEST_CASE("every listed model shows its rights position, not just its name") {
    PageFixture f;
    QStringList texts;
    for (QLabel* label : f.page->findChildren<QLabel*>()) texts << label->text();
    for (const GenerationModel& model : handpicked_models()) {
        INFO(model.id);
        CHECK(f.child<QRadioButton>("generation_model_" + QString::fromStdString(model.id))
                  ->text() == QString::fromStdString(model.name));
        CHECK(texts.contains(QString::fromStdString(model.output_rights)));
        CHECK(texts.contains(QString::fromStdString(model.conditions)));
        CHECK(texts.contains(QString::fromStdString(model.leaves_machine)));
    }
}

TEST_CASE("a provisional entry is shown but cannot be chosen") {
    PageFixture f;
    CHECK_FALSE(f.child<QRadioButton>("generation_model_stable-audio-api")->isEnabled());
}

TEST_CASE("the local path: choose a model, name the weights folder, confirm, turn on") {
    PageFixture f;
    f.child<QRadioButton>("generation_model_stable-audio-3")->click();
    f.child<QLineEdit>("generation_weights_stable-audio-3")->setText("  C:/models/sa3 ");

    // The section 6.2 warning must be confirmed before anything is kept.
    f.turn_on();
    CHECK(f.problem() ==
          "Confirm that you understand generated output may not be licenseable.");
    CHECK_FALSE(f.store.kept.enabled);

    f.child<QCheckBox>("generation_acknowledge")->click();
    QSignalSpy changed(f.page.get(), &ui::GenerationSettingsPage::changed);
    f.turn_on();
    CHECK(changed.count() == 1);
    CHECK(f.store.kept == GenerationSettings{true, "stable-audio-3", "C:/models/sa3"});
    CHECK_FALSE(f.store.has_key());
    CHECK(f.child<QLabel>("generation_state")->text() == QString::fromUtf8("ON \xC2\xB7 Stable Audio 3"));
    CHECK(f.child<QPushButton>("generation_off")->isVisible());

    // Reopening reads the kept choice back.
    f.open();
    CHECK(f.child<QRadioButton>("generation_model_stable-audio-3")->isChecked());
    CHECK(f.child<QLineEdit>("generation_weights_stable-audio-3")->text() == "C:/models/sa3");
}

TEST_CASE("the local path needs the weights folder") {
    PageFixture f;
    f.child<QRadioButton>("generation_model_ace-step-1.5")->click();
    f.child<QCheckBox>("generation_acknowledge")->click();
    f.turn_on();
    CHECK(f.problem() == "Choose the folder holding the ACE-Step 1.5 weights.");
    CHECK_FALSE(f.store.kept.enabled);
}

TEST_CASE("the remote path: paste a key, confirm, turn on; the key never stays in the field") {
    PageFixture f;
    f.child<QRadioButton>("generation_model_elevenlabs-music")->click();
    f.child<QCheckBox>("generation_acknowledge")->click();
    f.turn_on();
    CHECK(f.problem() == "Paste your ElevenLabs Music API key.");

    QLineEdit* key = f.child<QLineEdit>("generation_key_elevenlabs-music");
    CHECK(key->echoMode() == QLineEdit::Password);
    key->setText(" xi-secret \n");
    f.turn_on();
    CHECK(f.store.kept == GenerationSettings{true, "elevenlabs-music", ""});
    CHECK(f.store.key_kept == "xi-secret");
    CHECK(key->text().isEmpty());
    CHECK(f.child<QLabel>("generation_key_state_elevenlabs-music")->text() ==
          "A key is kept. Paste to replace it.");

    // Off keeps the key, so turning back on needs nothing pasted again.
    f.open();
    f.child<QPushButton>("generation_off")->click();
    CHECK_FALSE(f.store.kept.enabled);
    CHECK(f.store.has_key());
    f.turn_on();
    CHECK(f.store.kept.enabled);
}

TEST_CASE("switching from a remote model to a local one drops the key") {
    PageFixture f;
    f.store.kept = {true, "elevenlabs-music", ""};
    f.store.key_kept = "xi-secret";
    f.open();
    f.child<QRadioButton>("generation_model_ace-step-1.5")->click();
    f.child<QLineEdit>("generation_weights_ace-step-1.5")->setText("/models/ace");
    f.turn_on();
    CHECK(f.store.kept == GenerationSettings{true, "ace-step-1.5", "/models/ace"});
    CHECK_FALSE(f.store.has_key());
}

TEST_CASE("a store that cannot be written is reported, not pretended") {
    PageFixture f;
    f.store.writable = false;
    f.child<QRadioButton>("generation_model_ace-step-1.5")->click();
    f.child<QLineEdit>("generation_weights_ace-step-1.5")->setText("/models/ace");
    f.child<QCheckBox>("generation_acknowledge")->click();
    f.turn_on();
    CHECK(f.problem().startsWith("The setting could not be saved."));
    CHECK(f.child<QLabel>("generation_state")->text() == "OFF");
}

TEST_CASE("a model removed from the list is explained and the page reads as off") {
    PageFixture f;
    f.store.kept = {false, "retired-model", ""};  // the store already turned it off
    f.open();
    CHECK(f.child<QLabel>("generation_state")->text() == "OFF");
    CHECK(f.child<QLabel>("generation_removed")->text().contains("retired-model"));
}

TEST_CASE("Generate with generation off guides the user to the settings page") {
    auto ids = make_fixture();
    ProjectHistory history(std::move(ids.project));
    StubTransport transport;
    StubAudition audition;
    StubRecorder recorder;
    assistant::FunctionToggles toggles;
    StubGenerationStore generation;
    ui::MainWindow window(history, transport, audition, recorder, toggles, generation, "Test");
    window.show();
    (void)QTest::qWaitForWindowExposed(&window);

    QToolButton* generate = nullptr;
    for (QToolButton* button : window.findChildren<QToolButton*>()) {
        if (button->text() == "Generate") generate = button;
    }
    REQUIRE(generate);
    CHECK(generate->isEnabled());  // gated, not absent

    generate->click();
    auto* dialog = window.findChild<ui::SettingsDialog*>();
    REQUIRE(dialog);
    CHECK(dialog->isVisible());
    auto* page = dialog->findChild<ui::GenerationSettingsPage*>();
    REQUIRE(page);
    CHECK(page->isVisible());  // the generation tab is the one shown
    QLabel* guidance = page->findChild<QLabel*>("generation_guidance");
    REQUIRE(guidance);
    CHECK(guidance->isVisible());
    CHECK(guidance->text().contains("music generation is off"));
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // With generation on, the same control no longer detours through settings.
    generation.kept = {true, "ace-step-1.5", "/models/ace"};
    generate->click();
    CHECK(window.findChild<ui::SettingsDialog*>() == nullptr);
}

namespace {

// Whether the Function file the dialog's switchboard built holds `name`.
bool offered(const ui::SettingsDialog& dialog, std::string_view name) {
    const auto& entries = dialog.switchboard()->file().entries;
    return std::any_of(entries.begin(), entries.end(),
                       [name](auto* d) { return d->name == name; });
}

}  // namespace

TEST_CASE("turning generation on in Settings makes generate_sample offered on the next open") {
    auto ids = make_fixture();
    ProjectHistory history(std::move(ids.project));
    StubTransport transport;
    StubAudition audition;
    StubRecorder recorder;
    assistant::FunctionToggles toggles;
    StubGenerationStore generation;
    ui::MainWindow window(history, transport, audition, recorder, toggles, generation, "Test");
    window.show();
    (void)QTest::qWaitForWindowExposed(&window);

    window.open_settings();
    auto* dialog = window.findChild<ui::SettingsDialog*>();
    REQUIRE(dialog);
    CHECK_FALSE(offered(*dialog, "generate_sample"));

    auto* page = dialog->findChild<ui::GenerationSettingsPage*>();
    REQUIRE(page);
    page->findChild<QRadioButton*>("generation_model_ace-step-1.5")->click();
    page->findChild<QLineEdit*>("generation_weights_ace-step-1.5")->setText("/models/ace");
    page->findChild<QCheckBox*>("generation_acknowledge")->click();
    page->findChild<QPushButton*>("generation_on")->click();
    CHECK(generation.kept.enabled);
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    window.open_settings();
    dialog = window.findChild<ui::SettingsDialog*>();
    REQUIRE(dialog);
    CHECK(offered(*dialog, "generate_sample"));
}

// Round-2 wiring: the capability is derived from the store, not set by
// hand, so a store that says "on" with a model no longer in the list must
// read as off everywhere it is derived (the switchboard and the Generate
// route), not only on the page that explains it.
TEST_CASE("a store that is on with a removed model derives no capability and still guides") {
    auto ids = make_fixture();
    ProjectHistory history(std::move(ids.project));
    StubTransport transport;
    StubAudition audition;
    StubRecorder recorder;
    assistant::FunctionToggles toggles;
    StubGenerationStore generation;
    generation.kept = {true, "model-that-was-removed", "/models/gone"};
    ui::MainWindow window(history, transport, audition, recorder, toggles, generation, "Test");
    window.show();
    (void)QTest::qWaitForWindowExposed(&window);

    window.request_generation();
    auto* dialog = window.findChild<ui::SettingsDialog*>();
    REQUIRE(dialog);
    CHECK(dialog->isVisible());
    // Opened, not executed: the surface that sent the user here is not blocked.
    CHECK(dialog->windowModality() != Qt::ApplicationModal);
    CHECK_FALSE(offered(*dialog, "generate_sample"));
    auto* page = dialog->findChild<ui::GenerationSettingsPage*>();
    REQUIRE(page);
    CHECK(page->isVisible());
    CHECK(page->findChild<QLabel*>("generation_removed") != nullptr);

    // Turning it off from the guided tab reaches the main window's status bar
    // through the dialog's relay, and the store agrees.
    page->findChild<QPushButton*>("generation_off")->click();
    CHECK_FALSE(generation.kept.enabled);
    CHECK(window.statusBar()->currentMessage() == "Music generation is off.");
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
