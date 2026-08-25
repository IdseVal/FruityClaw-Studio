// A stand-in for the VST2 SDK header the vendored Airwindows sources include.
//
// This file is project code, not Airwindows code and not Steinberg code. The
// Airwindows LinuxVST sources derive from AudioEffectX and call a handful of
// its members; the real SDK is neither redistributable nor wanted (ADR-001:
// each pick's processing block is lifted into the project's own Processor).
// Providing the base class here lets every vendored file compile verbatim,
// so upstream updates are a plain file copy.
//
// Only what the five ADR-001 picks reach is defined. Everything a host would
// have observed (names, categories, chunks) is accepted and ignored; the
// adapter in src/effects/ talks to the plugin through setParameter /
// getParameter / processReplacing and the sample rate alone.
#pragma once
#define __audioeffect__

// The plugin headers include <math.h> after this file and use M_PI, which
// MSVC only defines on request.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int32_t VstInt32;
typedef void* audioMasterCallback;

enum VstPlugCategory { kPlugCategUnknown = 0, kPlugCategEffect = 1 };

const VstInt32 kVstMaxParamStrLen = 8;
const VstInt32 kVstMaxProgNameLen = 24;
const VstInt32 kVstMaxProductStrLen = 64;
const VstInt32 kVstMaxVendorStrLen = 64;

inline void vst_strncpy(char* dst, const char* src, size_t max_len) {
    size_t i = 0;
    for (; i < max_len && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

inline void float2string(float value, char* text, VstInt32 max_len) {
    snprintf(text, static_cast<size_t>(max_len) + 1, "%.3f", static_cast<double>(value));
}

inline void int2string(VstInt32 value, char* text, VstInt32 max_len) {
    snprintf(text, static_cast<size_t>(max_len) + 1, "%d", static_cast<int>(value));
}

class AudioEffect {
public:
    AudioEffect(audioMasterCallback, VstInt32, VstInt32) {}
    virtual ~AudioEffect() {}

    virtual void processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) = 0;
    virtual float getParameter(VstInt32 index) = 0;
    virtual void setParameter(VstInt32 index, float value) = 0;

    // The one host fact the processing blocks read.
    float getSampleRate() { return sampleRate; }
    void setSampleRate(float rate) { sampleRate = rate; }

    void setNumInputs(VstInt32) {}
    void setNumOutputs(VstInt32) {}
    void setUniqueID(VstInt32) {}
    void canProcessReplacing(bool = true) {}
    void programsAreChunks(bool = true) {}

private:
    float sampleRate = 44100.0f;
};

class AudioEffectX : public AudioEffect {
public:
    using AudioEffect::AudioEffect;

    void canDoubleReplacing(bool = true) {}
};
