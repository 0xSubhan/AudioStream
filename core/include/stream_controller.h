#pragma once

#include <string>
#include <thread>
#include <atomic>

namespace audiostream {

class StreamController {
public:
    StreamController();
    ~StreamController();

    // Prevent copy/assignment to avoid thread resource management issues
    StreamController(const StreamController&) = delete;
    StreamController& operator=(const StreamController&) = delete;

    /**
     * Starts the background streaming thread capturing system audio and transmitting it over UDP.
     * @param ip Destination IP address.
     * @param port Destination UDP port.
     * @return true if initialization succeeded and thread started, false otherwise.
     */
    bool start(const std::string& ip, int port);

    /**
     * Stops the background streaming thread and cleans up resources.
     */
    void stop();

    /**
     * Checks if the streaming thread is currently active.
     */
    bool isStreaming() const;

    /**
     * Returns the last recorded latency of the hardware capture driver in milliseconds.
     */
    float getLatencyMs() const;

    /**
     * Returns the total count of RTP packets sent during this stream.
     */
    int getPacketCount() const;

private:
    void streamLoop(const std::string ip, int port);

    std::thread streamThread_;
    std::atomic<bool> running_;
    std::atomic<float> latencyMs_;
    std::atomic<int> packetCount_;
};

} // namespace audiostream
