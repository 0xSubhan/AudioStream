#ifdef _WIN32

#include "../include/wasapi_capture.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <fstream>
#include <string>
#include <sstream>

// Link against required Windows libraries
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "uuid.lib")

inline void log_debug(const std::string& msg) {
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
        std::string exePath(path);
        std::string logPath = exePath.substr(0, exePath.find_last_of("\\/")) + "\\AudioStream_debug.log";
        std::ofstream file(logPath, std::ios::app);
        if (file.is_open()) {
            file << "[C++] " << msg << std::endl;
        }
    }
}

// Convenience macro for checking HRESULT
#define CHECK_HR(hr, msg) \
    if (FAILED(hr)) { \
        std::ostringstream ss; \
        ss << (msg) << " (HRESULT=0x" << std::hex << hr << ")"; \
        log_debug(ss.str()); \
        std::cerr << "[WasapiCapture] " << ss.str() << std::endl; \
        return false; \
    }

// ─────────────────────────────────────────────────────────────────────────────
// Thread-based timeout wrapper for IAudioClient::Start().
//
// Problem: IAudioClient::Start() can block indefinitely on Windows when the
// audio engine is idle and no render stream is active. The SEH __try/__except
// wrapper only catches crashes — NOT infinite blocking waits.
//
// Solution: Run Start() on a short-lived worker thread. Use an event to signal
// completion. Wait up to kStartTimeoutMs milliseconds, then fail gracefully.
// ─────────────────────────────────────────────────────────────────────────────

struct StartThreadArg {
    IAudioClient* client;
    HRESULT       result;
    HANDLE        doneEvent;
};

static DWORD WINAPI start_audio_client_thread(LPVOID param) {
    auto* arg = static_cast<StartThreadArg*>(param);
    arg->result = arg->client->Start();
    SetEvent(arg->doneEvent);
    return 0;
}

// Returns true on success; false on failure or timeout.
static bool timed_start_audio_client(IAudioClient* client,
                                     DWORD timeoutMs,
                                     HRESULT& outHr) {
    outHr = E_FAIL;

    HANDLE doneEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!doneEvent) {
        log_debug("timed_start: Failed to create done event");
        return false;
    }

    StartThreadArg arg{ client, E_FAIL, doneEvent };

    HANDLE hThread = CreateThread(nullptr, 0, start_audio_client_thread, &arg, 0, nullptr);
    if (!hThread) {
        log_debug("timed_start: Failed to create start thread");
        CloseHandle(doneEvent);
        return false;
    }

    DWORD waitResult = WaitForSingleObject(doneEvent, timeoutMs);
    if (waitResult == WAIT_OBJECT_0) {
        // Start() returned within the timeout
        outHr = arg.result;
        CloseHandle(hThread);
        CloseHandle(doneEvent);
        return SUCCEEDED(outHr);
    }

    // TIMEOUT — Start() is hanging. We cannot safely terminate the thread
    // because it is stuck inside the OS audio engine. Leave it as a zombie;
    // it will eventually be cleaned up when the process exits.
    // This is far better than hanging the UI forever.
    {
        std::ostringstream ss;
        ss << "timed_start: IAudioClient::Start() timed out after " << timeoutMs << " ms — audio engine is stuck!";
        log_debug(ss.str());
    }
    CloseHandle(hThread);
    CloseHandle(doneEvent);
    outHr = AUDCLNT_E_DEVICE_INVALIDATED;   // synthetic error to signal failure
    return false;
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
    log_debug("WasapiCapture::start() called");
    if (running_) {
        log_debug("WasapiCapture already running");
        return true;
    }

    // Create an auto-reset event that we signal when the ring buffer has data.
    // This is used only for the read() blocking wait — NOT passed to WASAPI.
    dataReadyEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!dataReadyEvent_) {
        log_debug("Failed to create dataReadyEvent");
        std::cerr << "[WasapiCapture] Failed to create dataReadyEvent." << std::endl;
        return false;
    }

    running_ = true;
    // The capture thread initialises COM and opens the WASAPI device itself.
    // This avoids COM apartment mismatch (COM objects must be used on the thread
    // that called CoInitializeEx for them).
    try {
        captureThread_ = std::thread(&WasapiCapture::captureLoop, this);
    } catch (const std::exception& e) {
        log_debug(std::string("Failed to spawn capture thread: ") + e.what());
        running_ = false;
        return false;
    }

    log_debug("WASAPI loopback capture starting");
    std::cout << "[WasapiCapture] WASAPI loopback capture starting ("
              << sampleRate_ << "Hz, " << channels_ << "ch)." << std::endl;
    return true;
}

