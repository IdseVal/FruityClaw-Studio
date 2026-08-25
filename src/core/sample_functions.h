// The mutations the Sample sidebar needs to put a Sample into a Pattern,
// matching the frozen catalogue in docs/specs/function-surface.md section
// 5.5 on the frozen data-model terms. Same discipline as
// arrangement_functions.h: each function validates against the current
// Project and produces exactly one Delta; ProjectHistory applies it.
//
// Naming: the catalogue's `create_channel` / `set_channel_sample` pair
// appears here as `create_instrument`, because the data model has no
// Channel — its Instrument is the thing a Part's `instrument` field names
// (project-data-model spec section 3.5) and "Channel" is not yet ratified
// vocabulary (function-surface spec, open item 1). Adding a Sample's
// Instrument as a lane of a Pattern is `add_part`: the lane the section 5.3
// step Functions will write into, created empty.
#pragma once

#include <string>

#include "core/arrangement_functions.h"

namespace core::functions {

// Adds one Sample over ready-made audio to the Project's Sample library.
// Provenance is Human: this is the door a recorded take comes through
// (core document 6.3 applies to AI content only). Assigns it to no
// Instrument and writes nothing into any Pattern. Fails on empty audio, so
// the Project never holds a Sample it cannot play.
Expected<CreatedDelta> add_sample(const Project& project, std::string name, SampleSource source,
                                  Origin origin = Origin::User);

// Adds one Sampler Instrument playing one Sample already in the Project.
// Writes nothing into any Pattern.
Expected<CreatedDelta> create_instrument(const Project& project, std::string name, Id sample,
                                         SamplerMode mode, Origin origin = Origin::User);

// Appends one empty Part for one Instrument to one Pattern — a new row in
// the step sequencer with no steps on. Fails when the Pattern already has a
// Part for that Instrument, so a lane is never duplicated by accident.
Expected<CreatedDelta> add_part(const Project& project, Id pattern, Id instrument,
                                Origin origin = Origin::User);

}  // namespace core::functions
