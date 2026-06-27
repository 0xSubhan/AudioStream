#ifdef _WIN32

#include "../include/wasapi_capture.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cmath>

// Link against required Windows libraries
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "uuid.lib")

// Convenience macro for checking HRESULT
#define CHECK_HR(hr, msg) \
    if (FAILED(hr)) { \
        std::cerr << "[WasapiCapture] " << (msg) << " (HRESULT=0x" << std::hex << hr << ")" << std::endl; \
        return false; \
    }

namespace audiostream {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

WasapiCapture::WasapiCapture(int sampleRate, int channels)
    : sampleRate_(sampleRate),
      channels_(channels) {
    // Pre-allocate ring buffer (~4 seconds of stereo float32 at 48kHz)
    ringBuf_.resize(kRingCapacity, 0.0f);
}

WasapiCapture::~WasapiCapture() {
    stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool WasapiCapture::start() {
    if (running_) {
        return true;
    }

    // Create a manual-reset event that we signal when the ring buffer has data.
    // This is used only for the read() blocking wait — NOT passed to WASAPI.
    dataReadyEvent_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!dataReadyEvent_) {
        std::cerr << "[WasapiCapture] Failed to create dataReadyEvent." << std::endl;
        return false;
    }

    running_ = true;
    // The capture thread initialises COM and opens the WASAPI device itself.
    // This avoids COM apartment mismatch (COM objects must be used on the thread
    // that called CoInitializeEx for them).
    captureThread_ = std::thread(&WasapiCapture::captureLoop, this);

    std::cout << "[WasapiCapture] WASAPI loopback capture starting ("
              << sampleRate_ << "Hz, " << channels_ << "ch)." << std::endl;
    return true;
}

bool WasapiCapture::stop() {
    if (!running_) {
        return true;
    }

    running_ = false;

    // Unblock any pending read() call
    if (dataReadyEvent_) {
        SetEvent(dataReadyEvent_);
    }

    if (captureThread_.joinable()) {
        captureThread_.join();
    }

    if (dataReadyEvent_) {
        CloseHandle(dataReadyEvent_);
        dataReadyEvent_ = nullptr;
    }

    // Reset ring buffer
    {
        std::lock_guard<std::mutex> lock(ringMutex_);
        ringWrite_ = ringRead_ = ringFill_ = 0;
    }

    std::cout << "[WasapiCapture] Stopped." << std::endl;
    return true;
}

int WasapiCapture::read(float* buffer, int frames) {
    if (!running_) {
        return -1;
    }

    int samplesNeeded = frames * channels_;

    while (running_) {
        {
            std::lock_guard<std::mutex> lock(ringMutex_);
            if (ringFill_ >= samplesNeeded) {
                // Drain samplesNeeded floats from the ring
                for (int i = 0; i < samplesNeeded; ++i) {
                    buffer[i] = ringBuf_[ringRead_];
                    ringRead_  = (ringRead_ + 1) % static_cast<int>(ringBuf_.size());
                }
                ringFill_ -= samplesNeeded;
                // Reset the event if ring is now low
                if (ringFill_ < samplesNeeded) {
                    ResetEvent(dataReadyEvent_);
                }
                return frames;
            }
        }
        // Wait until the capture thread pushes more data (10ms timeout)
        WaitForSingleObject(dataReadyEvent_, 10);
    }
    return -1;
}

double WasapiCapture::getLatencyMs() const {
    // streamLatency_ is in 100-nanosecond units (REFERENCE_TIME)
    return static_cast<double>(streamLatency_) / 10000.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers (called from the capture worker thread)
// ─────────────────────────────────────────────────────────────────────────────

bool WasapiCapture::initCom() {
    // Must be called from the same thread that will use the COM objects.
    // COINIT_MULTITHREADED is required for WASAPI from a dedicated worker thread.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::cerr << "[WasapiCapture] CoInitializeEx failed (0x" << std::hex << hr << ")." << std::endl;
        return false;
    }
    return true;
}

bool WasapiCapture::openDevice() {
    HRESULT hr;

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator_));
    CHECK_HR(hr, "CoCreateInstance(MMDeviceEnumerator) failed");

    // Get the default audio RENDER (output) endpoint.
    // WASAPI loopback intercepts this endpoint's output stream.
    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    CHECK_HR(hr, "GetDefaultAudioEndpoint failed — ensure an audio output device (speakers/headphones) is connected and set as default");

    return true;
}

bool WasapiCapture::configureStream() {
    HRESULT hr;

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(&audioClient_));
    CHECK_HR(hr, "IAudioClient Activate failed");

    // Query the device's native mix format
    WAVEFORMATEX* nativeFmt = nullptr;
    hr = audioClient_->GetMixFormat(&nativeFmt);
    CHECK_HR(hr, "GetMixFormat failed");

    // Inspect native format for resampling needs
    nativeRate_     = static_cast<int>(nativeFmt->nSamplesPerSec);
    nativeChannels_ = static_cast<int>(nativeFmt->nChannels);
    needsResample_  = (nativeRate_ != sampleRate_) || (nativeChannels_ != channels_);

    if (needsResample_) {
        std::cout << "[WasapiCapture] Native format: "
                  << nativeRate_ << "Hz " << nativeChannels_ << "ch. "
                  << "Will convert to " << sampleRate_ << "Hz " << channels_ << "ch." << std::endl;
    }

    // Request a 20ms buffer period (aligns with Opus frame size)
    REFERENCE_TIME hnsRequestedDuration = 200000; // 20ms in 100ns units

    // CRITICAL: WASAPI loopback MUST NOT use AUDCLNT_STREAMFLAGS_EVENTCALLBACK.
    // Combining LOOPBACK + EVENTCALLBACK is unsupported on Windows and causes
    // IAudioClient::Initialize to return E_INVALIDARG (0x80070057), which silently
    // breaks the entire capture pipeline. Use polling (Sleep-based) instead.
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,   // ← loopback only, NO event callback flag
        hnsRequestedDuration,
        0,          // periodicity — 0 for shared mode
        nativeFmt,
        nullptr);
    CoTaskMemFree(nativeFmt);
    CHECK_HR(hr, "IAudioClient::Initialize (loopback) failed");

    // Query actual stream latency
    hr = audioClient_->GetStreamLatency(&streamLatency_);
    if (FAILED(hr)) streamLatency_ = hnsRequestedDuration;

    hr = audioClient_->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(&captureClient_));
    CHECK_HR(hr, "GetService(IAudioCaptureClient) failed");

    hr = audioClient_->Start();
    CHECK_HR(hr, "IAudioClient::Start failed");

    std::cout << "[WasapiCapture] WASAPI stream configured. Latency: "
              << getLatencyMs() << " ms." << std::endl;
    return true;
}