bool WasapiCapture::stop() {
    log_debug("WasapiCapture::stop() called");
    if (!running_) {
        log_debug("WasapiCapture not running");
        return true;
    }

    running_ = false;

    // Unblock any pending read() call
    if (dataReadyEvent_) {
        SetEvent(dataReadyEvent_);
    }

    if (captureThread_.joinable()) {
        log_debug("Joining capture thread...");
        captureThread_.join();
        log_debug("Capture thread joined");
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

    log_debug("WasapiCapture stopped completely");
    std::cout << "[WasapiCapture] Stopped." << std::endl;
    return true;
}

int WasapiCapture::read(float* buffer, int frames) {
    if (!running_) {
        return -1;
    }

    int samplesNeeded = frames * channels_;
    int totalWaitTimeMs = 0;
    const int kMaxWaitTimeMs = 25;

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
                return frames;
            }
        }

        // Wait until the capture thread pushes more data (5ms timeout)
        DWORD waitResult = WaitForSingleObject(dataReadyEvent_, 5);
        if (waitResult == WAIT_TIMEOUT) {
            totalWaitTimeMs += 5;
            if (totalWaitTimeMs >= kMaxWaitTimeMs) {
                // Output silence if we timed out waiting for real audio
                std::lock_guard<std::mutex> lock(ringMutex_);
                int samplesToRead = (std::min)(ringFill_, samplesNeeded);

                // Read whatever is in the ring buffer
                for (int i = 0; i < samplesToRead; ++i) {
                    buffer[i] = ringBuf_[ringRead_];
                    ringRead_  = (ringRead_ + 1) % static_cast<int>(ringBuf_.size());
                }
                ringFill_ -= samplesToRead;

                // Pad the rest of the buffer with silence
                for (int i = samplesToRead; i < samplesNeeded; ++i) {
                    buffer[i] = 0.0f;
                }
                return frames;
            }
        }
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
    // Use MTA — STA requires a message pump which our worker thread does not have,
    // causing COM marshaling inside IAudioClient::Start() to deadlock.
    log_debug("Calling CoInitializeEx with COINIT_MULTITHREADED...");
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        std::ostringstream ss;
        ss << "CoInitializeEx returned HRESULT = 0x" << std::hex << hr;
        log_debug(ss.str());
    }
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

    // Query and log default audio device state
    DWORD state = 0;
    hr = device_->GetState(&state);
    if (SUCCEEDED(hr)) {
        std::ostringstream ss;
        ss << "Default audio device state = " << state << " (1=Active, 2=Disabled, 4=NotPresent, 8=Unplugged)";
        log_debug(ss.str());
    } else {
        log_debug("GetState on audio device failed");
    }

    return true;
}

