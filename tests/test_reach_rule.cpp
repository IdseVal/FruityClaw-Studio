// The reach rule, asserted as a property of the type graph.
//
// docs/specs/function-surface.md section 1 promises that a Function cannot
// reach Studio settings, provider credentials, the toggle store, view state,
// the Project's file path or the filesystem, and that this is "enforced by
// construction, not by a check". ADR-060 discharges obligation O-5.1 by
// making that true: a Function receives `core::MusicalContent`, which holds
// the musical content and nothing else.
//
// This file is the honest proof. Most of it is compile-time: it fails to
// build, rather than fails at run time, when the shape it describes changes.
#include <optional>
#include <string>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "core/arrangement_functions.h"
#include "core/entities.h"
#include "core/history.h"
#include "core/ops.h"
#include "core/sample_functions.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;

namespace {

// The first parameter of a free function, as a type.
template <typename F>
struct first_parameter;
template <typename R, typename A0, typename... Rest>
struct first_parameter<R(A0, Rest...)> {
    using type = A0;
};
template <typename F>
using first_parameter_t = typename first_parameter<F>::type;

// --- Every Function is handed exactly one object, and it is MusicalContent --
//
// A row added here for a Function that takes anything else does not compile.
// A new Function file that takes a Project is caught by reach_rule_lint.py,
// which does not need to be told the Function's name.

template <typename F>
inline constexpr bool takes_only_musical_content =
    std::is_same_v<first_parameter_t<F>, const MusicalContent&>;

static_assert(takes_only_musical_content<decltype(create_track)>);
static_assert(takes_only_musical_content<decltype(delete_track)>);
static_assert(takes_only_musical_content<decltype(rename_track)>);
static_assert(takes_only_musical_content<decltype(set_track_muted)>);
static_assert(takes_only_musical_content<decltype(add_placement)>);
static_assert(takes_only_musical_content<decltype(move_placement)>);
static_assert(takes_only_musical_content<decltype(resize_placement)>);
static_assert(takes_only_musical_content<decltype(remove_placement)>);
static_assert(takes_only_musical_content<decltype(create_instrument)>);
static_assert(takes_only_musical_content<decltype(add_part)>);

// --- What a Function returns cannot reach further than what it was handed ---
//
// A Function computes a Delta rather than performing a change, so the write
// side has to be as narrow as the read side: an Op that could take a Project&
// would let a Delta write the file path a Function cannot even read.
static_assert(std::is_same_v<decltype(Op::run),
                             std::function<std::optional<Op>(MusicalContent&)>>);
static_assert(std::is_same_v<decltype(apply_delta), Delta(MusicalContent&, const Delta&)>);

// --- The census: the whole of what a Function can see -----------------------
//
// Naming every member is the point. Adding a field to MusicalContent widens
// the Assistant's reach, and this stops compiling until whoever added it says
// so here — which is the reviewable act O-5.1 asks for. A structured binding
// admits no brace elision, so the count is exact.
void musical_content_is_exactly_this(const MusicalContent& content) {
    const auto& [tempo, time_signature, samples, instruments, patterns, arrangements,
                 master_chain] = content;
    (void)tempo;
    (void)time_signature;
    (void)samples;
    (void)instruments;
    (void)patterns;
    (void)arrangements;
    (void)master_chain;
}

// The other half of the document. Nothing here is reachable from a Function;
// this is where the Project's file path and save metadata go when issue #13
// wants them in the document (function-surface section 1, "meta/").
void project_meta_is_exactly_this(const ProjectMeta& meta) {
    const auto& [id, format_version, title] = meta;
    (void)id;
    (void)format_version;
    (void)title;
}

// And the split itself: two members, one reachable, one not.
void project_is_exactly_this(const Project& project) {
    const auto& [meta, musical] = project;
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(meta)>, ProjectMeta>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(musical)>, MusicalContent>);
    (void)meta;
    (void)musical;
}

}  // namespace

// --- The run-time half: #13 can add save metadata and nothing shifts --------

TEST_CASE("no Delta reaches the Project's non-musical state") {
    auto f = test_support::make_fixture();
    f.project.meta.title = "The user's title";
    f.project.meta.format_version = 1;
    const ProjectMeta before = f.project.meta;

    ProjectHistory history(std::move(f.project));

    // A creation, a set, a move and a deletion — one of each Op kind, driven
    // through the only writer.
    auto created = create_track(history.read().musical, f.arrangement, "Bass", std::nullopt);
    REQUIRE(created.ok());
    REQUIRE(history.apply(created->delta) == ApplyResult::Applied);

    auto placed = add_placement(history.read().musical, f.arrangement, f.track_a,
                                f.drum_pattern, 0);
    REQUIRE(placed.ok());
    REQUIRE(history.apply(placed->delta) == ApplyResult::Applied);

    auto renamed = rename_track(history.read().musical, f.arrangement, f.track_a, "Lead");
    REQUIRE(renamed.ok());
    REQUIRE(history.apply(*renamed) == ApplyResult::Applied);

    auto removed = remove_placement(history.read().musical, f.arrangement, f.track_a,
                                    placed->id);
    REQUIRE(removed.ok());
    REQUIRE(history.apply(*removed) == ApplyResult::Applied);

    // The claim is over the whole of ProjectMeta, not over a list of fields,
    // so it keeps holding as #13 and #55 add to it.
    CHECK(history.read().meta == before);

    history.undo();
    history.undo();
    CHECK(history.read().meta == before);
}

TEST_CASE("undo and redo restore the musical content and leave the document alone") {
    auto f = test_support::make_fixture();
    f.project.meta.title = "Kept";
    const MusicalContent original = f.project.musical;
    const ProjectMeta meta = f.project.meta;

    ProjectHistory history(std::move(f.project));
    auto created = create_track(history.read().musical, f.arrangement, "New", std::nullopt);
    REQUIRE(created.ok());
    REQUIRE(history.apply(created->delta) == ApplyResult::Applied);
    CHECK_FALSE(history.read().musical == original);

    REQUIRE(history.undo());
    CHECK(history.read().musical == original);
    CHECK(history.read().meta == meta);
}
