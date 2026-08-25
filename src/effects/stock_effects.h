// The six stock Effects of ADR-001 as Processor adapters (architecture-seams
// section 3): five Airwindows picks and x42's peaklim. This is the only
// module that includes their sources, and the composition root hands
// make_effect to the engine as its ProcessorFactory.
#pragma once

#include <memory>

#include "core/entities.h"
#include "engine/processor.h"

namespace effects {

// A fresh, unprepared Processor for one stock Effect type.
std::unique_ptr<engine::Processor> make_effect(core::EffectType type);

}  // namespace effects
