#pragma once

#ifdef _WIN32

#include "capture.h"

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

// Windows & COM headers — must come before Audioclient
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

namespace audiostream {

/**
 * WasapiCapture — Windows WASAPI loopback audio capture.
 *
 * Records system audio output (everything the user hears) using the
 * WASAPI loopback mode, which is the Windows equivalent of PulseAudio's
 * sink monitor source.
 *
 * IMPORTANT: WASAPI loopback does NOT support AUDCLNT_STREAMFLAGS_EVENTCALLBACK.
 * This implementation uses a polling loop (Sleep-based) which is the correct
 * and only supported approach for loopback capture on Windows.
 *
 * COM initialisation is performed inside the worker thread to satisfy COM
 * apartment rules (objects must be used on the thread that created them).
 *
 * Requirements: Windows Vista or later (WASAPI), linked against
 *   ole32, oleaut32, avrt, uuid
 */
class WasapiCapture : public AudioCapture {
public:
    /**
     * @param sampleRate  Desired sample rate in Hz (48000 recommended for Opus).
     * @param channels    Number of audio channels (1 = mono, 2 = stereo).
     */
    explicit WasapiCapture(int sampleRate = 48000, int channels = 2);
    ~WasapiCapture() override;

    // Non-copyable
    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    bool   start()                          override;
    bool   stop()                           override;
    int    read(float* buffer, int frames)  override;

    int    getSampleRate() const override { return sampleRate_; }
    int    getChannels()   const override { return channels_;   }
    double getLatencyMs()  const override;

private:
    // Configuration
    int sampleRate_;
    int channels_;

    // WASAPI COM objects (raw pointers — created and released on the worker thread)
    IMMDeviceEnumerator* enumerator_    = nullptr;
    IMMDevice*           device_        = nullptr;
    IAudioClient*        audioClient_   = nullptr;
    IAudioCaptureClient* captureClient_ = nullptr;

    // Latency reported by WASAPI (in 100-ns units, converted to ms on query)
    REFERENCE_TIME streamLatency_ = 0;

    // Ring buffer shared between the capture thread and read()
    static constexpr int kRingCapacity = 48000 * 2 * 4;  // ~4 seconds of stereo float32
    std::vector<float> ringBuf_;
    int                ringWrite_ = 0;
    int                ringRead_  = 0;
    int                ringFill_  = 0;
    std::mutex         ringMutex_;
    HANDLE             dataReadyEvent_ = nullptr;  // manual-reset, signalled when ring has data

    // Background capture thread
    std::thread captureThread_;
    std::atomic<bool> running_{ false };

    // --- private helpers ---
    bool  initCom();
    bool  openDevice();
    bool  configureStream();
    void  captureLoop();

    // Resampler / format converter state (if WASAPI device format ≠ requested format)
    bool   needsResample_     = false;
    int    nativeRate_        = 0;
    int    nativeChannels_    = 0;
};

} // namespace audiostream

#endif // _WIN32
