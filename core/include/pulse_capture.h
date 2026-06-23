#pragma once

#include "capture.h"
#include <pulse/simple.h>
#include <string>

namespace audiostream {

class PulseAudioCapture : public AudioCapture {
public:
    /**
     * Constructs a PulseAudio capture client.
     * @param sampleRate Target sample rate (default 48000).
     * @param channels Target channel count (default 2).
     * @param deviceName Optional PulseAudio source device name. If empty, the default monitor source is auto-resolved.
     */
    PulseAudioCapture(int sampleRate = 48000, int channels = 2, const std::string& deviceName = "");
    ~PulseAudioCapture() override;

    bool start() override;
    bool stop() override;
    int read(float* buffer, int frames) override;

    int getSampleRate() const override { return sampleRate_; }
    int getChannels() const override { return channels_; }
    double getLatencyMs() const override;

private:
    std::string resolveMonitorSource();

    int sampleRate_;
    int channels_;
    std::string deviceName_;
    pa_simple* paSimple_;
    bool isRunning_;
};

} // namespace audiostream