bool WasapiCapture::configureStream() {
    log_debug("configureStream() called");
    HRESULT hr;

    // ── Step A: Query native device mix format ────────────────────────────────
    // We need this FIRST to properly initialize the silent render stream.
    // The render stream MUST use the device's native mix format (or a compatible
    // one) to succeed — using AUTOCONVERTPCM alone is insufficient for render
    // when the engine is already in a specific mode.

    IAudioClient* pQueryClient = nullptr;
    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&pQueryClient));
    CHECK_HR(hr, "IAudioClient Activate (query) failed");

    WAVEFORMATEX* nativeFmtRaw = nullptr;
    bool nativeFmtNeedsCoFree = false;  // true only if CoTaskMemAlloc'd by GetMixFormat
    hr = pQueryClient->GetMixFormat(&nativeFmtRaw);
    pQueryClient->Release();
    pQueryClient = nullptr;

    // Fallback static format (stack/static — must NOT be CoTaskMemFree'd)
    static WAVEFORMATEXTENSIBLE fallbackFmt = {};

    if (FAILED(hr) || !nativeFmtRaw) {
        log_debug("GetMixFormat failed — using 48kHz stereo float as native format");
        fallbackFmt.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
        fallbackFmt.Format.nChannels       = 2;
        fallbackFmt.Format.nSamplesPerSec  = 48000;
        fallbackFmt.Format.wBitsPerSample  = 32;
        fallbackFmt.Format.nBlockAlign     = 8;
        fallbackFmt.Format.nAvgBytesPerSec = 48000 * 8;
        fallbackFmt.Format.cbSize          = 22;
        fallbackFmt.Samples.wValidBitsPerSample = 32;
        fallbackFmt.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
        fallbackFmt.SubFormat     = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        nativeFmtRaw = reinterpret_cast<WAVEFORMATEX*>(&fallbackFmt);
        nativeFmtNeedsCoFree = false;
    } else {
        nativeFmtNeedsCoFree = true;  // returned by GetMixFormat → CoTaskMemAlloc'd
    }

    // Log native format
    {
        nativeRate_          = static_cast<int>(nativeFmtRaw->nSamplesPerSec);
        nativeChannels_      = static_cast<int>(nativeFmtRaw->nChannels);
        nativeBitsPerSample_ = nativeFmtRaw->wBitsPerSample;
        nativeIsFloat_       = true;
        if (nativeFmtRaw->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(nativeFmtRaw);
            nativeIsFloat_ = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        } else {
            nativeIsFloat_ = (nativeFmtRaw->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        }
        std::ostringstream fmtLog;
        fmtLog << "Native mix format: " << nativeRate_ << "Hz, "
               << nativeChannels_ << "ch, " << nativeBitsPerSample_ << "-bit "
               << (nativeIsFloat_ ? "Float" : "PCM");
        log_debug(fmtLog.str());
    }

    // ── Step B: Start the SILENT RENDER STREAM first ──────────────────────────
    // CRITICAL: The render stream must be started BEFORE the loopback capture
    // client is started. The audio engine only keeps the mixer running when
    // there is at least one active render session. Without this, the loopback
    // capture Start() will block indefinitely waiting for the engine to wake.
    //
    // FIX vs. v1.2.2: We now use the device's NATIVE mix format for the render
    // stream initialization. Using a mismatched format (e.g., 48kHz float when
    // device is 44100Hz) caused Initialize() to silently fail (AUDCLNT_E_-
    // UNSUPPORTED_FORMAT), leaving the engine dormant and reproducing the hang.
    log_debug("Activating silent render client...");
    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&silentRenderClient_));
    if (FAILED(hr)) {
        std::ostringstream ss;
        ss << "Silent render Activate() failed 0x" << std::hex << hr << " (non-fatal, continuing)";
        log_debug(ss.str());
    } else {
        log_debug("Silent render client activated. Calling Initialize with native format...");
        // Use native format + AUTOCONVERTPCM so even if we guessed wrong it adapts
        HRESULT hrRender = silentRenderClient_->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            // Request a 200ms buffer — generous, ensures engine has time to wake
            2000000LL,  // 200ms in 100-ns units
            0,
            nativeFmtRaw,
            nullptr);

        if (FAILED(hrRender)) {
            std::ostringstream ss;
            ss << "Silent render Initialize() failed 0x" << std::hex << hrRender
               << " — engine may stay idle (non-fatal)";
            log_debug(ss.str());
            // Try again with a minimal format that must work
            WAVEFORMATEX simpleFmt = {};
            simpleFmt.wFormatTag      = WAVE_FORMAT_PCM;
            simpleFmt.nChannels       = 2;
            simpleFmt.nSamplesPerSec  = 44100;
            simpleFmt.wBitsPerSample  = 16;
            simpleFmt.nBlockAlign     = 4;
            simpleFmt.nAvgBytesPerSec = 44100 * 4;
            simpleFmt.cbSize          = 0;
            hrRender = silentRenderClient_->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                2000000LL, 0, &simpleFmt, nullptr);
            if (FAILED(hrRender)) {
                std::ostringstream ss2;
                ss2 << "Silent render Initialize() fallback also failed 0x" << std::hex << hrRender;
                log_debug(ss2.str());
            }
        }

        if (SUCCEEDED(hrRender)) {
            // Pre-fill the render buffer with silence so the engine doesn't starve
            IAudioRenderClient* pRC = nullptr;
            if (SUCCEEDED(silentRenderClient_->GetService(__uuidof(IAudioRenderClient),
                                                          reinterpret_cast<void**>(&pRC)))) {
                UINT32 nFrames = 0;
                silentRenderClient_->GetBufferSize(&nFrames);
                BYTE* pBuf = nullptr;
                if (SUCCEEDED(pRC->GetBuffer(nFrames, &pBuf))) {
                    // Release with AUDCLNT_BUFFERFLAGS_SILENT — no need to zero pBuf
                    pRC->ReleaseBuffer(nFrames, AUDCLNT_BUFFERFLAGS_SILENT);
                }
                pRC->Release();
                {
                    std::ostringstream ss;
                    ss << "Silent render buffer pre-filled with " << nFrames << " silent frames";
                    log_debug(ss.str());
                }
            }

            hrRender = silentRenderClient_->Start();
            if (SUCCEEDED(hrRender)) {
                log_debug("Silent render stream started successfully — audio engine is now active.");
            } else {
                std::ostringstream ss;
                ss << "Silent render Start() returned 0x" << std::hex << hrRender << " (non-fatal)";
                log_debug(ss.str());
            }
        }
    }

    // Free the native format memory if it was CoTaskMemAlloc'd by GetMixFormat
    if (nativeFmtNeedsCoFree && nativeFmtRaw) {
        CoTaskMemFree(nativeFmtRaw);
        nativeFmtRaw = nullptr;
    }

    // ── Step C: Give the engine a moment to wake up ───────────────────────────
    // After starting the render stream, the audio engine scheduler needs a small
    // amount of time to spin up before loopback capture can succeed.
    log_debug("Sleeping 50ms to let render engine wake up...");
    Sleep(50);

    // ── Step D: Set up the loopback capture client ────────────────────────────
    log_debug("Activating loopback capture client...");
    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(&audioClient_));
    CHECK_HR(hr, "IAudioClient Activate (loopback) failed");

    // Build our own well-defined target format: 48 kHz, stereo, 32-bit IEEE float.
    // AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM tells Windows to handle all sample-rate
    // and format conversion internally — this is the most stable initialization path.
    WAVEFORMATEXTENSIBLE wfx = {};
    wfx.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels       = static_cast<WORD>(channels_);
    wfx.Format.nSamplesPerSec  = static_cast<DWORD>(sampleRate_);
    wfx.Format.wBitsPerSample  = 32;
    wfx.Format.nBlockAlign     = wfx.Format.nChannels * 4;
    wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.cbSize          = 22;
    wfx.Samples.wValidBitsPerSample = 32;
    wfx.dwChannelMask = (channels_ == 2) ? KSAUDIO_SPEAKER_STEREO : KSAUDIO_SPEAKER_MONO;
    wfx.SubFormat     = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    // From now on, data from captureLoop is always 48kHz / stereo / float32
    nativeIsFloat_       = true;
    nativeBitsPerSample_ = 32;
    nativeChannels_      = channels_;
    nativeRate_          = sampleRate_;
    needsResample_       = false;

    log_debug("Calling audioClient_->Initialize with AUTOCONVERTPCM...");
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        0,       // hnsBufferDuration — 0 = engine default
        0,       // hnsPeriodicity   — 0 for shared mode
        reinterpret_cast<WAVEFORMATEX*>(&wfx),
        nullptr);
    log_debug("Checking Initialize HRESULT...");
    CHECK_HR(hr, "IAudioClient::Initialize (loopback+autoconvert) failed");

    // Query actual stream latency
    log_debug("Querying stream latency...");
    hr = audioClient_->GetStreamLatency(&streamLatency_);
    if (FAILED(hr)) {
        log_debug("GetStreamLatency failed, using 0");
        streamLatency_ = 0;
    }

    // Query and log actual allocated buffer size
    UINT32 bufferFrameCount = 0;
    if (SUCCEEDED(audioClient_->GetBufferSize(&bufferFrameCount))) {
        std::ostringstream ss;
        ss << "WASAPI loopback allocated buffer size = " << bufferFrameCount << " frames";
        log_debug(ss.str());
    }

    log_debug("Querying IAudioCaptureClient service...");
    hr = audioClient_->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(&captureClient_));
    CHECK_HR(hr, "GetService(IAudioCaptureClient) failed");
    log_debug("IAudioCaptureClient obtained successfully.");

    // ── Step E: Start the loopback capture client ─────────────────────────────
    // With the render stream already running (Step B), the audio engine is awake
    // and Start() should return promptly. We still apply a timeout (5 seconds)
    // as a safety net against driver-specific bugs.
    log_debug("Starting audioClient_ (loopback capture) with 5s timeout watchdog...");
    HRESULT hrStart = E_FAIL;
    bool started = timed_start_audio_client(audioClient_, 5000, hrStart);

    {
        std::ostringstream ss;
        ss << "audioClient_->Start() returned HRESULT = 0x" << std::hex << hrStart
           << (started ? " (success)" : " (FAILED or timed out)");
        log_debug(ss.str());
    }

    if (!started) {
        std::ostringstream ss;
        ss << "IAudioClient::Start timed out or failed (0x" << std::hex << hrStart
           << "). The audio engine may be in an unrecoverable state.";
        log_debug(ss.str());
        return false;
    }

    {
        std::ostringstream latLog;
        latLog << "WASAPI stream configured. Latency: " << getLatencyMs() << " ms.";
        log_debug(latLog.str());
        std::cout << "[WasapiCapture] " << latLog.str() << std::endl;
    }
    return true;
}

