#include "engine/engine.h"

#include <algorithm>
#include <cmath>

namespace engine {

Engine::~Engine() {
    // The stream must be stopped before the Engine dies; the composition root
    // owns that ordering. Nothing to hand back here.
    current_.store(nullptr, std::memory_order_release);
}

void Engine::publish(const core::Project& project, double sample_rate) {
    sample_rate_ = sample_rate;
    std::shared_ptr<const RenderModel> next = bake(project, sample_rate);

    current_.store(next.get(), std::memory_order_release);
    if (published_) retire(std::move(published_));
    published_ = std::move(next);
}

void Engine::audition(core::SampleSource audio) {
    if (!audio || audio->frame_count() < 2) return;
    auto cue = std::make_shared<AuditionCue>();
    cue->audio = std::move(audio);
    cue->rate = cue->audio->sample_rate / sample_rate_;

    audition_pending_.store(cue.get(), std::memory_order_release);
    if (auditioning_) retire(std::move(auditioning_));
    auditioning_ = std::move(cue);
}

void Engine::retire(std::shared_ptr<const void> object) {
    // A retired object is deletable once the audio thread has completed two
    // callbacks since retirement: the swap happened before or during the
    // first, so by the second it can no longer be in use. When the stream is
    // not running the epoch does not advance and objects are reclaimed on the
    // next hand-over after the stream stops, which is bounded and harmless.
    std::uint64_t epoch = audio_epoch_.load(std::memory_order_acquire);
    retired_.push_back({std::move(object), epoch});
    std::erase_if(retired_, [epoch](const Retired& r) { return epoch >= r.epoch + 2; });
}

void Engine::play() { playing_.store(true, std::memory_order_release); }

void Engine::stop() { playing_.store(false, std::memory_order_release); }

void Engine::seek(core::Ticks position) {
    const RenderModel* model = current_.load(std::memory_order_acquire);
    double spt = model ? model->samples_per_tick
                       : sample_rate_ * 60.0 / (120.0 * static_cast<double>(core::kPpq));
    seek_samples_.store(
        static_cast<std::int64_t>(static_cast<double>(std::max<core::Ticks>(position, 0)) * spt),
        std::memory_order_release);
}

core::PlaybackStatus Engine::status() const {
    const RenderModel* model = current_.load(std::memory_order_acquire);
    core::PlaybackStatus s;
    s.playing = playing_.load(std::memory_order_acquire);
    double spt = model ? model->samples_per_tick : 0.0;
    std::int64_t samples = playhead_samples_.load(std::memory_order_acquire);
    s.position = spt > 0.0 ? static_cast<core::Ticks>(static_cast<double>(samples) / spt) : 0;
    return s;
}

void Engine::start_voice(const RenderModel& model, const Trigger& trigger) {
    for (Voice& voice : voices_) {
        if (voice.active) continue;
        const BakedInstrument& instrument = model.instruments[trigger.instrument];

        voice.active = true;
        voice.instrument = trigger.instrument;
        voice.src_pos = static_cast<double>(instrument.start_frame);
        // Pitch shift relative to the root, plus rate conversion between the
        // Sample's rate and the output rate.
        double semitones = static_cast<double>(trigger.pitch) -
                           static_cast<double>(instrument.root_pitch);
        voice.rate = std::pow(2.0, semitones / 12.0) *
                     (instrument.audio->sample_rate / model.sample_rate);
        float gain = instrument.gain * trigger.velocity_gain;
        // Equal-power pan.
        float pan = std::clamp(instrument.pan, 0.0f, 1.0f);
        voice.gain_l = gain * std::cos(pan * 1.5707963f);
        voice.gain_r = gain * std::sin(pan * 1.5707963f);
        voice.gate_remaining =
            instrument.mode == core::SamplerMode::Sustain ? trigger.gate_samples : -1;
        voice.envelope = 1.0f;
        return;
    }
    // Voice pool exhausted: the trigger is dropped. Audible under extreme
    // load, never unsafe.
}

void Engine::render_voices(const RenderModel& model, float* const* output,
                           int output_channels, int frames) {
    for (Voice& voice : voices_) {
        if (!voice.active) continue;
        const BakedInstrument& instrument = model.instruments[voice.instrument];
        const core::AudioData& audio = *instrument.audio;
        std::int64_t last_frame = instrument.end_frame > 0
                                      ? std::min(instrument.end_frame, audio.frame_count())
                                      : audio.frame_count();

        for (int i = 0; i < frames; ++i) {
            std::int64_t frame = static_cast<std::int64_t>(voice.src_pos);
            if (frame + 1 >= last_frame) {
                voice.active = false;
                break;
            }
            float frac = static_cast<float>(voice.src_pos - static_cast<double>(frame));

            float left, right;
            if (audio.channels >= 2) {
                const float* s0 = &audio.frames[static_cast<std::size_t>(frame) * audio.channels];
                const float* s1 = s0 + audio.channels;
                left = s0[0] + frac * (s1[0] - s0[0]);
                right = s0[1] + frac * (s1[1] - s0[1]);
            } else {
                float a = audio.frames[static_cast<std::size_t>(frame)];
                float b = audio.frames[static_cast<std::size_t>(frame) + 1];
                left = right = a + frac * (b - a);
            }

            if (voice.gate_remaining == 0) {
                voice.envelope -= kReleasePerSample;
                if (voice.envelope <= 0.0f) {
                    voice.active = false;
                    break;
                }
            } else if (voice.gate_remaining > 0) {
                --voice.gate_remaining;
            }

            output[0][i] += left * voice.gain_l * voice.envelope;
            if (output_channels > 1) output[1][i] += right * voice.gain_r * voice.envelope;
            voice.src_pos += voice.rate;
        }
    }
}

void Engine::render_audition(float* const* output, int output_channels, int frames) {
    // Every block takes the pending cue, so a retired cue is provably unused
    // one callback after its replacement was handed over.
    if (const AuditionCue* next = audition_pending_.exchange(nullptr, std::memory_order_acq_rel)) {
        audition_cue_ = next;
        audition_pos_ = 0.0;
    }
    if (!audition_cue_) return;

    const core::AudioData& audio = *audition_cue_->audio;
    std::int64_t last_frame = audio.frame_count();
    for (int i = 0; i < frames; ++i) {
        std::int64_t frame = static_cast<std::int64_t>(audition_pos_);
        if (frame + 1 >= last_frame) {
            audition_cue_ = nullptr;
            return;
        }
        float frac = static_cast<float>(audition_pos_ - static_cast<double>(frame));
        float left, right;
        if (audio.channels >= 2) {
            const float* s0 = &audio.frames[static_cast<std::size_t>(frame) * audio.channels];
            const float* s1 = s0 + audio.channels;
            left = s0[0] + frac * (s1[0] - s0[0]);
            right = s0[1] + frac * (s1[1] - s0[1]);
        } else {
            float a = audio.frames[static_cast<std::size_t>(frame)];
            float b = audio.frames[static_cast<std::size_t>(frame) + 1];
            left = right = a + frac * (b - a);
        }
        output[0][i] += left * kAuditionGain;
        if (output_channels > 1) output[1][i] += right * kAuditionGain;
        audition_pos_ += audition_cue_->rate;
    }
}

void Engine::render(float* const* output, int output_channels, int frames) {
    for (int c = 0; c < output_channels; ++c)
        std::fill_n(output[c], frames, 0.0f);

    const RenderModel* model = current_.load(std::memory_order_acquire);

    // A model swap invalidates the trigger cursor; recover it by search.
    if (model != last_model_) {
        last_model_ = model;
        if (model) {
            next_trigger_ = static_cast<std::size_t>(
                std::lower_bound(model->triggers.begin(), model->triggers.end(), position_,
                                 [](const Trigger& t, std::int64_t pos) {
                                     return t.sample_pos < pos;
                                 }) -
                model->triggers.begin());
        }
        // Voices from the old model hold indices into it; silence them.
        for (Voice& voice : voices_) voice.active = false;
    }

    std::int64_t seek = seek_samples_.exchange(-1, std::memory_order_acq_rel);
    if (seek >= 0) {
        position_ = seek;
        for (Voice& voice : voices_) voice.active = false;
        if (model) {
            next_trigger_ = static_cast<std::size_t>(
                std::lower_bound(model->triggers.begin(), model->triggers.end(), position_,
                                 [](const Trigger& t, std::int64_t pos) {
                                     return t.sample_pos < pos;
                                 }) -
                model->triggers.begin());
        }
        playhead_samples_.store(position_, std::memory_order_release);
    }

    if (model && playing_.load(std::memory_order_acquire)) {
        std::int64_t block_end = position_ + frames;
        while (next_trigger_ < model->triggers.size() &&
               model->triggers[next_trigger_].sample_pos < block_end) {
            // Sample-accurate starts are a refinement; block-start triggering
            // is inaudible at typical buffer sizes (<= 10 ms).
            start_voice(*model, model->triggers[next_trigger_]);
            ++next_trigger_;
        }
        render_voices(*model, output, output_channels, frames);
        position_ = block_end;
        playhead_samples_.store(position_, std::memory_order_release);
    }

    render_audition(output, output_channels, frames);

    audio_epoch_.fetch_add(1, std::memory_order_acq_rel);
}

}  // namespace engine
