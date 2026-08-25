// Parametric as the stock EQ (ADR-001).
#include "airwindows/Parametric/Parametric.h"
#include "effects/airwindows_effect.h"

namespace effects {

std::unique_ptr<engine::Processor> make_eq() {
    return std::make_unique<AirwindowsEffect<Parametric>>(core::EffectType::Eq);
}

}  // namespace effects
