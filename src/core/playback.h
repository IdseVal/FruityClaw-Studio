// The transport seam between the UI and the engine.
// docs/specs/architecture-seams.md section 4: the UI writes through commands
// and reads through a polled event stream; it never links the engine or reads
// engine state directly. This narrow port is the command surface, implemented
// by the engine and handed to the UI by the composition root.
#pragma once

#include "core/entities.h"
#include "core/primitives.h"

namespace core {

struct PlaybackStatus {
    bool playing = false;
    Ticks position = 0;
};

class TransportPort {
public:
    virtual ~TransportPort() = default;

    virtual void play() = 0;
    virtual void stop() = 0;

    // Moves the playhead. Valid while stopped or playing.
    virtual void seek(Ticks position) = 0;

    // Snapshot of the last published transport state; safe to poll on a UI
    // timer.
    virtual PlaybackStatus status() const = 0;
};

// The audition command: play one Sample once, now, at its native pitch,
// independent of the transport. It is what the Sample sidebar does when a
// Sample is clicked (core document section 3.7). A second audition replaces
// the first. Like every structural hand-over across seam C, the audio is a
// ready-made immutable object the audio thread only swaps a pointer to.
class AuditionPort {
public:
    virtual ~AuditionPort() = default;

    virtual void audition(SampleSource audio) = 0;
};

}  // namespace core
