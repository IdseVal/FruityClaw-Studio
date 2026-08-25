// The Assistant machinery, headless, against a scripted transport: the
// Function file is a pure projection of the registry through the toggles;
// turn 1 carries no Project content; Selectors resolve locally and
// ambiguity goes to the user, never back to the model; every executed
// Function is one undoable Delta; the user is never shown a Function name;
// and no whole-song or settings Function exists.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

#include "assistant/function_file.h"
#include "assistant/registry.h"
#include "assistant/session.h"
#include "assistant/transport.h"
#include "core/history.h"
#include "test_support.h"

using namespace assistant;
using core::Id;
using test_support::make_fixture;

namespace {

Selector named(std::string text) {
    Selector s;
    s.kind = Selector::Kind::Named;
    s.name = std::move(text);
    return s;
}

Selector focused() {
    Selector s;
    s.kind = Selector::Kind::Focused;
    return s;
}

Selector last_created() {
    Selector s;
    s.kind = Selector::Kind::LastCreated;
    return s;
}

Selector ordinal(int n) {
    Selector s;
    s.kind = Selector::Kind::Ordinal;
    s.ordinal = n;
    return s;
}

// Replays a fixed reply and records what was sent.
struct ScriptedTransport : AssistantTransport {
    ModelReply reply;
    std::vector<std::string> prompts_seen;
    std::vector<std::string> tools_seen;  // wire JSON of each request's file
    int requests = 0;

    ModelReply send(const Turn1& request) override {
        ++requests;
        prompts_seen.push_back(request.prompt);
        tools_seen.push_back(to_wire_json(*request.tools));
        return reply;
    }
};

// Applies an outcome the way the panel does: in order, through the History.
void apply_all(core::ProjectHistory& history, const TurnOutcome& outcome) {
    for (const core::Delta& delta : outcome.deltas) history.apply(delta);
}

const std::set<std::string> kVerbs = {"create", "delete", "rename", "duplicate", "add", "remove",
                                      "clear", "set", "move", "place", "resize", "transpose",
                                      "quantize", "generate", "rework", "continue", "describe"};

}  // namespace

// --- the registry and the file ------------------------------------------------

TEST_CASE("every Function name is verb_object from the closed verb set, and none is blunt") {
    for (const FunctionDescriptor& d : registry()) {
        std::string verb = d.name.substr(0, d.name.find('_'));
        INFO(d.name);
        CHECK(kVerbs.count(verb) == 1);
        CHECK(d.name.find("project") == std::string::npos);
        CHECK(d.name.find("setting") == std::string::npos);
        CHECK(d.name.find("arrangement") == std::string::npos);  // no rework_arrangement
        CHECK_FALSE(d.effect.empty());
        CHECK_FALSE(d.instruction.empty());
        for (const Property& p : d.schema.properties) {
            // The forbidden argument shapes of spec 2.2.
            CHECK(p.name != "json");
            CHECK(p.name != "patch");
            CHECK(p.name != "data");
            CHECK(p.name != "payload");
            CHECK(p.name != "ops");
            CHECK(p.name != "body");
            CHECK(p.name != "op");
            CHECK(p.name != "action");
        }
    }
}

TEST_CASE("the Function file is the registry minus the switched-off names, in order") {
    Toggles all;
    FunctionFile file = build(registry(), all);
    REQUIRE(file.entries.size() == registry().size());
    for (std::size_t i = 0; i < file.entries.size(); ++i)
        CHECK(file.entries[i].name == registry()[i].name);
    CHECK(file.rework_count == 0);

    Toggles some;
    some.disabled = {"set_part_instrument", "delete_track"};
    FunctionFile smaller = build(registry(), some);
    CHECK(smaller.entries.size() == registry().size() - 2);
    for (const ToolDefinition& entry : smaller.entries) {
        CHECK(entry.name != "set_part_instrument");
        CHECK(entry.name != "delete_track");
    }
    // Absent, not marked: the wire form has no trace of the name at all.
    std::string wire = to_wire_json(smaller);
    CHECK(wire.find("set_part_instrument") == std::string::npos);
    CHECK(wire.find("disabled") == std::string::npos);
    CHECK(smaller.manifest_hash != file.manifest_hash);
    CHECK(build(registry(), some).manifest_hash == smaller.manifest_hash);  // pure
}

