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

// SEH helper — must be a plain function with no C++ objects (MSVC C2712 rule).
// Returns the HRESULT from Start(), or sets *outCode to the exception code on crash.
static HRESULT seh_start_audio_client(IAudioClient* client, DWORD* outCode) {
    *outCode = 0;
    HRESULT hr = E_FAIL;
    __try {
        hr = client->Start();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outCode = GetExceptionCode();
    }
    return hr;
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

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(&audioClient_));
    CHECK_HR(hr, "IAudioClient Activate failed");

    // Log the native mix format for diagnostics only
    WAVEFORMATEX* nativeFmt = nullptr;
    hr = audioClient_->GetMixFormat(&nativeFmt);
    if (SUCCEEDED(hr) && nativeFmt) {
        nativeRate_          = static_cast<int>(nativeFmt->nSamplesPerSec);
        nativeChannels_      = static_cast<int>(nativeFmt->nChannels);
        nativeBitsPerSample_ = nativeFmt->wBitsPerSample;
        nativeIsFloat_       = true;
        if (nativeFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(nativeFmt);
            nativeIsFloat_ = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        } else {
            nativeIsFloat_ = (nativeFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        }
        std::ostringstream fmtLog;
        fmtLog << "Native format: " << nativeRate_ << "Hz, "
               << nativeChannels_ << "ch, " << nativeBitsPerSample_ << "-bit "
               << (nativeIsFloat_ ? "Float" : "PCM");
        log_debug(fmtLog.str());
        CoTaskMemFree(nativeFmt);
        nativeFmt = nullptr;
    }

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
        ss << "WASAPI allocated buffer size = " << bufferFrameCount << " frames";
        log_debug(ss.str());
    }

    log_debug("Querying IAudioCaptureClient service...");
    hr = audioClient_->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(&captureClient_));
    CHECK_HR(hr, "GetService(IAudioCaptureClient) failed");

    log_debug("Starting audioClient...");
    DWORD exCode = 0;
    HRESULT hrStart = seh_start_audio_client(audioClient_, &exCode);
    if (exCode != 0) {
        std::ostringstream ex;
        ex << "EXCEPTION inside audioClient_->Start()! Code=0x" << std::hex << exCode;
        log_debug(ex.str());
        return false;
    }
    {
        std::ostringstream ss;
        ss << "audioClient_->Start() returned HRESULT = 0x" << std::hex << hrStart;
        log_debug(ss.str());
    }
    CHECK_HR(hrStart, "IAudioClient::Start failed");

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
    if (device_)        { device_->Release();        device_        = nullptr; }
    if (enumerator_)    { enumerator_->Release();    enumerator_    = nullptr; }
    CoUninitialize();

    log_debug("Capture thread exited successfully");
    std::cout << "[WasapiCapture] Capture thread exiting." << std::endl;
}

} // namespace audiostream

#endif // _WIN32
