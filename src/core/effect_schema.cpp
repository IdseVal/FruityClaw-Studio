#include "core/effect_schema.h"

namespace core {
namespace {

// Airwindows controls are all 0..1 with the defaults from each plugin's
// constructor; the names are the upstream labels written out in full.
constexpr EffectParameter kEq[] = {
    {"Treble Freq", 0.0f, 1.0f, 0.5f, ""},   {"Treble", 0.0f, 1.0f, 0.5f, ""},
    {"Treble Reso", 0.0f, 1.0f, 0.5f, ""},   {"High-Mid Freq", 0.0f, 1.0f, 0.5f, ""},
    {"High-Mid", 0.0f, 1.0f, 0.5f, ""},      {"High-Mid Reso", 0.0f, 1.0f, 0.5f, ""},
    {"Low-Mid Freq", 0.0f, 1.0f, 0.5f, ""},  {"Low-Mid", 0.0f, 1.0f, 0.5f, ""},
    {"Low-Mid Reso", 0.0f, 1.0f, 0.5f, ""},  {"Dry/Wet", 0.0f, 1.0f, 1.0f, ""},
};

constexpr EffectParameter kCompressor[] = {
    {"Compress", 0.0f, 1.0f, 0.0f, ""},
    {"Ratio", 0.0f, 1.0f, 1.0f, ""},
};

// peaklim's own units: gain and threshold in dB, release in seconds
// (clamped upstream to 1 ms .. 1 s), true-peak detection as a switch.
constexpr EffectParameter kLimiter[] = {
    {"Input Gain", -10.0f, 30.0f, 0.0f, "dB"},
    {"Threshold", -10.0f, 0.0f, -1.0f, "dB"},
    {"Release", 0.001f, 1.0f, 0.01f, "s"},
    {"True Peak", 0.0f, 1.0f, 0.0f, ""},
};

constexpr EffectParameter kReverb[] = {
    {"Room Size", 0.0f, 1.0f, 0.5f, ""},
    {"Sustain", 0.0f, 1.0f, 0.5f, ""},
    {"Mulch", 0.0f, 1.0f, 0.5f, ""},
    {"Wetness", 0.0f, 1.0f, 1.0f, ""},
};

constexpr EffectParameter kDelay[] = {
    {"Time", 0.0f, 1.0f, 1.0f, ""},    {"Regen", 0.0f, 1.0f, 0.0f, ""},
    {"Freq", 0.0f, 1.0f, 0.5f, ""},    {"Reso", 0.0f, 1.0f, 0.0f, ""},
    {"Flutter", 0.0f, 1.0f, 0.0f, ""}, {"Dry/Wet", 0.0f, 1.0f, 1.0f, ""},
};

constexpr EffectParameter kDistortion[] = {
    {"Input", 0.0f, 1.0f, 0.5f, ""},
    {"Mode", 0.0f, 1.0f, 0.5f, ""},
    {"Output", 0.0f, 1.0f, 0.5f, ""},
    {"Dry/Wet", 0.0f, 1.0f, 1.0f, ""},
};

}  // namespace

std::string_view effect_type_name(EffectType type) {
    switch (type) {
        case EffectType::Eq: return "EQ";
        case EffectType::Compressor: return "Compressor";
        case EffectType::Limiter: return "Limiter";
        case EffectType::Reverb: return "Reverb";
        case EffectType::Delay: return "Delay";
        case EffectType::Distortion: return "Distortion";
    }
    return "";
}

std::span<const EffectParameter> effect_parameters(EffectType type) {
    switch (type) {
        case EffectType::Eq: return kEq;
        case EffectType::Compressor: return kCompressor;
        case EffectType::Limiter: return kLimiter;
        case EffectType::Reverb: return kReverb;
        case EffectType::Delay: return kDelay;
        case EffectType::Distortion: return kDistortion;
    }
    return {};
}

const EffectParameter* find_effect_parameter(EffectType type, std::string_view name) {
    for (const EffectParameter& parameter : effect_parameters(type))
        if (parameter.name == name) return &parameter;
    return nullptr;
}

}  // namespace core