TEST_CASE("the wire form is strict and closed") {
    std::string wire = to_wire_json(build(registry(), Toggles{}));
    CHECK(wire.find("\"strict\":true") != std::string::npos);
    CHECK(wire.find("\"additionalProperties\":false") != std::string::npos);
    // Every property is required: the required list of create_pattern names both.
    CHECK(wire.find("\"required\":[\"name\",\"length_bars\"]") != std::string::npos);
    // A Selector carries no id, only how the user referred to the entity.
    CHECK(wire.find("\"by\":{\"type\":\"string\",\"enum\":[\"focused\",\"ordinal\",\"named\","
                    "\"last_created\"]") != std::string::npos);
    CHECK(wire.find("\"id\"") == std::string::npos);
}

TEST_CASE("argument validation refuses out-of-range and off-schema values locally") {
    const FunctionDescriptor* create = find(registry(), "create_pattern");
    REQUIRE(create);
    CHECK_FALSE(validate(create->schema, {{"name", std::string("x")}, {"length_bars", 4ll}}));
    CHECK(validate(create->schema, {{"name", std::string("x")}, {"length_bars", 9000ll}}));
    CHECK(validate(create->schema, {{"name", std::string("x")}}));
    CHECK(validate(create->schema, {{"name", std::string("x")}, {"length_bars", 4ll},
                                    {"json", std::string("{}")}}));

    const FunctionDescriptor* steps = find(registry(), "set_part_steps");
    REQUIRE(steps);
    Args ok{{"pattern", named("Drums A")}, {"instrument", std::monostate{}},
            {"steps", std::vector<long long>{0, 4}}};
    CHECK_FALSE(validate(steps->schema, ok));
    Args bad = ok;
    bad["steps"] = std::vector<long long>{0, -4};
    CHECK(validate(steps->schema, bad));
    Args nameless = ok;
    nameless["pattern"] = named("");
    CHECK(validate(steps->schema, nameless));
}

// --- turns ---------------------------------------------------------------------

TEST_CASE("a user asks for a Pattern and gets one; turn 1 carried no Project content") {
    auto f = make_fixture();
    core::ProjectHistory history(f.project);
    ScriptedTransport transport;
    transport.reply.text = "Added an empty two-bar Pattern called Drums B.";
    transport.reply.tool_uses = {
        {"create_pattern", {{"name", std::string("Drums B")}, {"length_bars", 2ll}}}};
    AssistantSession session(registry(), transport);

    TurnOutcome outcome = session.run_turn("make me a new two bar drum pattern called Drums B",
                                           history.read(), Toggles{}, Focus{});
    REQUIRE(outcome.deltas.size() == 1);
    CHECK_FALSE(outcome.pending);
    CHECK(outcome.manifest_hash == build(registry(), Toggles{}).manifest_hash);
    apply_all(history, outcome);
    REQUIRE(history.read().patterns.items.size() == 3);
    CHECK(history.read().patterns.items.back().name == "Drums B");

    // Nothing but the prompt and the tool list went out.
    REQUIRE(transport.requests == 1);
    CHECK(transport.prompts_seen[0] == "make me a new two bar drum pattern called Drums B");
    CHECK(transport.tools_seen[0].find("Drums A") == std::string::npos);
    CHECK(transport.tools_seen[0].find("Melody A") == std::string::npos);
    CHECK(transport.tools_seen[0].find("Track 1") == std::string::npos);
    CHECK(transport.tools_seen[0].find("Test") == std::string::npos);  // the title

    // Undoable, like any other change (core document 9.7).
    CHECK(history.state().undo_label == "Add Pattern 'Drums B'");
    history.undo();
    CHECK(history.read() == f.project);
}

