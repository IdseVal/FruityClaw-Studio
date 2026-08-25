// TapeDelay2 as the stock Delay (ADR-001).
#include "airwindows/TapeDelay2/TapeDelay2.h"
#include "effects/airwindows_effect.h"

namespace effects {

std::unique_ptr<engine::Processor> make_delay() {
    return std::make_unique<AirwindowsEffect<TapeDelay2>>(core::EffectType::Delay);
}

}  // namespace effects
