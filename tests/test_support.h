// Shared fixture: a small Project with one Arrangement, two Instruments
// (a OneShot drum and a Sustain melody voice, both over generated PCM), one
// drum-style Pattern and one melody-style Pattern, and two empty Tracks.
#pragma once

#include <cmath>

#include "core/entities.h"

namespace test_support {

inline core::SampleSource make_tone(double seconds = 0.25, double hz = 220.0) {
    auto audio = std::make_shared<core::AudioData>();
    audio->sample_rate = 48000.0;
    audio->channels = 1;
    int frames = static_cast<int>(seconds * audio->sample_rate);
    audio->frames.resize(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        double t = i / audio->sample_rate;
        audio->frames[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * t) *
                               std::exp(-3.0 * t));
    }
    return audio;
}

struct Fixture {
    core::Project project;
    core::Id arrangement;
    core::Id track_a;
    core::Id track_b;
    core::Id drum_pattern;    // one bar of OneShot hits
    core::Id melody_pattern;  // one bar of Sustain notes
};

inline Fixture make_fixture() {
    using namespace core;
    Fixture f;
    f.project.meta.title = "Test";
    f.project.musical.tempo = 120.0;

    Sample drum_hit{new_id(), "hit", make_tone(0.1, 100.0), Provenance::human()};
    Sample tone{new_id(), "tone", make_tone(0.5, 220.0), Provenance::human()};
    f.project.musical.samples.items = {drum_hit, tone};

    Instrument drum;
    drum.id = new_id();
    drum.name = "Drum";
    drum.params.sample = drum_hit.id;
    drum.params.mode = SamplerMode::OneShot;

    Instrument keys;
    keys.id = new_id();
    keys.name = "Keys";
    keys.params.sample = tone.id;
    keys.params.mode = SamplerMode::Sustain;
    keys.params.root_pitch = 57;  // the tone is A3
    f.project.musical.instruments.items = {drum, keys};

    Pattern drums;
    drums.id = new_id();
    drums.name = "Drums A";
    drums.length = 4 * kPpq;
    Part drum_part;
    drum_part.id = new_id();
    drum_part.instrument = drum.id;
    for (int beat = 0; beat < 4; ++beat) {
        drum_part.events.push_back(
            Event{new_id(), beat * kPpq, kPpq / 4, 60, 100});
    }
    drums.parts.push_back(drum_part);

    Pattern melody;
    melody.id = new_id();
    melody.name = "Melody A";
    melody.length = 4 * kPpq;
    Part melody_part;
    melody_part.id = new_id();
    melody_part.instrument = keys.id;
    melody_part.events.push_back(Event{new_id(), 0, 2 * kPpq, 57, 100});
    melody_part.events.push_back(Event{new_id(), 2 * kPpq, 2 * kPpq, 64, 90});
    melody.parts.push_back(melody_part);
    f.project.musical.patterns.items = {drums, melody};

    Arrangement arrangement;
    arrangement.id = new_id();
    arrangement.name = "Arrangement";
    arrangement.tracks.push_back(Track{new_id(), "Track 1", {}, false, {}});
    arrangement.tracks.push_back(Track{new_id(), "Track 2", {}, false, {}});
    f.project.musical.arrangements.items = {arrangement};

    f.arrangement = arrangement.id;
    f.track_a = arrangement.tracks[0].id;
    f.track_b = arrangement.tracks[1].id;
    f.drum_pattern = drums.id;
    f.melody_pattern = melody.id;
    return f;
}

}  // namespace test_support
