// The Arrangement mutations, one single-purpose function per user-nameable
// effect, matching the frozen catalogue in docs/specs/function-surface.md
// section 5.7. Each function validates its targets against the current
// Project and produces exactly one Delta — it computes a change, it never
// performs one. The Delta is applied by ProjectHistory, the only writer, so
// every mutation here is undoable by construction.
//
// Naming: the catalogue's `place_clip` group appears here on the frozen
// data-model term Placement (`add_placement`, ...) because "Clip" is not yet
// ratified vocabulary (function-surface spec, open item 1). One concept,
// one pending name — flagged, not decided here.
//
// These functions are the local implementations the Function registry
// (issue #6 machinery, built by #16) will bind; the UI calls them directly.
// Both callers go through the same door, which is what makes "every mutation
// is available to the Assistant" and "every mutation is undoable" the same
// guarantee.
#pragma once

#include <optional>
#include <string>

#include "core/ops.h"

namespace core::functions {

// A failed function returns a message and no Delta; nothing was mutated.
template <typename T>
struct Expected {
    std::optional<T> value;
    std::string error;

    bool ok() const { return value.has_value(); }
    const T& operator*() const { return *value; }
    T& operator*() { return *value; }
    const T* operator->() const { return &*value; }
    T* operator->() { return &*value; }

    static Expected failure(std::string message) { return {std::nullopt, std::move(message)}; }
    static Expected success(T v) { return {std::move(v), {}}; }
};

// A creation returns the new entity's Id alongside the Delta, so a caller
// (or a later turn's LastCreated Selector) can address what it made.
struct CreatedDelta {
    Delta delta;
    Id id;
};

// Adds one Track lane at the bottom of the Arrangement. Places no Placement.
Expected<CreatedDelta> create_track(const Project& project, Id arrangement,
                                    std::string name, std::optional<Colour> colour,
                                    Origin origin = Origin::User);

// Removes one Track and the Placements on it. Does not delete the Patterns
// those Placements referenced; undo restores the Track with its Placements.
Expected<Delta> delete_track(const Project& project, Id arrangement, Id track,
                             Origin origin = Origin::User);

// Changes one Track's display name. Nothing else.
Expected<Delta> rename_track(const Project& project, Id arrangement, Id track,
                             std::string name, Origin origin = Origin::User);

// Mutes or unmutes one Track. Independent of any Part-level mute.
Expected<Delta> set_track_muted(const Project& project, Id arrangement, Id track,
                                bool muted, Origin origin = Origin::User);

// Places one Placement referencing one Pattern on one Track at one position.
// Does not modify the Pattern; a Placement is a reference, not a copy.
// length == 0 means "the Pattern's own length".
Expected<CreatedDelta> add_placement(const Project& project, Id arrangement, Id track,
                                     Id pattern, Ticks start, Ticks length = 0,
                                     Origin origin = Origin::User);

// Moves one Placement in time and/or to another Track. Does not resize it or
// alter the referenced Pattern. to_track == the current Track is valid.
Expected<Delta> move_placement(const Project& project, Id arrangement, Id track,
                               Id placement, Ticks new_start, Id to_track,
                               Origin origin = Origin::User);

// Changes one Placement's length. Longer than the Pattern loops it; shorter
// trims it. Does not change the referenced Pattern's length.
Expected<Delta> resize_placement(const Project& project, Id arrangement, Id track,
                                 Id placement, Ticks new_length,
                                 Origin origin = Origin::User);

// Removes one Placement from its Track. Does not delete the referenced Pattern.
Expected<Delta> remove_placement(const Project& project, Id arrangement, Id track,
                                 Id placement, Origin origin = Origin::User);

}  // namespace core::functions
