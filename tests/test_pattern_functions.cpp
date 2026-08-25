// The step-sequencer catalogue: each function does its one thing, touches
// nothing else, and undoes exactly. The fixture's drum lane has hits on the
// four beats — steps 0, 4, 8, 12 at velocity 100.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "core/history.h"
#include "core/pattern_functions.h"
#include "core/sample_functions.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

namespace {

// The step indices of a lane's on-grid Events, in start order.
std::vector<int> steps_of(const Part& lane) {
    std::vector<int> steps;
    for (const Event& e : lane.events)
        if (e.start % kStepTicks == 0) steps.push_back(static_cast<int>(e.start / kStepTicks));
    std::sort(steps.begin(), steps.end());
    return steps;
}

}  // namespace

TEST_CASE("create_pattern adds one empty Pattern of the requested length") {
    auto f = make_fixture();
    ProjectHistory history(f.project);

    auto result = create_pattern(history.read(), "Drums B", 2, Origin::Assistant);
    REQUIRE(result.ok());
    REQUIRE(history.apply(result->delta) == ApplyResult::Applied);

    const Pattern* created = history.read().patterns.find(result->id);
    REQUIRE(created);
    CHECK(created->name == "Drums B");
    CHECK(created->length == 2 * 4 * kPpq);
    CHECK(created->parts.empty());
    CHECK(created->provenance.is_human());
    CHECK(history.read().patterns.items.size() == 3);
    CHECK(history.read().arrangements.items.front().tracks[0].placements.empty());
    CHECK(history.state().undo_label == "Add Pattern 'Drums B'");

    history.undo();
    CHECK(history.read().patterns.items.size() == 2);
    CHECK(history.read() == f.project);
}

TEST_CASE("create_pattern rejects an out-of-range length") {
    auto f = make_fixture();
    CHECK_FALSE(create_pattern(f.project, "x", 0).ok());
    CHECK_FALSE(create_pattern(f.project, "x", 65).ok());
}

TEST_CASE("rename_pattern changes the name and nothing else") {
    auto f = make_fixture();
    ProjectHistory history(f.project);

    auto result = rename_pattern(history.read(), f.drum_pattern, "Beat");
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);
    const Pattern* drums = history.read().patterns.find(f.drum_pattern);
    CHECK(drums->name == "Beat");
    CHECK(drums->parts == f.project.patterns.find(f.drum_pattern)->parts);
    CHECK(history.state().undo_label == "Rename Pattern to 'Beat'");

    CHECK(rename_pattern(history.read(), f.drum_pattern, "").error == "A Pattern needs a name");
    CHECK(rename_pattern(history.read(), new_id(), "x").error == "No such Pattern");

    history.undo();
    CHECK(history.read() == f.project);
}

TEST_CASE("set_part_steps rewrites one lane, keeps velocities of surviving steps") {
    auto f = make_fixture();
    ProjectHistory history(f.project);
    const Pattern* drums = history.read().patterns.find(f.drum_pattern);
    Id part = drums->parts[0].id;
    Id first_hit = drums->parts[0].events[0].id;

    auto result = set_part_steps(history.read(), f.drum_pattern, part, {15, 0, 2, 2});
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);

    const Part& lane = history.read().patterns.find(f.drum_pattern)->parts[0];
    REQUIRE(lane.events.size() == 3);
    CHECK(lane.events[0].id == first_hit);  // step 0 survived with its Event
    CHECK(lane.events[1].start == 2 * kStepTicks);
    CHECK(lane.events[1].velocity == 100);
    CHECK(lane.events[2].start == 15 * kStepTicks);
    CHECK(lane.instrument == f.project.patterns.find(f.drum_pattern)->parts[0].instrument);
    CHECK(history.state().undo_label == "Set steps of 'Drum' in 'Drums A'");
    // The melody Pattern is untouched.
    CHECK(*history.read().patterns.find(f.melody_pattern) ==
          *f.project.patterns.find(f.melody_pattern));

    history.undo();
    CHECK(history.read() == f.project);
}

TEST_CASE("set_part_steps leaves Events off the grid where they are") {
    auto f = make_fixture();
    Pattern* drums = f.project.patterns.find(f.drum_pattern);
    Event swung{new_id(), kStepTicks / 2, kPpq / 8, 60, 80};  // between steps 0 and 1
    drums->parts[0].events.push_back(swung);
    Id part = drums->parts[0].id;
    ProjectHistory history(f.project);

    auto result = set_part_steps(history.read(), f.drum_pattern, part, {4});
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);
    const Part& lane = history.read().patterns.find(f.drum_pattern)->parts[0];
    REQUIRE(lane.events.size() == 2);
    CHECK(lane.events[0] == swung);
    CHECK(steps_of(lane) == std::vector<int>{4});

    auto cleared = clear_part_steps(history.read(), f.drum_pattern, part);
    REQUIRE(cleared.ok());
    REQUIRE(history.apply(*cleared) == ApplyResult::Applied);
    const Part& empty = history.read().patterns.find(f.drum_pattern)->parts[0];
    REQUIRE(empty.events.size() == 1);
    CHECK(empty.events[0] == swung);
}

