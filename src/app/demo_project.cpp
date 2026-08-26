#include "app/demo_project.h"

#include <cmath>
#include <functional>
#include <initializer_list>
#include <random>

namespace app {
namespace {

using namespace core;

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

SampleSource render_pcm(double seconds, const std::function<float(double)>& voice) {
    auto audio = std::make_shared<AudioData>();
    audio->sample_rate = kRate;
    audio->channels = 1;
    int frames = static_cast<int>(seconds * kRate);
    audio->frames.resize(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        audio->frames[static_cast<std::size_t>(i)] = voice(i / kRate);
    }
    return audio;
}

SampleSource make_kick() {
    return render_pcm(0.35, [](double t) {
        // Downward pitch sweep: instantaneous frequency 152 Hz falling to 42 Hz.
        double phase = 2.0 * kPi * (42.0 * t + (110.0 / 9.0) * (1.0 - std::exp(-9.0 * t)));
        return static_cast<float>(std::sin(phase) * std::exp(-7.0 * t));
    });
}

SampleSource make_snare() {
    std::mt19937 rng{7};
    std::uniform_real_distribution<double> noise{-1.0, 1.0};
    return render_pcm(0.22, [rng, noise](double t) mutable {
        double body = std::sin(2.0 * kPi * 185.0 * t) * std::exp(-25.0 * t) * 0.5;
        double snap = noise(rng) * std::exp(-18.0 * t) * 0.55;
        return static_cast<float>(body + snap);
    });
}

SampleSource make_hat() {
    std::mt19937 rng{11};
    std::uniform_real_distribution<double> noise{-1.0, 1.0};
    double previous = 0.0;
    return render_pcm(0.08, [rng, noise, previous](double t) mutable {
        // First difference of noise: a cheap high-pass for a brighter tick.
        double n = noise(rng);
        double high = n - previous;
        previous = n;
        return static_cast<float>(high * std::exp(-60.0 * t) * 0.5);
    });
}

SampleSource make_pluck() {
    // C3 with a few decaying harmonics; the Sampler repitches it per note.
    return render_pcm(1.2, [](double t) {
        double f = 130.81;
        double v = std::sin(2.0 * kPi * f * t) * 0.55 +
                   std::sin(2.0 * kPi * 2.0 * f * t) * 0.22 * std::exp(-3.0 * t) +
                   std::sin(2.0 * kPi * 3.0 * f * t) * 0.12 * std::exp(-5.0 * t);
        return static_cast<float>(v * std::exp(-2.2 * t) * 0.8);
    });
}

Part drum_lane(Id instrument, std::initializer_list<int> sixteenths, Velocity velocity) {
    Part part;
    part.id = new_id();
    part.instrument = instrument;
    for (int step : sixteenths) {
        part.events.push_back(
            Event{new_id(), step * (kPpq / 4), kPpq / 8, 60, velocity});
    }
    return part;
}

Event note(Ticks start, Ticks duration, Pitch pitch, Velocity velocity = 96) {
    return Event{new_id(), start, duration, pitch, velocity};
}

}  // namespace

core::Project make_demo_project() {
    using namespace core;
    Project project;
    project.meta.title = "Demo";
    project.musical.tempo = 112.0;

    Sample kick{new_id(), "Kick", make_kick(), Provenance::human()};
    Sample snare{new_id(), "Snare", make_snare(), Provenance::human()};
    Sample hat{new_id(), "Hat", make_hat(), Provenance::human()};
    Sample pluck{new_id(), "Pluck", make_pluck(), Provenance::human()};
    project.musical.samples.items = {kick, snare, hat, pluck};

    auto drum_instrument = [](const std::string& name, Id sample, float gain) {
        Instrument instrument;
        instrument.id = new_id();
        instrument.name = name;
        instrument.params.sample = sample;
        instrument.params.mode = SamplerMode::OneShot;
        instrument.params.gain = gain;
        return instrument;
    };
    Instrument kick_instrument = drum_instrument("Kick", kick.id, 0.9f);
    Instrument snare_instrument = drum_instrument("Snare", snare.id, 0.7f);
    Instrument hat_instrument = drum_instrument("Hat", hat.id, 0.5f);

    Instrument keys;
    keys.id = new_id();
    keys.name = "Pluck";
    keys.params.sample = pluck.id;
    keys.params.mode = SamplerMode::Sustain;
    keys.params.root_pitch = 48;  // the rendered tone is C3
    keys.params.gain = 0.65f;
    project.musical.instruments.items = {kick_instrument, snare_instrument, hat_instrument, keys};

    // --- drum Patterns: step-sequencer material (16 sixteenths per bar) -----
    Pattern drums_a;
    drums_a.id = new_id();
    drums_a.name = "Drums A";
    drums_a.length = 4 * kPpq;
    drums_a.parts = {
        drum_lane(kick_instrument.id, {0, 6, 8}, 110),
        drum_lane(snare_instrument.id, {4, 12}, 100),
        drum_lane(hat_instrument.id, {0, 2, 4, 6, 8, 10, 12, 14}, 70),
    };

    Pattern drums_b;
    drums_b.id = new_id();
    drums_b.name = "Drums B";
    drums_b.length = 4 * kPpq;
    drums_b.parts = {
        drum_lane(kick_instrument.id, {0, 6, 8, 10}, 110),
        drum_lane(snare_instrument.id, {4, 12, 15}, 100),
        drum_lane(hat_instrument.id, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
                  62),
    };

    // --- melody Patterns: piano-roll material -------------------------------
    Pattern bass;
    bass.id = new_id();
    bass.name = "Bass A";
    bass.length = 4 * kPpq;
    Part bass_part;
    bass_part.id = new_id();
    bass_part.instrument = keys.id;
    bass_part.events = {
        note(0, kPpq, 36),                    // C2
        note(kPpq, kPpq / 2, 43),             // G2
        note(2 * kPpq, kPpq, 39),             // Eb2
        note(3 * kPpq, kPpq / 2, 46),         // Bb2
        note(3 * kPpq + kPpq / 2, kPpq / 2, 43),
    };
    bass.parts = {bass_part};

    Pattern lead;
    lead.id = new_id();
    lead.name = "Melody A";
    lead.length = 8 * kPpq;
    Part lead_part;
    lead_part.id = new_id();
    lead_part.instrument = keys.id;
    lead_part.events = {
        note(0, 2 * kPpq, 60), note(2 * kPpq, kPpq, 63),
        note(3 * kPpq, kPpq, 67), note(4 * kPpq, 2 * kPpq, 65),
        note(6 * kPpq, kPpq, 63), note(7 * kPpq, kPpq, 58),
    };
    lead.parts = {lead_part};

    project.musical.patterns.items = {drums_a, drums_b, bass, lead};

    // --- one Arrangement, three empty Tracks ready to place onto ------------
    Arrangement arrangement;
    arrangement.id = new_id();
    arrangement.name = "Arrangement";
    arrangement.tracks = {
        Track{new_id(), "Drums", {}, false, {}},
        Track{new_id(), "Bass", {}, false, {}},
        Track{new_id(), "Lead", {}, false, {}},
    };
    project.musical.arrangements.items = {arrangement};

    return project;
}

}  // namespace app
