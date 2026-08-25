// Verbity2 as the stock Reverb (ADR-001).
#include "airwindows/Verbity2/Verbity2.h"
#include "effects/airwindows_effect.h"

namespace effects {

std::unique_ptr<engine::Processor> make_reverb() {
    return std::make_unique<AirwindowsEffect<Verbity2>>(core::EffectType::Reverb);
}

}  // namespace effects
