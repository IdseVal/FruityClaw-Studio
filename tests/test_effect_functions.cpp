// The Effect-chain function catalogue (function-surface section 5.6): each
// function does its one thing, validates against the Project, and is
// undoable through the History.
#include <catch2/catch_test_macros.hpp>

#include "core/effect_functions.h"
#include "core/effect_schema.h"
#include "core/history.h"
#include "test_support.h"

using namespace core;
using namespace core::functions;
using test_support::make_fixture;

namespace {

Id keys_of(const Project& project) { return project.instruments.items[1].id; }

const Effect* find_in(const std::vector<Effect>& chain, Id id) {
    for (const Effect& e : chain)
        if (e.id == id) return &e;
    return nullptr;
}

}  // namespace

TEST_CASE("every type has a schema and add_effect seeds its defaults") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    for (int i = 0; i < kEffectTypeCount; ++i) {
        auto type = static_cast<EffectType>(i);
        CHECK_FALSE(effect_type_name(type).empty());
        REQUIRE_FALSE(effect_parameters(type).empty());

        auto added = add_effect(history.read(), ChainRef::master(), type);
        REQUIRE(added.ok());
        REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);
        const Effect* e = find_in(history.read().master_chain, added->id);
        REQUIRE(e);
        CHECK(e->type == type);
        CHECK(e->enabled);
        for (const EffectParameter& parameter : effect_parameters(type)) {
            REQUIRE(e->params.count(std::string(parameter.name)) == 1);
            CHECK(e->params.at(std::string(parameter.name)) == parameter.default_value);
        }
    }
    CHECK(history.read().master_chain.size() == 6);
}

TEST_CASE("an Effect lands on the named Instrument's chain, and only there") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    Id keys = keys_of(history.read());

    auto added = add_effect(history.read(), ChainRef::of(keys), EffectType::Reverb);
    REQUIRE(added.ok());
    REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);
    CHECK(history.read().instruments.find(keys)->chain.size() == 1);
    CHECK(history.read().instruments.items[0].chain.empty());
    CHECK(history.read().master_chain.empty());

    CHECK_FALSE(add_effect(history.read(), ChainRef::of(new_id()), EffectType::Eq).ok());
}

TEST_CASE("remove closes the gap and undo restores the position and parameters") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));

    Id ids[3];
    EffectType types[] = {EffectType::Eq, EffectType::Delay, EffectType::Limiter};
    for (int i = 0; i < 3; ++i) {
        auto added = add_effect(history.read(), ChainRef::master(), types[i]);
        REQUIRE(added.ok());
        ids[i] = added->id;
        REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);
    }
    auto tweaked = set_effect_parameter(history.read(), ids[1], "Regen", 0.7f);
    REQUIRE(tweaked.ok());
    REQUIRE(history.apply(std::move(*tweaked)) == ApplyResult::Applied);

    auto removed = remove_effect(history.read(), ids[1]);
    REQUIRE(removed.ok());
    REQUIRE(history.apply(std::move(*removed)) == ApplyResult::Applied);
    REQUIRE(history.read().master_chain.size() == 2);
    CHECK(history.read().master_chain[0].id == ids[0]);
    CHECK(history.read().master_chain[1].id == ids[2]);

    REQUIRE(history.undo());
    REQUIRE(history.read().master_chain.size() == 3);
    CHECK(history.read().master_chain[1].id == ids[1]);
    CHECK(history.read().master_chain[1].params.at("Regen") == 0.7f);

    CHECK_FALSE(remove_effect(history.read(), new_id()).ok());
}

TEST_CASE("move reorders within the chain and is its own inverse") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    Id keys = keys_of(history.read());

    Id ids[3];
    for (int i = 0; i < 3; ++i) {
        auto added = add_effect(history.read(), ChainRef::of(keys), EffectType::Distortion);
        REQUIRE(added.ok());
        ids[i] = added->id;
        REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);
    }

    auto moved = move_effect(history.read(), ids[2], 0);
    REQUIRE(moved.ok());
    REQUIRE(history.apply(std::move(*moved)) == ApplyResult::Applied);
    const auto& chain = history.read().instruments.find(keys)->chain;
    CHECK(chain[0].id == ids[2]);
    CHECK(chain[1].id == ids[0]);
    CHECK(chain[2].id == ids[1]);

    REQUIRE(history.undo());
    CHECK(history.read().instruments.find(keys)->chain[2].id == ids[2]);

    // Moving to where it already is changes nothing and records nothing.
    auto same = move_effect(history.read(), ids[2], 2);
    REQUIRE(same.ok());
    CHECK(history.apply(std::move(*same)) == ApplyResult::NoChange);
    CHECK_FALSE(move_effect(history.read(), ids[2], 3).ok());
}

TEST_CASE("set_effect_parameter validates the name, clamps to range, and is undoable") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto added = add_effect(history.read(), ChainRef::master(), EffectType::Limiter);
    REQUIRE(added.ok());
    Id id = added->id;
    REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);

    CHECK_FALSE(set_effect_parameter(history.read(), id, "Ratio", 0.5f).ok());

    auto clamped = set_effect_parameter(history.read(), id, "Threshold", -40.0f);
    REQUIRE(clamped.ok());
    REQUIRE(history.apply(std::move(*clamped)) == ApplyResult::Applied);
    CHECK(history.read().master_chain[0].params.at("Threshold") == -10.0f);

    auto same = set_effect_parameter(history.read(), id, "Threshold", -10.0f);
    REQUIRE(same.ok());
    CHECK(history.apply(std::move(*same)) == ApplyResult::NoChange);

    REQUIRE(history.undo());
    CHECK(history.read().master_chain[0].params.at("Threshold") == -1.0f);
    // Only the named parameter moved.
    CHECK(history.read().master_chain[0].params.at("Release") == 0.01f);
}

TEST_CASE("bypass toggles enabled without touching parameters") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto added = add_effect(history.read(), ChainRef::master(), EffectType::Compressor);
    REQUIRE(added.ok());
    Id id = added->id;
    REQUIRE(history.apply(std::move(added->delta)) == ApplyResult::Applied);
    auto tweaked = set_effect_parameter(history.read(), id, "Compress", 0.4f);
    REQUIRE(tweaked.ok());
    REQUIRE(history.apply(std::move(*tweaked)) == ApplyResult::Applied);

    auto bypassed = set_effect_bypassed(history.read(), id, true);
    REQUIRE(bypassed.ok());
    REQUIRE(history.apply(std::move(*bypassed)) == ApplyResult::Applied);
    CHECK_FALSE(history.read().master_chain[0].enabled);
    CHECK(history.read().master_chain[0].params.at("Compress") == 0.4f);
    CHECK(history.state().undo_label == "Bypass Compressor");

    auto again = set_effect_bypassed(history.read(), id, true);
    REQUIRE(again.ok());
    CHECK(history.apply(std::move(*again)) == ApplyResult::NoChange);

    REQUIRE(history.undo());
    CHECK(history.read().master_chain[0].enabled);
}

TEST_CASE("an Assistant-made Effect Delta carries its Origin") {
    auto f = make_fixture();
    ProjectHistory history(std::move(f.project));
    auto added = add_effect(history.read(), ChainRef::master(), EffectType::Eq,
                            Origin::Assistant);
    REQUIRE(added.ok());
    CHECK(added->delta.origin == Origin::Assistant);
}