TEST_CASE("set_part_steps refuses a step beyond the Pattern and a missing lane") {
    auto f = make_fixture();
    Id part = f.project.patterns.find(f.drum_pattern)->parts[0].id;
    CHECK(set_part_steps(f.project, f.drum_pattern, part, {16}).error == "'Drums A' has 16 steps");
    CHECK_FALSE(set_part_steps(f.project, f.drum_pattern, part, {-1}).ok());
    CHECK(set_part_steps(f.project, f.drum_pattern, new_id(), {0}).error ==
          "That Instrument is not in 'Drums A'");
    CHECK(set_part_steps(f.project, new_id(), part, {0}).error == "No such Pattern");
}

TEST_CASE("set_part_step_velocities changes velocity only, on steps that are on") {
    auto f = make_fixture();
    ProjectHistory history(f.project);
    Id part = history.read().patterns.find(f.drum_pattern)->parts[0].id;

    auto result = set_part_step_velocities(history.read(), f.drum_pattern, part,
                                           {{0, 127}, {8, 40}});
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);
    const Part& lane = history.read().patterns.find(f.drum_pattern)->parts[0];
    const Part& before = f.project.patterns.find(f.drum_pattern)->parts[0];
    REQUIRE(lane.events.size() == 4);
    CHECK(lane.events[0].velocity == 127);
    CHECK(lane.events[2].velocity == 40);
    CHECK(lane.events[1] == before.events[1]);
    CHECK(lane.events[0].id == before.events[0].id);
    CHECK(lane.events[0].start == before.events[0].start);
    CHECK(steps_of(lane) == steps_of(before));
    CHECK(history.state().undo_label == "Set velocity of 'Drum' in 'Drums A'");

    // A step that is off is not turned on by a velocity.
    CHECK(set_part_step_velocities(history.read(), f.drum_pattern, part, {{1, 90}}).error ==
          "Step 2 of 'Drum' is off");
    CHECK(set_part_step_velocities(history.read(), f.drum_pattern, part, {{0, 0}}).error ==
          "Velocity must be 1 to 127");

    // The same velocity again is a no-op, not an entry.
    auto same = set_part_step_velocities(history.read(), f.drum_pattern, part, {{0, 127}});
    CHECK(history.apply(*same) == ApplyResult::NoChange);

    history.undo();
    CHECK(history.read() == f.project);
}

TEST_CASE("clear_part_steps empties one lane and undo restores it exactly") {
    auto f = make_fixture();
    ProjectHistory history(f.project);
    Id part = history.read().patterns.find(f.drum_pattern)->parts[0].id;

    auto result = clear_part_steps(history.read(), f.drum_pattern, part);
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);
    const Pattern* drums = history.read().patterns.find(f.drum_pattern);
    REQUIRE(drums->parts.size() == 1);
    CHECK(drums->parts[0].events.empty());
    CHECK(history.state().undo_label == "Clear steps of 'Drum' in 'Drums A'");

    auto again = clear_part_steps(history.read(), f.drum_pattern, part);
    CHECK(history.apply(*again) == ApplyResult::NoChange);

    history.undo();
    CHECK(history.read() == f.project);
}

TEST_CASE("set_part_muted flips one flag and leaves the Events alone") {
    auto f = make_fixture();
    ProjectHistory history(f.project);
    Id part = history.read().patterns.find(f.drum_pattern)->parts[0].id;

    auto result = set_part_muted(history.read(), f.drum_pattern, part, true);
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);
    const Part& lane = history.read().patterns.find(f.drum_pattern)->parts[0];
    CHECK(lane.muted);
    CHECK(lane.events == f.project.patterns.find(f.drum_pattern)->parts[0].events);
    CHECK(history.state().undo_label == "Mute 'Drum' in 'Drums A'");

    auto back = set_part_muted(history.read(), f.drum_pattern, part, false);
    REQUIRE(history.apply(*back) == ApplyResult::Applied);
    CHECK(history.state().undo_label == "Unmute 'Drum' in 'Drums A'");
    CHECK_FALSE(set_part_muted(history.read(), f.drum_pattern, new_id(), true).ok());

    history.undo();
    history.undo();
    CHECK(history.read() == f.project);
}

TEST_CASE("remove_part takes one lane out and undo puts it back in place") {
    auto f = make_fixture();
    ProjectHistory history(f.project);
    Id keys = f.project.instruments.items[1].id;
    auto added = add_part(history.read(), f.drum_pattern, keys);
    REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);
    Project with_two_lanes = history.read();
    Id first = with_two_lanes.patterns.find(f.drum_pattern)->parts[0].id;

    auto removed = remove_part(history.read(), f.drum_pattern, first);
    REQUIRE(removed.ok());
    REQUIRE(history.apply(*removed) == ApplyResult::Applied);
    const Pattern* drums = history.read().patterns.find(f.drum_pattern);
    REQUIRE(drums->parts.size() == 1);
    CHECK(drums->parts[0].id == added->id);
    CHECK(history.read().instruments == with_two_lanes.instruments);
    CHECK(history.state().undo_label == "Remove 'Drum' from 'Drums A'");

    CHECK(remove_part(history.read(), f.drum_pattern, first).error ==
          "That Instrument is not in 'Drums A'");

    history.undo();
    CHECK(history.read() == with_two_lanes);
}
