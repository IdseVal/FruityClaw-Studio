#include "audioio/portaudio_device.h"

#include <portaudio.h>

#include <algorithm>

namespace audioio {
namespace {

class PortAudioDevice final : public AudioDevice {
public:
    PortAudioDevice() = default;

    ~PortAudioDevice() override {
        close();
        Pa_Terminate();
    }

    std::vector<DeviceInfo> enumerate() override {
        std::vector<DeviceInfo> devices;
        int count = Pa_GetDeviceCount();
        PaDeviceIndex default_out = Pa_GetDefaultOutputDevice();
        PaDeviceIndex default_in = Pa_GetDefaultInputDevice();
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (!info) continue;
            const PaHostApiInfo* host = Pa_GetHostApiInfo(info->hostApi);

            DeviceInfo device;
            device.id = DeviceId{i};
            device.name = info->name ? info->name : "";
            device.backend_name = host && host->name ? host->name : "";
            device.max_input_channels = info->maxInputChannels;
            device.is_default_input = (i == default_in);
            device.max_output_channels = info->maxOutputChannels;
            device.supported_sample_rates = {44100.0, 48000.0, 96000.0};
            device.is_default_output = (i == default_out);
            devices.push_back(std::move(device));
        }
        return devices;
    }

    Result open(const StreamConfig& config, AudioCallback callback,
                void* user_data) override {
        if (stream_) return Result::failure("Stream already open");
        callback_ = callback;
        user_data_ = user_data;

        PaStreamParameters out_params{};
        out_params.device = config.output_device.value;
        out_params.channelCount = 2;
        out_params.sampleFormat = paFloat32 | paNonInterleaved;
        const PaDeviceInfo* out_info = Pa_GetDeviceInfo(out_params.device);
        if (!out_info) return Result::failure("No such output device");
        out_params.suggestedLatency = out_info->defaultLowOutputLatency;

        PaStreamParameters in_params{};
        const PaStreamParameters* in_ptr = nullptr;
        input_channels_ = 0;
        if (!config.input_device.is_none()) {
            in_params.device = config.input_device.value;
            const PaDeviceInfo* in_info = Pa_GetDeviceInfo(in_params.device);
            if (!in_info || in_info->maxInputChannels < 1)
                return Result::failure("No such input device");
            // A full-duplex stream must sit on one host API. When the input
            // lives elsewhere, the output moves to that host's default output
            // rather than failing — the device-combination quirk this seam
            // exists to hide.
            if (in_info->hostApi != out_info->hostApi) {
                const PaHostApiInfo* host = Pa_GetHostApiInfo(in_info->hostApi);
                if (!host || host->defaultOutputDevice == paNoDevice)
                    return Result::failure("This input cannot share a stream with any output");
                out_params.device = host->defaultOutputDevice;
                out_info = Pa_GetDeviceInfo(out_params.device);
                if (!out_info) return Result::failure("No such output device");
                out_params.suggestedLatency = out_info->defaultLowOutputLatency;
            }
            in_params.channelCount = std::min(in_info->maxInputChannels, 2);
            in_params.sampleFormat = paFloat32 | paNonInterleaved;
            in_params.suggestedLatency = in_info->defaultLowInputLatency;
            in_ptr = &in_params;
            input_channels_ = in_params.channelCount;
        }

        PaError err = Pa_OpenStream(&stream_, in_ptr, &out_params, config.sample_rate,
                                    static_cast<unsigned long>(config.buffer_frames),
                                    paNoFlag, &PortAudioDevice::pa_callback, this);
        if (err != paNoError) {
            input_channels_ = 0;
            return Result::failure(Pa_GetErrorText(err));
        }
        return Result::success();
    }

    Result start() override {
        if (!stream_) return Result::failure("Stream not open");
        PaError err = Pa_StartStream(stream_);
        if (err != paNoError) return Result::failure(Pa_GetErrorText(err));
        return Result::success();
    }

    Result stop() override {
        if (!stream_) return Result::failure("Stream not open");
        PaError err = Pa_StopStream(stream_);
        if (err != paNoError) return Result::failure(Pa_GetErrorText(err));
        return Result::success();
    }

    void close() override {
        if (stream_) {
            Pa_CloseStream(stream_);
            stream_ = nullptr;
        }
    }

    StreamLatency measured_latency() const override {
        StreamLatency latency;
        if (stream_) {
            if (const PaStreamInfo* info = Pa_GetStreamInfo(stream_)) {
                latency.input_seconds = info->inputLatency;
                latency.output_seconds = info->outputLatency;
            }
        }
        return latency;
    }

private:
    static int pa_callback(const void* input, void* output, unsigned long frames,
                           const PaStreamCallbackTimeInfo* time_info,
                           PaStreamCallbackFlags, void* self_ptr) {
        auto* self = static_cast<PortAudioDevice*>(self_ptr);
        StreamTime time{time_info ? time_info->currentTime : 0.0};
        self->callback_(static_cast<const float* const*>(input), self->input_channels_,
                        static_cast<float* const*>(output), 2,
                        static_cast<int>(frames), time, self->user_data_);
        return paContinue;
    }

    PaStream* stream_ = nullptr;
    int input_channels_ = 0;
    AudioCallback callback_ = nullptr;
    void* user_data_ = nullptr;
};

}  // namespace

std::unique_ptr<AudioDevice> make_portaudio_device() {
    if (Pa_Initialize() != paNoError) return nullptr;
    return std::make_unique<PortAudioDevice>();
}

}  // namespace audioio
