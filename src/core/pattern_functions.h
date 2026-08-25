// Pattern mutations from the frozen catalogue in docs/specs/function-surface.md
// sections 5.2, 5.3 and 5.5, on the frozen data-model terms. Same discipline
// as arrangement_functions.h: each function validates against the current
// Project and produces exactly one Delta; ProjectHistory applies it.
//
// Naming: the catalogue's `set_channel_steps` and `set_channel_instrument`
// appear here as `set_part_steps` and `set_part_instrument`, because the data
// model's lane is a Part and "Channel" is not yet ratified vocabulary
// (function-surface spec, open item 1). `set_part_instrument` is core
// document section 3.8's worked example: it points one lane at another
// Instrument and touches no Event.
#pragma once

#include <string>
#include <vector>

#include "core/arrangement_functions.h"

namespace core::functions {

// Creates one empty Pattern of `length_bars` bars at the Project's time
// signature. Does not place it, assign it an Instrument, or write any Event.
Expected<CreatedDelta> create_pattern(const Project& project, std::string name, int length_bars,
                                      Origin origin = Origin::User);

// Writes the on/off sixteenth-step grid of one Part in one Pattern: `steps`
// lists the step indices that are on; every other step is off. A step that
// stays on keeps its Event, so velocities survive; a step that turns on gets
// a new Event at default velocity. No other Part and no other Pattern.
Expected<Delta> set_part_steps(const Project& project, Id pattern, Id part,
                               std::vector<int> steps, Origin origin = Origin::User);

// Points one Part at a different Instrument. No Event anywhere is touched.
Expected<Delta> set_part_instrument(const Project& project, Id pattern, Id part, Id instrument,
                                    Origin origin = Origin::User);

}  // namespace core::functions
