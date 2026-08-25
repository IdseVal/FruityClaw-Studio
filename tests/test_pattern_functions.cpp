// The Pattern and Instrument Functions added for the Assistant: each does
// its one thing, touches nothing else, and undoes exactly.
#include <catch2/catch_test_macros.hpp>

#include "core/history.h"
#include "core/pattern_functions.h"
#include "core/sample_functions.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

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
    CHECK(history.read().patterns.items.size() == 3);
    CHECK(history.read().arrangements.items.front().tracks[0].placements.empty());

    history.undo();
    CHECK(history.read().patterns.items.size() == 2);
    CHECK(history.read() == f.project);
}

TEST_CASE("create_pattern rejects an out-of-range length") {
    auto f = make_fixture();
    CHECK_FALSE(create_pattern(f.project, "x", 0).ok());
    CHECK_FALSE(create_pattern(f.project, "x", 65).ok());
}

TEST_CASE("set_part_steps rewrites one lane, keeps velocities of surviving steps") {
    auto f = make_fixture();
    ProjectHistory history(f.project);
    const Pattern* drums = history.read().patterns.find(f.drum_pattern);
    Id part = drums->parts[0].id;
    // The fixture lane has hits on beats 0..3: steps 0, 4, 8, 12 at velocity 100.
    Id first_hit = drums->parts[0].events[0].id;

    auto result = set_part_steps(history.read(), f.drum_pattern, part, {0, 2, 15});
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);

    const Part& lane = history.read().patterns.find(f.drum_pattern)->parts[0];
    REQUIRE(lane.events.size() == 3);
    CHECK(lane.events[0].id == first_hit);  // step 0 survived with its Event
    CHECK(lane.events[1].start == 2 * (kPpq / 4));
    CHECK(lane.events[2].start == 15 * (kPpq / 4));
    CHECK(lane.instrument == f.project.patterns.find(f.drum_pattern)->parts[0].instrument);
    // The melody Pattern is untouched.
    CHECK(*history.read().patterns.find(f.melody_pattern) ==
          *f.project.patterns.find(f.melody_pattern));

    history.undo();
    CHECK(history.read() == f.project);
}

TEST_CASE("set_part_steps refuses a step beyond the Pattern") {
    auto f = make_fixture();
    Id part = f.project.patterns.find(f.drum_pattern)->parts[0].id;
    CHECK_FALSE(set_part_steps(f.project, f.drum_pattern, part, {16}).ok());
    CHECK_FALSE(set_part_steps(f.project, f.drum_pattern, part, {-1}).ok());
    CHECK_FALSE(set_part_steps(f.project, f.drum_pattern, new_id(), {0}).ok());
}

TEST_CASE("set_part_instrument switches the lane and touches no Event") {
    auto f = make_fixture();
    ProjectHistory history(f.project);
    Id drum = f.project.instruments.items[0].id;
    Id keys = f.project.instruments.items[1].id;
    Id part = f.project.patterns.find(f.melody_pattern)->parts[0].id;

    auto result = set_part_instrument(history.read(), f.melody_pattern, part, drum);
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);
    const Part& lane = history.read().patterns.find(f.melody_pattern)->parts[0];
    CHECK(lane.instrument == drum);
    CHECK(lane.events == f.project.patterns.find(f.melody_pattern)->parts[0].events);

    // Already there: a no-op is not recorded.
    auto same = set_part_instrument(history.read(), f.melody_pattern, part, drum);
    CHECK(history.apply(*same) == ApplyResult::NoChange);

    CHECK_FALSE(set_part_instrument(history.read(), f.melody_pattern, part, new_id()).ok());
    history.undo();
    CHECK(history.read().patterns.find(f.melody_pattern)->parts[0].instrument == keys);
}

TEST_CASE("set_instrument_sample repoints one Instrument, nothing else") {
    auto f = make_fixture();
    ProjectHistory history(f.project);
    Id keys = f.project.instruments.items[1].id;
    Id hit = f.project.samples.items[0].id;

    auto result = set_instrument_sample(history.read(), keys, hit);
    REQUIRE(result.ok());
    REQUIRE(history.apply(*result) == ApplyResult::Applied);
    CHECK(history.read().instruments.find(keys)->params.sample == hit);
    CHECK(history.read().instruments.find(keys)->params.mode == SamplerMode::Sustain);
    CHECK(history.read().patterns == f.project.patterns);

    CHECK_FALSE(set_instrument_sample(history.read(), keys, new_id()).ok());
    history.undo();
    CHECK(history.read() == f.project);
}
