// The parameter schema of each of the six stock Effects.
// Contract: docs/specs/project-data-model.md section 3.10 freezes the
// container (`Effect.params` is a map string -> float) and leaves the
// contents to the Effect specification. This is that specification as data:
// for each EffectType, the parameter names that may appear in `params`, their
// ranges and defaults. ADR-001 fixes the implementations behind them.
//
// The engine-side adapters (src/effects/) declare their ParameterDescriptors
// from this table, so there is one source for the names the user, the
// Assistant's Functions and the Processor all use; the position of a
// parameter here is its permanent ParamId (architecture-seams rule 4).
#pragma once

#include <span>
#include <string_view>

#include "core/entities.h"

namespace core {

struct EffectParameter {
    std::string_view name;
    float min_value;
    float max_value;
    float default_value;
    std::string_view unit;  // "" for a plain 0..1 control
};

inline constexpr int kEffectTypeCount = 6;

// User-facing name of the type: "EQ", "Compressor", ...
std::string_view effect_type_name(EffectType type);

// The parameters of one type, in ParamId order.
std::span<const EffectParameter> effect_parameters(EffectType type);

// The parameter of `type` called `name`, or null.
const EffectParameter* find_effect_parameter(EffectType type, std::string_view name);

}  // namespace core
