#include "../core/include/pulse_capture.h"
#include "../core/include/opus_encoder.h"
#include "../core/include/udp_sender.h"
#include "../core/include/rtp_packet.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <signal.h>
#include <cstdlib>

volatile bool g_running = true;

void signalHandler(int signum) {
    std::cout << "\n[Test] Interrupted. Stopping stream..." << std::endl;
    g_running = false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);

    std::string ip = "127.0.0.1";
    int port = 8554;

    if (argc >= 2) {
        ip = argv[1];
    }
    if (argc >= 3) {
        port = std::atoi(argv[2]);
    }

    std::cout << "========================================" << std::endl;
    std::cout << "       AudioStream RTP/UDP Sender       " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "[Test] Streaming target: " << ip << ":" << port << std::endl;

    // 1. Initialize Loopback Audio Capture (48kHz, Stereo)
    audiostream::PulseAudioCapture capture(48000, 2);
    if (!capture.start()) {
        std::cerr << "[ERROR] Failed to start audio capture." << std::endl;
        return 1;
    }

    // 2. Initialize Opus Encoder (96kbps VBR stereo)
    audiostream::OpusEncoder encoder(48000, 2, 96000);
    if (!encoder.init()) {
        std::cerr << "[ERROR] Failed to initialize Opus Encoder." << std::endl;
        return 1;
    }

    // 3. Initialize UDP Socket Sender
    audiostream::UdpSender sender;
    if (!sender.init(ip, port)) {
        std::cerr << "[ERROR] Failed to initialize UDP socket." << std::endl;
        return 1;
    }

    std::cout << "[Test] Capture, Encoder, and Socket initialized successfully." << std::endl;
    std::cout << "[Test] Streaming started. Play some audio now!" << std::endl;
    std::cout << "[Test] Run: ffplay -protocol_whitelist file,udp,rtp tests/stream.sdp" << std::endl;
    std::cout << "[Test] Press Ctrl+C to stop." << std::endl;

    const int chunkFrames = 960; // 20ms of audio
    const int channels = capture.getChannels();
    
    std::vector<float> pcmBuffer(chunkFrames * channels);
    // Allocate space for 12-byte header + max possible compressed frame size
    std::vector<uint8_t> packetBuffer(sizeof(audiostream::RtpHeader) + chunkFrames * channels * sizeof(float));

    uint16_t seqNum = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0x5354524D; // SSRC ID "STRM"

    int packetCount = 0;

    while (g_running) {
        int readResult = capture.read(pcmBuffer.data(), chunkFrames);
        if (readResult < 0) {
            std::cerr << "[ERROR] Capture read failed." << std::endl;
            break;
        }

        // Pointer inside buffer immediately following the 12-byte header
        uint8_t* opusPayloadPtr = packetBuffer.data() + sizeof(audiostream::RtpHeader);
        int maxPayloadSize = packetBuffer.size() - sizeof(audiostream::RtpHeader);

        // 1. Encode floats to Opus directly into the packet buffer payload section
        int encodedBytes = encoder.encode(pcmBuffer.data(), chunkFrames, opusPayloadPtr, maxPayloadSize);
        if (encodedBytes < 0) {
            std::cerr << "[ERROR] Encoding failed." << std::endl;
            break;
        }

        // 2. Prep standard RTP Header at the front of the packet buffer
        auto* rtpHeaderPtr = reinterpret_cast<audiostream::RtpHeader*>(packetBuffer.data());
        audiostream::packRtpHeader(*rtpHeaderPtr, seqNum, timestamp, ssrc, 96);

        // 3. Send full packet (Header + Payload) over UDP
        int totalPacketSize = sizeof(audiostream::RtpHeader) + encodedBytes;
        int sentBytes = sender.send(packetBuffer.data(), totalPacketSize);
        if (sentBytes < 0) {
            // We ignore transmission errors (like ECONNREFUSED) so the sender doesn't crash
            // if the receiver hasn't started listening yet.
        }

        // 4. Update sequence counter and timestamp step size
        seqNum++;
        timestamp += chunkFrames; // Opus timestamp increments by number of samples per channel

        packetCount++;
        if (packetCount % 50 == 0) { // roughly every 1 second of audio (50 packets * 20ms)
            std::cout << "[Live] Packets sent: " << packetCount 
                      << " | Size: " << totalPacketSize << " bytes" 
                      << " | Capture Latency: " << capture.getLatencyMs() << " ms" << std::endl;
        }
    }

    capture.stop();
    sender.closeSocket();
    std::cout << "[Test] Streaming stopped. Total packets transmitted: " << packetCount << std::endl;
    return 0;
}