TEST_CASE("two Functions in one reply chain through LastCreated, each its own Delta") {
    auto f = make_fixture();
    core::ProjectHistory history(f.project);
    ScriptedTransport transport;
    transport.reply.tool_uses = {
        {"create_pattern", {{"name", std::string("Hats")}, {"length_bars", 1ll}}},
        {"add_part", {{"pattern", last_created()}, {"instrument", named("drum")}}},
        {"set_part_steps", {{"pattern", last_created()}, {"instrument", std::monostate{}},
                            {"steps", std::vector<long long>{0, 2, 4, 6, 8, 10, 12, 14}}}},
    };
    AssistantSession session(registry(), transport);

    TurnOutcome outcome = session.run_turn("give me a hi-hat pattern", history.read(),
                                           Toggles{}, Focus{});
    REQUIRE(outcome.deltas.size() == 3);
    apply_all(history, outcome);
    const core::Pattern& hats = history.read().patterns.items.back();
    CHECK(hats.name == "Hats");
    REQUIRE(hats.parts.size() == 1);
    CHECK(hats.parts[0].instrument == f.project.instruments.items[0].id);
    CHECK(hats.parts[0].events.size() == 8);
    CHECK(outcome.text == "Done.");

    // Three presses of undo, one per Function (history contract 7.1).
    CHECK(history.undo());
    CHECK(history.undo());
    CHECK(history.undo());
    CHECK_FALSE(history.undo());
    CHECK(history.read() == f.project);
}

TEST_CASE("an Instrument swap on the focused Pattern touches no Event") {
    auto f = make_fixture();
    core::ProjectHistory history(f.project);
    ScriptedTransport transport;
    transport.reply.tool_uses = {{"set_part_instrument",
                                  {{"pattern", focused()}, {"current", std::monostate{}},
                                   {"instrument", named("dru")}}}};
    AssistantSession session(registry(), transport);
    Focus focus;
    focus.pattern = f.melody_pattern;

    TurnOutcome outcome = session.run_turn("swap the instrument on this to the drum",
                                           history.read(), Toggles{}, focus);
    REQUIRE(outcome.deltas.size() == 1);
    apply_all(history, outcome);
    const core::Part& lane = history.read().patterns.find(f.melody_pattern)->parts[0];
    CHECK(lane.instrument == f.project.instruments.items[0].id);
    CHECK(lane.events == f.project.patterns.find(f.melody_pattern)->parts[0].events);
}

TEST_CASE("a Sample is placed: Instrument over the Sample, then a lane in a Pattern") {
    auto f = make_fixture();
    core::ProjectHistory history(f.project);
    ScriptedTransport transport;
    transport.reply.tool_uses = {
        {"create_instrument", {{"name", std::string("Tone 2")}, {"sample", named("tone")},
                               {"mode", std::string("one_shot")}}},
        {"add_part", {{"pattern", ordinal(1)}, {"instrument", last_created()}}},
    };
    AssistantSession session(registry(), transport);

    TurnOutcome outcome = session.run_turn("put the tone sample in the first pattern",
                                           history.read(), Toggles{}, Focus{});
    REQUIRE(outcome.deltas.size() == 2);
    apply_all(history, outcome);
    const core::Instrument& made = history.read().instruments.items.back();
    CHECK(made.params.sample == f.project.samples.items[1].id);
    CHECK(made.params.mode == core::SamplerMode::OneShot);
    CHECK(history.read().patterns.find(f.drum_pattern)->parts.back().instrument == made.id);
}

