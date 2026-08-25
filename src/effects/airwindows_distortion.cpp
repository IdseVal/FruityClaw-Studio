// Distortion as the stock Distortion (ADR-001).
#include "airwindows/Distortion/Distortion.h"
#include "effects/airwindows_effect.h"

namespace effects {

std::unique_ptr<engine::Processor> make_distortion() {
    return std::make_unique<AirwindowsEffect<Distortion>>(core::EffectType::Distortion);
}

}  // namespace effects
