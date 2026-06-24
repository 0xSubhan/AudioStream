#include "../include/stream_controller.h"
#include "../include/pulse_capture.h"
#include "../include/opus_encoder.h"
#include "../include/udp_sender.h"
#include "../include/rtp_packet.h"
#include <iostream>
#include <vector>

namespace audiostream {

StreamController::StreamController()
    : running_(false),
      latencyMs_(0.0f),
      packetCount_(0) {}

StreamController::~StreamController() {
    stop();
}

bool StreamController::start(const std::string& ip, int port) {
    if (running_) {
        stop();
    }

    running_ = true;
    packetCount_ = 0;
    latencyMs_ = 0.0f;

    // Start background streaming thread
    try {
        streamThread_ = std::thread(&StreamController::streamLoop, this, ip, port);
    } catch (const std::exception& e) {
        std::cerr << "[StreamController] Failed to spawn thread: " << e.what() << std::endl;
        running_ = false;
        return false;
    }

    return true;
}

void StreamController::stop() {
    if (running_) {
        running_ = false;
        if (streamThread_.joinable()) {
            streamThread_.join();
        }
    }
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
    std::cout << "[StreamController] Streaming thread started." << std::endl;

    // 1. Initialize PulseAudio capture (48kHz, Stereo)
    PulseAudioCapture capture(48000, 2);
    if (!capture.start()) {
        std::cerr << "[StreamController] Failed to start capture." << std::endl;
        running_ = false;
        return;
    }

    // 2. Initialize Opus Encoder (96kbps VBR Stereo)
    OpusEncoder encoder(48000, 2, 96000);
    if (!encoder.init()) {
        std::cerr << "[StreamController] Failed to initialize Opus encoder." << std::endl;
        capture.stop();
        running_ = false;
        return;
    }

    // 3. Initialize UDP Socket Sender
    UdpSender sender;
    if (!sender.init(ip, port)) {
        std::cerr << "[StreamController] Failed to initialize UDP sender." << std::endl;
        capture.stop();
        running_ = false;
        return;
    }

    const int chunkFrames = 960; // 20ms of audio
    const int channels = capture.getChannels();

    std::vector<float> pcmBuffer(chunkFrames * channels);
    // Allocate space for 12-byte RTP header + max possible payload
    std::vector<uint8_t> packetBuffer(sizeof(RtpHeader) + chunkFrames * channels * sizeof(float));

    uint16_t seqNum = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0x5354524D; // "STRM" SSRC ID

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

    std::cout << "[StreamController] Stopping engine components..." << std::endl;
    capture.stop();
    sender.closeSocket();
    std::cout << "[StreamController] Streaming thread exiting." << std::endl;
}

} // namespace audiostream