void WasapiCapture::captureLoop() {
    log_debug("captureLoop() thread entry");
    // ── Step 1: Init COM on THIS thread ──────────────────────────────────────
    if (!initCom()) {
        log_debug("COM init failed in capture thread.");
        std::cerr << "[WasapiCapture] COM init failed in capture thread." << std::endl;
        running_ = false;
        return;
    }

    // ── Step 2: Open WASAPI device on THIS thread ─────────────────────────────
    if (!openDevice() || !configureStream()) {
        log_debug("Failed to open or configure WASAPI device in capture thread.");
        std::cerr << "[WasapiCapture] Failed to open WASAPI device in capture thread." << std::endl;
        // Release any partially-initialised COM objects
        if (captureClient_) { captureClient_->Release(); captureClient_ = nullptr; }
        if (audioClient_)   { audioClient_->Release(); audioClient_ = nullptr; }
        if (silentRenderClient_) {
            silentRenderClient_->Stop();
            silentRenderClient_->Release(); silentRenderClient_ = nullptr;
        }
        if (device_)        { device_->Release();        device_        = nullptr; }
        if (enumerator_)    { enumerator_->Release();    enumerator_    = nullptr; }
        CoUninitialize();
        running_ = false;
        return;
    }

    // ── Step 3: Elevate thread priority ──────────────────────────────────────
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

    log_debug("Capture thread running successfully");
    std::cout << "[WasapiCapture] Capture thread running." << std::endl;

    // ── Step 4: Polling capture loop ──────────────────────────────────────────
    while (running_) {
        // Sleep for half the buffer duration to avoid busy-waiting
        Sleep(10);

        if (!running_) break;

        UINT32 packetSize = 0;
        HRESULT hr = captureClient_->GetNextPacketSize(&packetSize);
        if (FAILED(hr)) {
            log_debug("GetNextPacketSize failed — device disconnected?");
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
                int          srcCh   = nativeChannels_;
                int          dstCh   = channels_;

                std::lock_guard<std::mutex> lock(ringMutex_);

                for (UINT32 f = 0; f < numFrames && running_; ++f) {
                    float samples[2] = { 0.0f, 0.0f };
                    
                    // Decode native samples (Float, 16-bit PCM, 24-bit PCM, 32-bit PCM) to float32
                    for (int c = 0; c < (std::min)(srcCh, 2); ++c) {
                        int sampleIdx = f * srcCh + c;
                        if (nativeIsFloat_) {
                            const float* src = reinterpret_cast<const float*>(pData);
                            samples[c] = src[sampleIdx];
                        } else {
                            if (nativeBitsPerSample_ == 16) {
                                const int16_t* src = reinterpret_cast<const int16_t*>(pData);
                                samples[c] = static_cast<float>(src[sampleIdx]) / 32768.0f;
                            } else if (nativeBitsPerSample_ == 24) {
                                const uint8_t* src = reinterpret_cast<const uint8_t*>(pData);
                                int idx = sampleIdx * 3;
                                int32_t val = (src[idx] << 8) | (src[idx + 1] << 16) | (src[idx + 2] << 24);
                                val >>= 8; // Sign extend
                                samples[c] = static_cast<float>(val) / 8388608.0f;
                            } else if (nativeBitsPerSample_ == 32) {
                                const int32_t* src = reinterpret_cast<const int32_t*>(pData);
                                samples[c] = static_cast<float>(src[sampleIdx]) / 2147483648.0f;
                            }
                        }
                    }

                    // Duplication or downmixing depending on requested channel count
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
    log_debug("captureLoop thread exiting done block");
    if (hTask) {
        AvRevertMmThreadCharacteristics(hTask);
    }

    // Release WASAPI objects on the same thread that created them (COM rule)
    if (captureClient_) { captureClient_->Release(); captureClient_ = nullptr; }
    if (audioClient_)   {
        audioClient_->Stop();
        audioClient_->Release();   audioClient_   = nullptr;
    }
    if (silentRenderClient_) {
        silentRenderClient_->Stop();
        silentRenderClient_->Release(); silentRenderClient_ = nullptr;
    }
    if (device_)        { device_->Release();        device_        = nullptr; }
    if (enumerator_)    { enumerator_->Release();    enumerator_    = nullptr; }
    CoUninitialize();

    log_debug("Capture thread exited successfully");
    std::cout << "[WasapiCapture] Capture thread exiting." << std::endl;
}

} // namespace audiostream

#endif // _WIN32
