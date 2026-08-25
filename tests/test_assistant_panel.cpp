// The Assistant panel on the offscreen platform, over a scripted transport:
// a prompt becomes an undoable change in the Project; the transcript never
// shows a Function name; an ambiguous name is resolved by a click with no
// second request; and with no provider the panel only says a key is needed.
#include <catch2/catch_test_macros.hpp>

#include <QLineEdit>
#include <QTextEdit>
#include <QToolButton>
#include <QtTest/QtTest>

#include "assistant/registry.h"
#include "assistant/session.h"
#include "core/history.h"
#include "test_support.h"
#include "ui/assistant_panel.h"

using namespace assistant;
using test_support::make_fixture;

namespace {

struct ScriptedTransport : AssistantTransport {
    ModelReply reply;
    int requests = 0;
    ModelReply send(const Turn1&) override {
        ++requests;
        return reply;
    }
};

Selector named(std::string text) {
    Selector s;
    s.kind = Selector::Kind::Named;
    s.name = std::move(text);
    return s;
}

bool settles(const ui::AssistantPanel& panel) {
    return QTest::qWaitFor([&] { return !panel.busy(); }, 5000);
}

}  // namespace

TEST_CASE("a prompt becomes one undoable Pattern and the transcript names no Function") {
    auto f = make_fixture();
    core::ProjectHistory history(f.project);
    ScriptedTransport transport;
    transport.reply.text = "Added Drums B, empty, two bars.";
    transport.reply.tool_uses = {
        {"create_pattern", {{"name", std::string("Drums B")}, {"length_bars", 2ll}}}};
    AssistantSession session(registry(), transport);
    ui::AssistantPanel panel(history, &session, {});

    QTest::keyClicks(panel.input(), "make a new drum pattern");
    QTest::keyClick(panel.input(), Qt::Key_Return);
    CHECK(panel.busy());
    REQUIRE(settles(panel));

    REQUIRE(history.read().patterns.items.size() == 3);
    CHECK(history.read().patterns.items.back().name == "Drums B");
    CHECK(history.state().can_undo);
    QString shown = panel.transcript()->toPlainText();
    CHECK(shown.contains("make a new drum pattern"));
    CHECK(shown.contains("Added Drums B, empty, two bars."));
    for (const FunctionDescriptor& d : registry())
        CHECK_FALSE(shown.contains(QString::fromStdString(d.name)));
    CHECK(transport.requests == 1);

    history.undo();
    CHECK(history.read() == f.project);
}

TEST_CASE("an ambiguous name is settled by a click, locally") {
    auto f = make_fixture();
    f.project.patterns.items.push_back(f.project.patterns.items[0]);
    f.project.patterns.items.back().id = core::new_id();
    f.project.patterns.items.back().name = "Drums A2";
    core::ProjectHistory history(f.project);
    ScriptedTransport transport;
    transport.reply.tool_uses = {{"add_placement", {{"track", named("Track 1")},
                                                    {"pattern", named("Drums")},
                                                    {"bar", 1ll}}}};
    AssistantSession session(registry(), transport);
    ui::AssistantPanel panel(history, &session, {});
    panel.show();  // the choice buttons are found by visibility

    QTest::keyClicks(panel.input(), "place drums on track 1");
    QTest::keyClick(panel.input(), Qt::Key_Return);
    REQUIRE(QTest::qWaitFor(
        [&] { return panel.transcript()->toPlainText().contains("Which Pattern"); }, 5000));
    CHECK(panel.busy());
    CHECK(history.read().arrangements.items[0].tracks[0].placements.empty());

    QList<QToolButton*> buttons;
    for (QToolButton* b : panel.findChildren<QToolButton*>())
        if (b->isVisible() && b->text() != "Send") buttons.push_back(b);
    REQUIRE(buttons.size() == 2);
    CHECK(buttons[1]->text() == "Drums A2");
    QTest::mouseClick(buttons[1], Qt::LeftButton);

    REQUIRE(settles(panel));
    CHECK(transport.requests == 1);
    const core::Track& track = history.read().arrangements.items[0].tracks[0];
    REQUIRE(track.placements.size() == 1);
    CHECK(track.placements[0].pattern == f.project.patterns.items.back().id);
}

TEST_CASE("with no provider the panel says a key is required and accepts nothing") {
    auto f = make_fixture();
    core::ProjectHistory history(f.project);
    ui::AssistantPanel panel(history, nullptr, {});
    CHECK_FALSE(panel.input()->isEnabled());
    CHECK(panel.transcript()->toPlainText().contains("An API key is required"));
    CHECK_FALSE(panel.busy());
}
