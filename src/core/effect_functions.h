// The Effect-chain mutations, matching the frozen catalogue in
// docs/specs/function-surface.md section 5.6 on the frozen data-model terms.
// Same discipline as arrangement_functions.h: each function validates against
// the current Project and produces exactly one Delta; ProjectHistory applies
// it, so every mutation here is undoable by construction.
//
// An Effect is addressed by its Id alone: Ids are globally unique, so the
// chain holding it — an Instrument's or the Project's master chain — is
// found, never named. Only add_effect needs to be told which chain.
#pragma once

#include <optional>
#include <string>

#include "core/arrangement_functions.h"

namespace core::functions {

// The chain an Effect is added to: one Instrument's, or the master chain
// when `instrument` is absent. The two holders the data model defines
// (project-data-model section 3.10).
struct ChainRef {
    std::optional<Id> instrument;

    static ChainRef master() { return {}; }
    static ChainRef of(Id instrument) { return {instrument}; }
};

// Appends one of the six stock Effects, at its schema defaults, to one chain.
Expected<CreatedDelta> add_effect(const Project& project, ChainRef chain, EffectType type,
                                  Origin origin = Origin::User);

// Removes one Effect from its chain; the survivors close the gap. Undo
// restores it at its old position with its parameters.
Expected<Delta> remove_effect(const Project& project, Id effect, Origin origin = Origin::User);

// Moves one Effect to `new_index` within its chain. No parameter values, no
// other chain.
Expected<Delta> move_effect(const Project& project, Id effect, std::size_t new_index,
                            Origin origin = Origin::User);

// Sets one named parameter on one Effect. The name must be in the type's
// schema (effect_schema.h); the value is clamped to the schema range.
Expected<Delta> set_effect_parameter(const Project& project, Id effect, std::string name,
                                     float value, Origin origin = Origin::User);

// Bypasses or re-enables one Effect. Does not remove it and does not touch
// its parameters.
Expected<Delta> set_effect_bypassed(const Project& project, Id effect, bool bypassed,
                                    Origin origin = Origin::User);

}  // namespace core::functions
