// The Sample-to-Pattern catalogue: one function, one Delta, exact inverses.
#include <catch2/catch_test_macros.hpp>

#include "core/history.h"
#include "core/sample_functions.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

TEST_CASE("create_instrument appends one Sampler over the Sample and undo removes it") {
    auto f = make_fixture();
    MusicalContent original = f.project.musical;
    Id sample = original.samples.items[0].id;
    ProjectHistory history(std::move(f.project));

    auto created = create_instrument(history.read().musical, "Hit", sample, SamplerMode::OneShot);
    REQUIRE(created.ok());
    REQUIRE(created->delta.ops.size() == 1);
    REQUIRE(history.apply(std::move(created->delta)) == ApplyResult::Applied);

    const auto& instruments = history.read().musical.instruments.items;
    REQUIRE(instruments.size() == 3);
    CHECK(instruments.back().id == created->id);
    CHECK(instruments.back().name == "Hit");
    CHECK(instruments.back().params.sample == sample);
    CHECK(instruments.back().params.mode == SamplerMode::OneShot);
    CHECK(instruments.back().chain.empty());
    // Nothing else moved: no Pattern gained a lane.
    CHECK(history.read().musical.patterns == original.patterns);
    CHECK(history.state().undo_label == "Add Instrument 'Hit'");

    REQUIRE(history.undo());
    CHECK(history.read().musical == original);
}

TEST_CASE("create_instrument refuses a Sample that is not in the Project") {
    auto f = make_fixture();
    auto created = create_instrument(f.project.musical, "Ghost", new_id(), SamplerMode::OneShot);
    CHECK_FALSE(created.ok());
    CHECK(created.error == "No such Sample");
}

TEST_CASE("add_part appends one empty lane and undo restores the Pattern exactly") {
    auto f = make_fixture();
    MusicalContent original = f.project.musical;
    Id keys = original.instruments.items[1].id;  // not yet in the drum Pattern
    ProjectHistory history(std::move(f.project));

    auto added = add_part(history.read().musical, f.drum_pattern, keys);
    REQUIRE(added.ok());
    REQUIRE(added->delta.ops.size() == 1);
    REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);

    const Pattern* drums = history.read().musical.patterns.find(f.drum_pattern);
    REQUIRE(drums->parts.size() == 2);
    CHECK(drums->parts.back().id == added->id);
    CHECK(drums->parts.back().instrument == keys);
    CHECK(drums->parts.back().events.empty());
    CHECK_FALSE(drums->parts.back().muted);
    CHECK(drums->parts.front() == original.patterns.find(f.drum_pattern)->parts.front());
    CHECK(history.state().undo_label == "Add 'Keys' to 'Drums A'");

    REQUIRE(history.undo());
    CHECK(history.read().musical == original);
    REQUIRE(history.redo());
    CHECK(history.read().musical.patterns.find(f.drum_pattern)->parts.size() == 2);
}

TEST_CASE("add_part refuses a duplicate lane and missing targets") {
    auto f = make_fixture();
    Id drum = f.project.musical.instruments.items[0].id;  // already the drum Pattern's lane

    auto duplicate = add_part(f.project.musical, f.drum_pattern, drum);
    CHECK_FALSE(duplicate.ok());
    CHECK(duplicate.error == "'Drum' is already in 'Drums A'");

    CHECK(add_part(f.project.musical, new_id(), drum).error == "No such Pattern");
    CHECK(add_part(f.project.musical, f.drum_pattern, new_id()).error == "No such Instrument");
}