void WasapiCapture::captureLoop() {
    // ── Step 1: Init COM on THIS thread ──────────────────────────────────────
    // COM objects (IMMDeviceEnumerator, IAudioClient, etc.) must be created and
    // used on the same thread that called CoInitializeEx. Doing this in start()
    // (the main/Qt thread) and then using them here causes COM apartment errors.
    if (!initCom()) {
        std::cerr << "[WasapiCapture] COM init failed in capture thread." << std::endl;
        running_ = false;
        return;
    }

    // ── Step 2: Open WASAPI device on THIS thread ─────────────────────────────
    if (!openDevice() || !configureStream()) {
        std::cerr << "[WasapiCapture] Failed to open WASAPI device in capture thread." << std::endl;
        // Release any partially-initialised COM objects
        if (captureClient_) { captureClient_->Release(); captureClient_ = nullptr; }
        if (audioClient_)   { audioClient_->Stop(); audioClient_->Release(); audioClient_ = nullptr; }
        if (device_)        { device_->Release();        device_        = nullptr; }
        if (enumerator_)    { enumerator_->Release();    enumerator_    = nullptr; }
        CoUninitialize();
        running_ = false;
        return;
    }

    // ── Step 3: Elevate thread priority ──────────────────────────────────────
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

    std::cout << "[WasapiCapture] Capture thread running." << std::endl;

    // ── Step 4: Polling capture loop ──────────────────────────────────────────
    // WASAPI loopback does not support event-driven mode, so we poll every 10ms.
    // This matches the 20ms Opus frame size, giving at least 1 full frame per poll.
    while (running_) {
        // Sleep for half the buffer duration to avoid busy-waiting
        Sleep(10);

        if (!running_) break;

        UINT32 packetSize = 0;
        HRESULT hr = captureClient_->GetNextPacketSize(&packetSize);
        if (FAILED(hr)) {
            std::cerr << "[WasapiCapture] GetNextPacketSize failed — audio device may have been disconnected." << std::endl;
            break;
        }

        while (packetSize > 0 && running_) {
            BYTE*  pData       = nullptr;
            UINT32 numFrames   = 0;
            DWORD  flags       = 0;

            hr = captureClient_->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && numFrames > 0 && pData) {
                // WASAPI gives us IEEE float32 in shared mode (from GetMixFormat)
                const float* src     = reinterpret_cast<const float*>(pData);
                int          srcCh   = nativeChannels_;
                int          dstCh   = channels_;

                std::lock_guard<std::mutex> lock(ringMutex_);

                for (UINT32 f = 0; f < numFrames && running_; ++f) {
                    // Channel fold: stereo→stereo (noop), mono→stereo (dup), stereo→mono (avg)
                    float samples[2] = { 0.0f, 0.0f };
                    if (srcCh == 1) {
                        samples[0] = samples[1] = src[f];
                    } else if (srcCh >= 2) {
                        samples[0] = src[f * srcCh + 0];
                        samples[1] = src[f * srcCh + 1];
                    }

                    int dstSamples = (dstCh == 1) ? 1 : 2;
                    for (int c = 0; c < dstSamples; ++c) {
                        float val = (dstCh == 1) ? (samples[0] + samples[1]) * 0.5f : samples[c];
                        // Only push if ring has space
                        if (ringFill_ < static_cast<int>(ringBuf_.size())) {
                            ringBuf_[ringWrite_] = val;
                            ringWrite_ = (ringWrite_ + 1) % static_cast<int>(ringBuf_.size());
                            ringFill_++;
                        }
                    }
                }

                // Signal the read() caller that data is available
                SetEvent(dataReadyEvent_);
            }

            captureClient_->ReleaseBuffer(numFrames);
            hr = captureClient_->GetNextPacketSize(&packetSize);
            if (FAILED(hr)) goto done;
        }
    }

done:
    if (hTask) {
        AvRevertMmThreadCharacteristics(hTask);
    }

    // Release WASAPI objects on the same thread that created them (COM rule)
    if (captureClient_) { captureClient_->Release(); captureClient_ = nullptr; }
    if (audioClient_)   { audioClient_->Stop();
                          audioClient_->Release();   audioClient_   = nullptr; }
    if (device_)        { device_->Release();        device_        = nullptr; }
    if (enumerator_)    { enumerator_->Release();    enumerator_    = nullptr; }
    CoUninitialize();

    std::cout << "[WasapiCapture] Capture thread exiting." << std::endl;
}

} // namespace audiostream

#endif // _WIN32
