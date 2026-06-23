#include "../include/pulse_capture.h"
#include <iostream>
#include <cstdio>
#include <pulse/error.h>

namespace audiostream {

PulseAudioCapture::PulseAudioCapture(int sampleRate, int channels, const std::string& deviceName)
    : sampleRate_(sampleRate),
      channels_(channels),
      deviceName_(deviceName),
      paSimple_(nullptr),
      isRunning_(false) {}

PulseAudioCapture::~PulseAudioCapture() {
    stop();
}

std::string PulseAudioCapture::resolveMonitorSource() {
    if (!deviceName_.empty()) {
        return deviceName_;
    }

    // Run pactl to find the default sink
    FILE* pipe = popen("pactl get-default-sink 2>/dev/null", "r");
    if (!pipe) {
        std::cerr << "[PulseAudioCapture] Failed to execute pactl to get default sink. Defaulting to system fallback." << std::endl;
        return "";
    }

    char buffer[256];
    std::string sinkName = "";
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        sinkName = buffer;
        // Strip trailing whitespaces/newlines
        while (!sinkName.empty() && (sinkName.back() == '\n' || sinkName.back() == '\r' || sinkName.back() == ' ')) {
            sinkName.pop_back();
        }
    }
    pclose(pipe);

    if (sinkName.empty()) {
        std::cerr << "[PulseAudioCapture] Default sink name is empty. Using PulseAudio default source (microphone fallback)." << std::endl;
        return "";
    }

    // Typical naming convention for PulseAudio loopback monitors
    std::string monitorSource = sinkName + ".monitor";
    std::cout << "[PulseAudioCapture] Resolved monitor source: " << monitorSource << std::endl;
    return monitorSource;
}

bool PulseAudioCapture::start() {
    if (isRunning_) {
        return true;
    }

    std::string sourceName = resolveMonitorSource();
    const char* dev = sourceName.empty() ? nullptr : sourceName.c_str();

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32NE; // Native-endian 32-bit floats
    ss.channels = static_cast<uint8_t>(channels_);
    ss.rate = static_cast<uint32_t>(sampleRate_);

    // Buffer attributes for low latency
    pa_buffer_attr attr;
    attr.maxlength = (uint32_t)-1;
    attr.tlength = (uint32_t)-1;
    attr.prebuf = (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    // Request 5ms fragment size (5ms = 240 frames @ 48kHz)
    attr.fragsize = 240 * channels_ * sizeof(float);

    int error = 0;
    paSimple_ = pa_simple_new(
        nullptr,               // Use default server
        "AudioStream",         // App name
        PA_STREAM_RECORD,      // Record direction
        dev,                   // Device name (monitor source for system audio loopback)
        "System Audio Capture",// Stream description
        &ss,                   // Sample spec
        nullptr,               // Default channel map
        &attr,                 // Low-latency buffering attributes
        &error                 // Error code receiver
    );

    if (!paSimple_) {
        std::cerr << "[PulseAudioCapture] pa_simple_new() failed: " << pa_strerror(error) << std::endl;
        return false;
    }

    isRunning_ = true;
    std::cout << "[PulseAudioCapture] Successfully started capture from: " 
              << (dev ? dev : "Default Source") << std::endl;
    return true;
}

bool PulseAudioCapture::stop() {
    if (paSimple_) {
        pa_simple_free(paSimple_);
        paSimple_ = nullptr;
    }
    isRunning_ = false;
    return true;
}

int PulseAudioCapture::read(float* buffer, int frames) {
    if (!paSimple_ || !isRunning_) {
        std::cerr << "[PulseAudioCapture] Read called on stopped or uninitialized capture." << std::endl;
        return -1;
    }

    size_t bytesToRead = frames * channels_ * sizeof(float);
    int error = 0;

    if (pa_simple_read(paSimple_, buffer, bytesToRead, &error) < 0) {
        std::cerr << "[PulseAudioCapture] pa_simple_read() failed: " << pa_strerror(error) << std::endl;
        return -1;
    }

    return frames;
}

double PulseAudioCapture::getLatencyMs() const {
    if (!paSimple_) {
        return -1.0;
    }
    int error = 0;
    pa_usec_t latency = pa_simple_get_latency(paSimple_, &error);
    if (latency == (pa_usec_t)-1) {
        return -1.0;
    }
    return static_cast<double>(latency) / 1000.0;
}

} // namespace audiostream
