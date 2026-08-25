// Pattern mutations from the frozen catalogue in docs/specs/function-surface.md
// sections 5.2, 5.3 and 5.5, on the frozen data-model terms. Same discipline
// as arrangement_functions.h: each function validates against the current
// Project and produces exactly one Delta; ProjectHistory applies it.
//
// Naming: the catalogue's `set_channel_steps`, `set_channel_step_velocities`,
// `clear_channel_steps` and `set_channel_muted` appear here on Part, because
// the data model's lane is a Part and "Channel" is not yet ratified
// vocabulary (function-surface spec, open item 1).
//
// A step is an Event whose start lies exactly on the sixteenth grid
// (project-data-model spec section 3.4: the grid is a property of the
// surface, not the data). Events off that grid are not steps: the step
// functions leave them exactly where they are, so a melody lane opened in
// the step sequencer cannot lose notes it never showed.
#pragma once

#include <string>
#include <vector>

#include "core/arrangement_functions.h"

namespace core::functions {

// The grid the step functions address: sixteenths.
inline constexpr Ticks kStepTicks = kPpq / 4;

// Creates one empty Pattern of `length_bars` bars at the Project's time
// signature. Does not place it, assign it an Instrument, or write any Event.
Expected<CreatedDelta> create_pattern(const Project& project, std::string name, int length_bars,
                                      Origin origin = Origin::User);

// Changes one Pattern's display name. No Event, no identity, no Placement.
Expected<Delta> rename_pattern(const Project& project, Id pattern, std::string name,
                               Origin origin = Origin::User);

// Writes the on/off sixteenth-step grid of one Part in one Pattern: `steps`
// lists the step indices that are on; every other step is off. A step that
// stays on keeps its Event, so velocities survive; a step that turns on gets
// a new Event at default velocity. No other Part, no other Pattern, and no
// Event that is not on the grid.
Expected<Delta> set_part_steps(const Project& project, Id pattern, Id part,
                               std::vector<int> steps, Origin origin = Origin::User);

struct StepVelocity {
    int step;
    Velocity velocity;  // 1..127
};

// Sets the velocity of steps that are already on in one Part of one Pattern.
// Does not turn any step on or off: naming a step that is off is an error.
Expected<Delta> set_part_step_velocities(const Project& project, Id pattern, Id part,
                                         std::vector<StepVelocity> velocities,
                                         Origin origin = Origin::User);

// Turns every step of one Part in one Pattern off. Does not remove the Part
// and does not touch Events off the grid.
Expected<Delta> clear_part_steps(const Project& project, Id pattern, Id part,
                                 Origin origin = Origin::User);

// Mutes or unmutes one Part. No gain, no Event; an unmute restores what was
// there. Independent of any Track-level mute.
Expected<Delta> set_part_muted(const Project& project, Id pattern, Id part, bool muted,
                               Origin origin = Origin::User);

}  // namespace core::functions
