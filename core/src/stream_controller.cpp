#include "../include/stream_controller.h"

// Platform-specific audio capture backend
#ifdef _WIN32
  #include "../include/wasapi_capture.h"
  #include <windows.h>
  #include <fstream>
  #include <sstream>
#else
  #include "../include/pulse_capture.h"
  #include <fstream>
  #include <sstream>
#endif

#include "../include/opus_encoder.h"
#include "../include/udp_sender.h"
#include "../include/rtp_packet.h"
#include <iostream>
#include <vector>

inline void log_debug(const std::string& msg) {
#ifdef _WIN32
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
        std::string exePath(path);
        std::string logPath = exePath.substr(0, exePath.find_last_of("\\/")) + "\\AudioStream_debug.log";
        std::ofstream file(logPath, std::ios::app);
        if (file.is_open()) {
            file << "[C++] " << msg << std::endl;
        }
    }
#else
    std::ofstream file("AudioStream_debug.log", std::ios::app);
    if (file.is_open()) {
        file << "[C++] " << msg << std::endl;
    }
#endif
}

namespace audiostream {

StreamController::StreamController()
    : running_(false),
      latencyMs_(0.0f),
      packetCount_(0) {}

StreamController::~StreamController() {
    stop();
}

bool StreamController::start(const std::string& ip, int port) {
    log_debug("StreamController::start() called");
    if (running_) {
        log_debug("StreamController already running, stopping first");
        stop();
    }

    running_ = true;
    packetCount_ = 0;
    latencyMs_ = 0.0f;

    // Start background streaming thread
    try {
        log_debug("Spawning stream thread...");
        streamThread_ = std::thread(&StreamController::streamLoop, this, ip, port);
        log_debug("Stream thread spawned successfully");
    } catch (const std::exception& e) {
        std::ostringstream err;
        err << "Failed to spawn thread: " << e.what();
        log_debug(err.str());
        std::cerr << "[StreamController] " << err.str() << std::endl;
        running_ = false;
        return false;
    }

    return true;
}

void StreamController::stop() {
    log_debug("StreamController::stop() called");
    if (running_) {
        running_ = false;
        if (streamThread_.joinable()) {
            log_debug("Joining stream thread...");
            streamThread_.join();
            log_debug("Stream thread joined");
        }
    }
    log_debug("StreamController stopped completely");
}

bool StreamController::isStreaming() const {
    return running_;
}

float StreamController::getLatencyMs() const {
    return latencyMs_.load();
}

int StreamController::getPacketCount() const {
    return packetCount_.load();
}

void StreamController::streamLoop(const std::string ip, int port) {
    log_debug("streamLoop thread started");
    std::cout << "[StreamController] Streaming thread started." << std::endl;

    // 1. Initialize platform-specific audio capture (48kHz, Stereo)
    log_debug("Initializing audio capture...");
#ifdef _WIN32
    WasapiCapture capture(48000, 2);
#else
    PulseAudioCapture capture(48000, 2);
#endif
    if (!capture.start()) {
        log_debug("Failed to start audio capture");
        std::cerr << "[StreamController] Failed to start capture." << std::endl;
        running_ = false;
        return;
    }
    log_debug("Audio capture started successfully");

    // 2. Initialize Opus Encoder (96kbps VBR Stereo)
    log_debug("Initializing Opus encoder...");
    OpusEncoder encoder(48000, 2, 96000);
    if (!encoder.init()) {
        log_debug("Failed to initialize Opus encoder");
        std::cerr << "[StreamController] Failed to initialize Opus encoder." << std::endl;
        capture.stop();
        running_ = false;
        return;
    }
    log_debug("Opus encoder initialized successfully");

    // 3. Initialize UDP Socket Sender
    log_debug("Initializing UDP sender...");
    UdpSender sender;
    if (!sender.init(ip, port)) {
        log_debug("Failed to initialize UDP sender");
        std::cerr << "[StreamController] Failed to initialize UDP sender." << std::endl;
        capture.stop();
        running_ = false;
        return;
    }
    log_debug("UDP sender initialized successfully");

    const int chunkFrames = 960; // 20ms of audio
    const int channels = capture.getChannels();

    std::vector<float> pcmBuffer(chunkFrames * channels);
    // Allocate space for 12-byte RTP header + max possible payload
    std::vector<uint8_t> packetBuffer(sizeof(RtpHeader) + chunkFrames * channels * sizeof(float));

    uint16_t seqNum = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0x5354524D; // "STRM" SSRC ID

    log_debug("Engine components initialized. Starting loop...");
    std::cout << "[StreamController] Engine components initialized. Loop running..." << std::endl;

    while (running_) {
        int readResult = capture.read(pcmBuffer.data(), chunkFrames);
        if (readResult < 0) {
            std::cerr << "[StreamController] Capture read error." << std::endl;
            break;
        }

        // Pointer inside packetBuffer immediately following the 12-byte header
        uint8_t* opusPayloadPtr = packetBuffer.data() + sizeof(RtpHeader);
        int maxPayloadSize = packetBuffer.size() - sizeof(RtpHeader);

        // 1. Encode floats to Opus directly into the packet buffer payload section
        int encodedBytes = encoder.encode(pcmBuffer.data(), chunkFrames, opusPayloadPtr, maxPayloadSize);
        if (encodedBytes < 0) {
            std::cerr << "[StreamController] Opus encoding failed." << std::endl;
            break;
        }

        // 2. Prep standard RTP Header at the front of the packet buffer
        auto* rtpHeaderPtr = reinterpret_cast<RtpHeader*>(packetBuffer.data());
        packRtpHeader(*rtpHeaderPtr, seqNum, timestamp, ssrc, 96);

        // 3. Send full packet (Header + Payload) over UDP
        int totalPacketSize = sizeof(RtpHeader) + encodedBytes;
        int sentBytes = sender.send(packetBuffer.data(), totalPacketSize);
        if (sentBytes < 0) {
            // Ignore connection errors (like ECONNREFUSED) so the stream continues
            // running if the receiver is temporarily offline.
        }

        // 4. Update metrics
        seqNum++;
        timestamp += chunkFrames;
        packetCount_++;
        latencyMs_ = capture.getLatencyMs();
    }

    log_debug("streamLoop exited loop. Stopping engine components...");
    std::cout << "[StreamController] Stopping engine components..." << std::endl;
    capture.stop();
    sender.closeSocket();
    log_debug("streamLoop thread exiting cleanly");
    std::cout << "[StreamController] Streaming thread exiting." << std::endl;
}

} // namespace audiostream
