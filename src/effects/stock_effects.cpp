#include "effects/stock_effects.h"

#include "effects/airwindows_effect.h"
#include "effects/peaklim_limiter.h"

namespace effects {

std::unique_ptr<engine::Processor> make_effect(core::EffectType type) {
    switch (type) {
        case core::EffectType::Eq: return make_eq();
        case core::EffectType::Compressor: return make_compressor();
        case core::EffectType::Limiter: return std::make_unique<PeaklimLimiter>();
        case core::EffectType::Reverb: return make_reverb();
        case core::EffectType::Delay: return make_delay();
        case core::EffectType::Distortion: return make_distortion();
    }
    return nullptr;
}

}  // namespace effects