TEST_CASE("an ambiguous name is answered by the user, with no second request") {
    auto f = make_fixture();
    f.project.patterns.items.push_back(f.project.patterns.items[0]);
    f.project.patterns.items.back().id = core::new_id();
    f.project.patterns.items.back().name = "Drums A2";
    core::ProjectHistory history(f.project);

    ScriptedTransport transport;
    transport.reply.text = "Muted it.";
    transport.reply.tool_uses = {
        {"add_placement", {{"track", named("Track 1")}, {"pattern", named("Drums")}, {"bar", 3ll}}},
        {"set_track_muted", {{"track", named("Track 1")}, {"muted", true}}},
    };
    AssistantSession session(registry(), transport);

    TurnOutcome stalled = session.run_turn("place drums on track 1 at bar 3 and mute it",
                                           history.read(), Toggles{}, Focus{});
    REQUIRE(stalled.pending);
    CHECK(stalled.deltas.empty());
    CHECK(stalled.pending->question == "Which Pattern did you mean by 'Drums'?");
    REQUIRE(stalled.pending->candidates.size() == 2);
    CHECK(stalled.pending->candidates[0].name == "Drums A");
    CHECK(stalled.pending->candidates[1].name == "Drums A2");
    CHECK(transport.requests == 1);

    TurnOutcome resumed = session.resume(*stalled.pending, stalled.pending->candidates[1].id,
                                         history.read());
    CHECK(transport.requests == 1);  // still: answered locally
    REQUIRE(resumed.deltas.size() == 2);
    CHECK(resumed.text == "Muted it.");
    apply_all(history, resumed);
    const core::Track& track = history.read().arrangements.items[0].tracks[0];
    REQUIRE(track.placements.size() == 1);
    CHECK(track.placements[0].pattern == f.project.patterns.items.back().id);
    CHECK(track.placements[0].start == 2 * 4 * core::kPpq);
    CHECK(track.muted);
}

TEST_CASE("a name that matches nothing ends the turn with a plain message") {
    auto f = make_fixture();
    ScriptedTransport transport;
    transport.reply.tool_uses = {{"delete_track", {{"track", named("Vocals")}}}};
    AssistantSession session(registry(), transport);
    TurnOutcome outcome = session.run_turn("delete the vocals", f.project, Toggles{}, Focus{});
    CHECK(outcome.deltas.empty());
    CHECK(outcome.text == "No Track called 'Vocals'.");
}

TEST_CASE("a switched-off Function is not offered, and a reply naming it is malformed") {
    auto f = make_fixture();
    Toggles toggles;
    toggles.disabled = {"delete_track"};
    ScriptedTransport transport;
    transport.reply.tool_uses = {{"delete_track", {{"track", ordinal(1)}}}};
    AssistantSession session(registry(), transport);

    TurnOutcome outcome = session.run_turn("delete track 1", f.project, toggles, Focus{});
    CHECK(transport.tools_seen[0].find("delete_track") == std::string::npos);
    CHECK(outcome.deltas.empty());
    CHECK(outcome.text.find("Nothing was changed") != std::string::npos);
    CHECK(outcome.text.find("delete_track") == std::string::npos);
    CHECK(outcome.text.find("disabled") == std::string::npos);
}

TEST_CASE("an unreachable provider is reported without a Delta") {
    auto f = make_fixture();
    ScriptedTransport transport;
    transport.reply.error = "no network";
    AssistantSession session(registry(), transport);
    TurnOutcome outcome = session.run_turn("hello", f.project, Toggles{}, Focus{});
    CHECK(outcome.deltas.empty());
    CHECK(outcome.text == "The Assistant could not be reached: no network");
}

TEST_CASE("the user is never shown a Function name, whatever the model says") {
    auto f = make_fixture();
    ScriptedTransport transport;
    transport.reply.text = "I used create_track to add it, then set_track_muted.";
    transport.reply.tool_uses = {{"create_track", {{"name", std::string("FX")}}}};
    AssistantSession session(registry(), transport);
    TurnOutcome outcome = session.run_turn("add an FX track", f.project, Toggles{}, Focus{});
    for (const FunctionDescriptor& d : registry()) {
        INFO(outcome.text);
        CHECK(outcome.text.find(d.name) == std::string::npos);
    }
    // Delta labels are content, not Function identity (history contract 7.3).
    for (const core::Delta& delta : outcome.deltas)
        for (const FunctionDescriptor& d : registry())
            CHECK(delta.label.find(d.name) == std::string::npos);
    CHECK(outcome.deltas[0].origin == core::Origin::Assistant);
}

