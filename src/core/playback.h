// The transport seam between the UI and the engine.
// docs/specs/architecture-seams.md section 4: the UI writes through commands
// and reads through a polled event stream; it never links the engine or reads
// engine state directly. This narrow port is the command surface, implemented
// by the engine and handed to the UI by the composition root.
#pragma once

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

}  // namespace core
