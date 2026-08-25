// The transport seam between the UI and the engine.
// docs/specs/architecture-seams.md section 4: the UI writes through commands
// and reads through a polled event stream; it never links the engine or reads
// engine state directly. This narrow port is the command surface, implemented
// by the engine and handed to the UI by the composition root.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// Recording (core document section 3.7: recording from audio inputs).
//
// The recorder is a second narrow port beside TransportPort, on the same
// pattern as AuditionPort: the UI names an input and starts or stops a take;
// the composition root owns the device behind it. A finished take is a
// ready-made immutable AudioData the caller turns into a Sample through the
// sample_functions catalogue — so the recording itself is not a mutation,
// and adding the take to the Project is one undoable Delta like any other.

namespace core {

// One recordable input as the user sees it: a name and a settings label for
// the backend. `id` is opaque and stable for the session; -1 is "no input".
struct InputInfo {
    int id = -1;
    std::string name;
    std::string backend_name;
    int channels = 0;
};

struct RecorderStatus {
    bool recording = false;
    bool input_open = false;       // an input is selected and its stream runs
    std::int64_t frames = 0;       // captured so far in the current take
    double sample_rate = 0.0;
    float peak = 0.0f;             // input peak since the previous status()
    std::int64_t dropped_frames = 0;  // lost to a capture overrun this take
};

class RecorderPort {
public:
    virtual ~RecorderPort() = default;

    virtual std::vector<InputInfo> inputs() = 0;
    virtual int selected_input() const = 0;

    // Routes the named input into the recorder; -1 releases it. Returns an
    // empty string on success, otherwise the reason the input could not be
    // opened, in words fit for the status bar.
    virtual std::string select_input(int id) = 0;

    // False when no input is open or a take is already running.
    virtual bool start_recording() = 0;

    // Ends the take. Null when nothing was recording or nothing was captured.
    virtual SampleSource stop_recording() = 0;

    // Polled on a UI timer. Not const: reading resets the peak hold.
    virtual RecorderStatus status() = 0;
};

}  // namespace core