TEST_CASE("a Function whose target vanished before apply fails cleanly in the History") {
    auto f = make_fixture();
    core::ProjectHistory history(f.project);
    ScriptedTransport transport;
    transport.reply.tool_uses = {{"rename_track", {{"track", named("Track 2")},
                                                   {"name", std::string("Lead")}}}};
    AssistantSession session(registry(), transport);
    TurnOutcome outcome = session.run_turn("rename track 2 to Lead", history.read(), Toggles{},
                                           Focus{});
    REQUIRE(outcome.deltas.size() == 1);

    // The user deleted Track 2 while the request was in flight.
    auto gone = core::functions::delete_track(history.read(), f.arrangement, f.track_b);
    history.apply(*gone);
    CHECK(history.apply(outcome.deltas[0]) == core::ApplyResult::Failed);
    CHECK(history.state().undo_label == "Delete Track 'Track 2'");
}

// --- Selector resolution rules (function-surface spec section 2.3) -------------

TEST_CASE("Selectors resolve locally: focus, ordinal, exact-over-partial, last created") {
    auto f = make_fixture();
    const core::Project& p = f.project;
    Focus focus;
    focus.pattern = f.melody_pattern;
    Focus created;
    created.track = f.track_b;

    SECTION("Focused resolves to the focused slot of that kind, and nothing else") {
        auto r = resolve(focused(), EntityKind::Pattern, p, focus, created);
        REQUIRE(std::holds_alternative<Id>(r));
        CHECK(std::get<Id>(r) == f.melody_pattern);
        auto none = resolve(focused(), EntityKind::Track, p, focus, created);
        REQUIRE(std::holds_alternative<NotFound>(none));
        CHECK(std::get<NotFound>(none).message == "No Track is selected");
    }
    SECTION("LastCreated resolves per kind and is empty for other kinds") {
        auto r = resolve(last_created(), EntityKind::Track, p, focus, created);
        REQUIRE(std::holds_alternative<Id>(r));
        CHECK(std::get<Id>(r) == f.track_b);
        auto none = resolve(last_created(), EntityKind::Instrument, p, focus, created);
        REQUIRE(std::holds_alternative<NotFound>(none));
        CHECK(std::get<NotFound>(none).message == "No Instrument was just created");
    }
    SECTION("Ordinal is 1-based in authored order and refuses out-of-range positions") {
        auto second = resolve(ordinal(2), EntityKind::Track, p, focus, created);
        REQUIRE(std::holds_alternative<Id>(second));
        CHECK(std::get<Id>(second) == f.track_b);
        CHECK(std::holds_alternative<NotFound>(resolve(ordinal(0), EntityKind::Track, p, focus, created)));
        auto beyond = resolve(ordinal(3), EntityKind::Track, p, focus, created);
        REQUIRE(std::holds_alternative<NotFound>(beyond));
        CHECK(std::get<NotFound>(beyond).message == "There is no Track number 3");
    }
    SECTION("Named matches case-insensitively and an exact match beats partial matches") {
        auto r = resolve(named("drums a"), EntityKind::Pattern, p, focus, created);
        REQUIRE(std::holds_alternative<Id>(r));
        CHECK(std::get<Id>(r) == f.drum_pattern);

        core::Project two = p;
        two.patterns.items.push_back(two.patterns.items[0]);
        two.patterns.items.back().id = core::new_id();
        two.patterns.items.back().name = "Drums AB";
        auto exact = resolve(named("Drums A"), EntityKind::Pattern, two, focus, created);
        REQUIRE(std::holds_alternative<Id>(exact));
        CHECK(std::get<Id>(exact) == f.drum_pattern);
        auto partial = resolve(named("drums"), EntityKind::Pattern, two, focus, created);
        REQUIRE(std::holds_alternative<Ambiguous>(partial));
        CHECK(std::get<Ambiguous>(partial).candidates.size() == 2);
    }
    SECTION("Named finds a Sample and an Instrument the same way") {
        auto s = resolve(named("Tone"), EntityKind::Sample, p, focus, created);
        REQUIRE(std::holds_alternative<Id>(s));
        CHECK(std::get<Id>(s) == p.samples.items[1].id);
        auto i = resolve(named("keys"), EntityKind::Instrument, p, focus, created);
        REQUIRE(std::holds_alternative<Id>(i));
        CHECK(std::get<Id>(i) == p.instruments.items[1].id);
    }
}

