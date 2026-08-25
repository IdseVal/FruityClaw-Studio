// The PortAudio adapter behind the AudioDevice seam.
#pragma once

#include <memory>

#include "audioio/audio_device.h"

namespace audioio {

// Creates the PortAudio-backed AudioDevice. Returns nullptr when the backend
// fails to initialise (no audio host at all); the Studio then runs silent.
std::unique_ptr<AudioDevice> make_portaudio_device();

}  // namespace audioio
