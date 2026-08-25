// Pressure6 as the stock Compressor (ADR-001).
#include "airwindows/Pressure6/Pressure6.h"
#include "effects/airwindows_effect.h"

namespace effects {

std::unique_ptr<engine::Processor> make_compressor() {
    return std::make_unique<AirwindowsEffect<Pressure6>>(core::EffectType::Compressor);
}

}  // namespace effects
