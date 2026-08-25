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
    Project original = f.project;
    Id sample = original.samples.items[0].id;
    ProjectHistory history(std::move(f.project));

    auto created = create_instrument(history.read(), "Hit", sample, SamplerMode::OneShot);
    REQUIRE(created.ok());
    REQUIRE(created->delta.ops.size() == 1);
    REQUIRE(history.apply(std::move(created->delta)) == ApplyResult::Applied);

    const auto& instruments = history.read().instruments.items;
    REQUIRE(instruments.size() == 3);
    CHECK(instruments.back().id == created->id);
    CHECK(instruments.back().name == "Hit");
    CHECK(instruments.back().params.sample == sample);
    CHECK(instruments.back().params.mode == SamplerMode::OneShot);
    CHECK(instruments.back().chain.empty());
    // Nothing else moved: no Pattern gained a lane.
    CHECK(history.read().patterns == original.patterns);
    CHECK(history.state().undo_label == "Add Instrument 'Hit'");

    REQUIRE(history.undo());
    CHECK(history.read() == original);
}

TEST_CASE("create_instrument refuses a Sample that is not in the Project") {
    auto f = make_fixture();
    auto created = create_instrument(f.project, "Ghost", new_id(), SamplerMode::OneShot);
    CHECK_FALSE(created.ok());
    CHECK(created.error == "No such Sample");
}

TEST_CASE("add_part appends one empty lane and undo restores the Pattern exactly") {
    auto f = make_fixture();
    Project original = f.project;
    Id keys = original.instruments.items[1].id;  // not yet in the drum Pattern
    ProjectHistory history(std::move(f.project));

    auto added = add_part(history.read(), f.drum_pattern, keys);
    REQUIRE(added.ok());
    REQUIRE(added->delta.ops.size() == 1);
    REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);

    const Pattern* drums = history.read().patterns.find(f.drum_pattern);
    REQUIRE(drums->parts.size() == 2);
    CHECK(drums->parts.back().id == added->id);
    CHECK(drums->parts.back().instrument == keys);
    CHECK(drums->parts.back().events.empty());
    CHECK_FALSE(drums->parts.back().muted);
    CHECK(drums->parts.front() == original.patterns.find(f.drum_pattern)->parts.front());
    CHECK(history.state().undo_label == "Add 'Keys' to 'Drums A'");

    REQUIRE(history.undo());
    CHECK(history.read() == original);
    REQUIRE(history.redo());
    CHECK(history.read().patterns.find(f.drum_pattern)->parts.size() == 2);
}

TEST_CASE("add_part refuses a duplicate lane and missing targets") {
    auto f = make_fixture();
    Id drum = f.project.instruments.items[0].id;  // already the drum Pattern's lane

    auto duplicate = add_part(f.project, f.drum_pattern, drum);
    CHECK_FALSE(duplicate.ok());
    CHECK(duplicate.error == "'Drum' is already in 'Drums A'");

    CHECK(add_part(f.project, new_id(), drum).error == "No such Pattern");
    CHECK(add_part(f.project, f.drum_pattern, new_id()).error == "No such Instrument");
}

TEST_CASE("add_sample appends one Human Sample over the audio and undo removes it") {
    auto f = make_fixture();
    Project original = f.project;
    ProjectHistory history(std::move(f.project));
    SampleSource take = test_support::make_tone(0.3, 330.0);

    auto added = add_sample(history.read(), "Take 1", take);
    REQUIRE(added.ok());
    REQUIRE(added->delta.ops.size() == 1);
    REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);

    const auto& samples = history.read().samples.items;
    REQUIRE(samples.size() == 3);
    CHECK(samples.back().id == added->id);
    CHECK(samples.back().name == "Take 1");
    CHECK(samples.back().source == take);  // shared, not copied
    // A recording is the user's own work: never marked AI-generated.
    CHECK(samples.back().provenance.is_human());
    // Nothing else moved: no Instrument, no Pattern lane.
    CHECK(history.read().instruments == original.instruments);
    CHECK(history.read().patterns == original.patterns);
    CHECK(history.state().undo_label == "Add Sample 'Take 1'");

    REQUIRE(history.undo());
    CHECK(history.read() == original);
    REQUIRE(history.redo());
    CHECK(history.read().samples.items.size() == 3);
}

TEST_CASE("add_sample refuses empty audio") {
    auto f = make_fixture();
    CHECK(add_sample(f.project, "Empty", nullptr).error == "Nothing was recorded");
    CHECK(add_sample(f.project, "Empty", std::make_shared<AudioData>()).error ==
          "Nothing was recorded");
}
