// The Arrangement mutation catalogue: one function, one Delta, exact inverses.
#include <catch2/catch_test_macros.hpp>

#include "core/arrangement_functions.h"
#include "core/history.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

namespace {

const Track& track(const Project& p, Id arrangement, Id id) {
    const Arrangement* a = p.arrangements.find(arrangement);
    REQUIRE(a);
    for (const Track& t : a->tracks)
        if (t.id == id) return t;
    FAIL("track not found");
    static Track unreachable;
    return unreachable;
}

}  // namespace

TEST_CASE("create_track appends one empty Track and undo removes it") {
    auto f = make_fixture();
    Project original = f.project;
    ProjectHistory history(std::move(f.project));

    auto created = create_track(history.read(), f.arrangement, "Bass", std::nullopt);
    REQUIRE(created.ok());
    REQUIRE(created->delta.ops.size() == 1);
    REQUIRE(history.apply(std::move(created->delta)) == ApplyResult::Applied);

    const Arrangement* a = history.read().arrangements.find(f.arrangement);
    REQUIRE(a->tracks.size() == 3);
    CHECK(a->tracks.back().id == created->id);
    CHECK(a->tracks.back().name == "Bass");
    CHECK(a->tracks.back().placements.empty());

    REQUIRE(history.undo());
    CHECK(history.read() == original);
}

TEST_CASE("delete_track takes its Placements with it and undo restores both") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    Project with_placement = history.read();

    auto del = delete_track(history.read(), f.arrangement, f.track_a);
    REQUIRE(del.ok());
    REQUIRE(del->ops.size() == 1);  // one Delta, one compound removal
    REQUIRE(history.apply(std::move(*del)) == ApplyResult::Applied);

    const Arrangement* a = history.read().arrangements.find(f.arrangement);
    CHECK(a->tracks.size() == 1);
    // The referenced Pattern is not deleted.
    CHECK(history.read().patterns.find(f.drum_pattern) != nullptr);

    REQUIRE(history.undo());
    CHECK(history.read() == with_placement);
}

TEST_CASE("deleting a middle Track restores it at its position") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto created = create_track(history.read(), f.arrangement, "Third", std::nullopt);
    REQUIRE(created.ok());
    REQUIRE(history.apply(std::move(created->delta)) == ApplyResult::Applied);
    Project before = history.read();

    auto del = delete_track(history.read(), f.arrangement, f.track_b);  // middle
    REQUIRE(del.ok());
    REQUIRE(history.apply(std::move(*del)) == ApplyResult::Applied);
    REQUIRE(history.undo());
    CHECK(history.read() == before);  // order preserved bit for bit
}

TEST_CASE("add_placement defaults its length to the Pattern's length") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.melody_pattern,
                                8 * kPpq);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);

    const Track& t = track(history.read(), f.arrangement, f.track_a);
    REQUIRE(t.placements.size() == 1);
    CHECK(t.placements[0].start == 8 * kPpq);
    CHECK(t.placements[0].length == 4 * kPpq);
    CHECK(t.placements[0].pattern == f.melody_pattern);
}

TEST_CASE("removing a middle Placement restores it at its position on undo") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    Id middle;
    for (int i = 0; i < 3; ++i) {
        auto placed = add_placement(history.read(), f.arrangement, f.track_a,
                                    f.drum_pattern, i * 4 * kPpq);
        REQUIRE(placed.ok());
        if (i == 1) middle = placed->id;
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    }
    Project before = history.read();

    auto removed = remove_placement(history.read(), f.arrangement, f.track_a, middle);
    REQUIRE(removed.ok());
    REQUIRE(history.apply(std::move(*removed)) == ApplyResult::Applied);
    CHECK(track(history.read(), f.arrangement, f.track_a).placements.size() == 2);

    REQUIRE(history.undo());
    CHECK(history.read() == before);
}

TEST_CASE("move_placement within a Track changes start and nothing else") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    Id id = placed->id;
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);

    auto moved = move_placement(history.read(), f.arrangement, f.track_a, id,
                                4 * kPpq, f.track_a);
    REQUIRE(moved.ok());
    REQUIRE(moved->ops.size() == 1);
    REQUIRE(history.apply(std::move(*moved)) == ApplyResult::Applied);

    const Track& t = track(history.read(), f.arrangement, f.track_a);
    CHECK(t.placements[0].start == 4 * kPpq);
    CHECK(t.placements[0].length == 4 * kPpq);
    CHECK(t.placements[0].id == id);
}

TEST_CASE("move_placement across Tracks is one atomic Delta") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    Id id = placed->id;
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    Project before = history.read();

    auto moved = move_placement(history.read(), f.arrangement, f.track_a, id,
                                2 * kPpq, f.track_b);
    REQUIRE(moved.ok());
    REQUIRE(history.apply(std::move(*moved)) == ApplyResult::Applied);

    CHECK(track(history.read(), f.arrangement, f.track_a).placements.empty());
    const Track& to = track(history.read(), f.arrangement, f.track_b);
    REQUIRE(to.placements.size() == 1);
    CHECK(to.placements[0].id == id);
    CHECK(to.placements[0].start == 2 * kPpq);

    // One undo press restores both halves of the move.
    REQUIRE(history.undo());
    CHECK(history.read() == before);
}

TEST_CASE("resize_placement changes length only; the Pattern is untouched") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto placed = add_placement(history.read(), f.arrangement, f.track_a, f.drum_pattern, 0);
    REQUIRE(placed.ok());
    Id id = placed->id;
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);

    auto resized = resize_placement(history.read(), f.arrangement, f.track_a, id, 8 * kPpq);
    REQUIRE(resized.ok());
    REQUIRE(history.apply(std::move(*resized)) == ApplyResult::Applied);

    CHECK(track(history.read(), f.arrangement, f.track_a).placements[0].length == 8 * kPpq);
    CHECK(history.read().patterns.find(f.drum_pattern)->length == 4 * kPpq);
}

TEST_CASE("validation failures return an error and touch nothing") {
    auto f = make_fixture();
    Project original = f.project;

    CHECK_FALSE(add_placement(f.project, f.arrangement, new_id(), f.drum_pattern, 0).ok());
    CHECK_FALSE(add_placement(f.project, f.arrangement, f.track_a, new_id(), 0).ok());
    CHECK_FALSE(add_placement(f.project, f.arrangement, f.track_a, f.drum_pattern, -1).ok());
    CHECK_FALSE(rename_track(f.project, f.arrangement, new_id(), "X").ok());
    CHECK_FALSE(resize_placement(f.project, f.arrangement, f.track_a, new_id(), kPpq).ok());
    CHECK(f.project == original);
}

TEST_CASE("both Pattern kinds place through the same function without special cases") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    for (Id pattern : {f.drum_pattern, f.melody_pattern}) {
        auto placed = add_placement(history.read(), f.arrangement, f.track_a, pattern,
                                    0);
        REQUIRE(placed.ok());
        REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);
    }
    CHECK(track(history.read(), f.arrangement, f.track_a).placements.size() == 2);
}
