// History contract checks (docs/specs/history-contract.md section 12).
#include <catch2/catch_test_macros.hpp>

#include "core/arrangement_functions.h"
#include "core/history.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

namespace {

Delta rename_delta(const ProjectHistory& history, const test_support::Fixture& f,
                   const std::string& name) {
    auto result = rename_track(history.read(), f.arrangement, f.track_a, name);
    REQUIRE(result.ok());
    return std::move(*result);
}

}  // namespace

TEST_CASE("apply then undo restores the exact prior Project") {
    auto f = make_fixture();
    Project original = f.project;
    ProjectHistory history(std::move(f.project));

    REQUIRE(history.apply(rename_delta(history, f, "Renamed")) == ApplyResult::Applied);
    REQUIRE_FALSE(history.read() == original);

    REQUIRE(history.undo());
    CHECK(history.read() == original);
}

TEST_CASE("undo then redo restores the applied state bit for bit") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    REQUIRE(history.apply(rename_delta(history, f, "Renamed")) == ApplyResult::Applied);
    Project applied = history.read();

    REQUIRE(history.undo());
    REQUIRE(history.redo());
    CHECK(history.read() == applied);
}

TEST_CASE("applying after an undo discards the future permanently") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    REQUIRE(history.apply(rename_delta(history, f, "One")) == ApplyResult::Applied);
    REQUIRE(history.apply(rename_delta(history, f, "Two")) == ApplyResult::Applied);
    REQUIRE(history.undo());
    REQUIRE(history.state().can_redo);

    REQUIRE(history.apply(rename_delta(history, f, "Three")) == ApplyResult::Applied);
    CHECK_FALSE(history.state().can_redo);
    REQUIRE_FALSE(history.redo());
}

TEST_CASE("a no-op Delta is not recorded and preserves the redo stack") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    REQUIRE(history.apply(rename_delta(history, f, "One")) == ApplyResult::Applied);
    REQUIRE(history.undo());
    REQUIRE(history.state().can_redo);

    auto same = set_track_muted(history.read(), f.arrangement, f.track_a, false);
    REQUIRE(same.ok());
    CHECK(history.apply(std::move(*same)) == ApplyResult::NoChange);
    CHECK(history.state().can_redo);
}

TEST_CASE("a failed Delta leaves the Project, the History and the future intact") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    // Build a Delta against the current state, invalidate its target, undo to
    // restore the redo stack, then apply the stale Delta: every Op throws.
    Delta stale = rename_delta(history, f, "Stale");
    auto del = delete_track(history.read(), f.arrangement, f.track_a);
    REQUIRE(del.ok());
    REQUIRE(history.apply(std::move(*del)) == ApplyResult::Applied);
    Project before = history.read();
    auto state_before = history.state();

    CHECK(history.apply(std::move(stale)) == ApplyResult::Failed);
    CHECK(history.read() == before);
    CHECK(history.state().can_undo == state_before.can_undo);
    CHECK(history.state().can_redo == state_before.can_redo);
}

TEST_CASE("a compound Delta is atomic: a failing member rolls the rest back") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    // First op succeeds (rename), second op targets a missing Track.
    Delta compound = rename_delta(history, f, "Half");
    auto broken = rename_track(history.read(), f.arrangement, f.track_b, "Other");
    REQUIRE(broken.ok());
    compound.ops.push_back(std::move(broken->ops[0]));
    auto del = delete_track(history.read(), f.arrangement, f.track_b);
    REQUIRE(del.ok());
    REQUIRE(history.apply(std::move(*del)) == ApplyResult::Applied);
    Project before = history.read();

    CHECK(history.apply(std::move(compound)) == ApplyResult::Failed);
    CHECK(history.read() == before);
}

TEST_CASE("a gesture collapses to one undo press") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    Project original = history.read();

    history.begin_gesture("Rename Track");
    REQUIRE(history.apply(rename_delta(history, f, "A")) == ApplyResult::Applied);
    REQUIRE(history.apply(rename_delta(history, f, "AB")) == ApplyResult::Applied);
    REQUIRE(history.apply(rename_delta(history, f, "ABC")) == ApplyResult::Applied);
    history.end_gesture();

    REQUIRE(history.undo());
    CHECK(history.read() == original);
    CHECK_FALSE(history.state().can_undo);

    REQUIRE(history.redo());
    const Arrangement* a = history.read().arrangements.find(f.arrangement);
    REQUIRE(a);
    CHECK(a->tracks[0].name == "ABC");
}

TEST_CASE("a gesture with no net change is not recorded") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    history.begin_gesture("Wiggle");
    REQUIRE(history.apply(rename_delta(history, f, "Moved")) == ApplyResult::Applied);
    REQUIRE(history.apply(rename_delta(history, f, "Track 1")) == ApplyResult::Applied);
    history.end_gesture();

    CHECK_FALSE(history.state().can_undo);
}

TEST_CASE("eviction drops the oldest entry only and never the redo stack") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    history.set_limits(2, 1 << 30);

    REQUIRE(history.apply(rename_delta(history, f, "One")) == ApplyResult::Applied);
    REQUIRE(history.apply(rename_delta(history, f, "Two")) == ApplyResult::Applied);
    REQUIRE(history.apply(rename_delta(history, f, "Three")) == ApplyResult::Applied);

    REQUIRE(history.undo());
    REQUIRE(history.undo());
    CHECK_FALSE(history.state().can_undo);  // the first entry was evicted

    const Arrangement* a = history.read().arrangements.find(f.arrangement);
    REQUIRE(a);
    CHECK(a->tracks[0].name == "One");
    CHECK(history.state().can_redo);
}

TEST_CASE("the dirty flag follows the cursor across save, undo and redo") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    CHECK_FALSE(history.state().is_dirty);

    REQUIRE(history.apply(rename_delta(history, f, "One")) == ApplyResult::Applied);
    CHECK(history.state().is_dirty);

    history.mark_saved();
    CHECK_FALSE(history.state().is_dirty);

    REQUIRE(history.undo());
    CHECK(history.state().is_dirty);
    REQUIRE(history.redo());
    CHECK_FALSE(history.state().is_dirty);
}

TEST_CASE("undo labels describe the effect in domain vocabulary") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    auto placed = add_placement(history.read(), f.arrangement, f.track_a,
                                f.drum_pattern, 0);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(std::move(placed->delta)) == ApplyResult::Applied);

    auto label = history.state().undo_label;
    REQUIRE(label.has_value());
    // Content, not Function identity (history contract section 7.3).
    CHECK(label->find("add_placement") == std::string::npos);
    CHECK(label->find("Drums A") != std::string::npos);
}