TEST_CASE("a Function with two ambiguous Selectors is settled by two answers, not a loop") {
    auto f = make_fixture();
    // A second "Drums"-ish Pattern and a second "Drum"-ish Instrument.
    f.project.patterns.items.push_back(f.project.patterns.items[0]);
    f.project.patterns.items.back().id = core::new_id();
    f.project.patterns.items.back().name = "Drums B";
    f.project.patterns.items.back().parts.clear();
    f.project.instruments.items.push_back(f.project.instruments.items[0]);
    f.project.instruments.items.back().id = core::new_id();
    f.project.instruments.items.back().name = "Drum 2";
    core::ProjectHistory history(f.project);

    ScriptedTransport transport;
    transport.reply.text = "Added the lane.";
    transport.reply.tool_uses = {
        {"add_part", {{"pattern", named("Drums")}, {"instrument", named("dru")}}}};
    AssistantSession session(registry(), transport);

    TurnOutcome first = session.run_turn("add drum to drums", history.read(), Toggles{}, Focus{});
    REQUIRE(first.pending);
    CHECK(first.pending->argument == "pattern");
    Id pattern = f.project.patterns.items.back().id;

    TurnOutcome second = session.resume(*first.pending, pattern, history.read());
    REQUIRE(second.pending);
    CHECK(second.pending->argument == "instrument");
    Id instrument = f.project.instruments.items.back().id;

    // The second answer must not throw the first away: the turn completes.
    TurnOutcome done = session.resume(*second.pending, instrument, history.read());
    CHECK_FALSE(done.pending);
    REQUIRE(done.deltas.size() == 1);
    CHECK(transport.requests == 1);
    apply_all(history, done);
    const core::Pattern* pat = history.read().patterns.find(pattern);
    REQUIRE(pat);
    REQUIRE(pat->parts.size() == 1);
    CHECK(pat->parts[0].instrument == instrument);
}

TEST_CASE("an answer given for one Function is not reused for the next Function's Selector") {
    auto f = make_fixture();
    f.project.patterns.items.push_back(f.project.patterns.items[0]);
    f.project.patterns.items.back().id = core::new_id();
    f.project.patterns.items.back().name = "Drums B";
    core::ProjectHistory history(f.project);

    ScriptedTransport transport;
    transport.reply.text = "Placed both.";
    // Two Functions, each with an ambiguous `pattern`: the user must be asked
    // twice, once per Function, and the first answer must not silently
    // resolve the second.
    transport.reply.tool_uses = {
        {"add_placement", {{"track", named("Track 1")}, {"pattern", named("Drums")}, {"bar", 1ll}}},
        {"add_placement", {{"track", named("Track 1")}, {"pattern", named("Drums")}, {"bar", 5ll}}},
    };
    AssistantSession session(registry(), transport);
    Id drums_a = f.project.patterns.items[0].id;
    Id drums_b = f.project.patterns.items.back().id;

    TurnOutcome first = session.run_turn("place drums at bars 1 and 5", history.read(),
                                         Toggles{}, Focus{});
    REQUIRE(first.pending);
    CHECK(first.pending->argument == "pattern");
    CHECK(first.pending->remaining.size() == 2);
    CHECK(first.pending->answered.empty());

    TurnOutcome second = session.resume(*first.pending, drums_a, history.read());
    REQUIRE(second.pending);
    CHECK(second.pending->argument == "pattern");
    CHECK(second.pending->remaining.size() == 1);
    CHECK(second.pending->answered.empty());  // Function 0's answer stayed with Function 0
    // Function 0 completed and its Delta travels with the stall, as the panel applies it.
    REQUIRE(second.deltas.size() == 1);
    apply_all(history, second);

    TurnOutcome done = session.resume(*second.pending, drums_b, history.read());
    CHECK_FALSE(done.pending);
    REQUIRE(done.deltas.size() == 1);
    CHECK(transport.requests == 1);
    apply_all(history, done);
    const core::Track& track = history.read().arrangements.items[0].tracks[0];
    REQUIRE(track.placements.size() == 2);
    CHECK(track.placements[0].pattern == drums_a);
    CHECK(track.placements[1].pattern == drums_b);
}
