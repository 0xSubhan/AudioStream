#pragma once

#include <map>
#include <vector>
#include <mutex>
#include <cstdint>
#include <algorithm>

namespace audiostream {

struct AudioPacket {
    uint16_t seq;
    uint32_t timestamp;
    std::vector<uint8_t> data;
};

class JitterBuffer {
public:
    explicit JitterBuffer(size_t targetDelay = 3)
        : targetDelay_(targetDelay),
          initialized_(false),
          nextSeq_(0) {}

    /**
     * Enqueues an incoming RTP packet sorted by its sequence number.
     */
    void push(uint16_t seq, uint32_t timestamp, const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // If we get an old packet that has already been played or skipped, drop it
        if (initialized_ && seqCompare(seq, nextSeq_) < 0) {
            return;
        }

        packets_[seq] = {seq, timestamp, std::vector<uint8_t>(data, data + len)};
        
        // Cap buffer size to prevent runaway memory if network is completely broken
        if (packets_.size() > 100) {
            packets_.erase(packets_.begin());
        }
    }

    /**
     * Retrieves the next packet in the sequence.
     * Returns true if a packet was processed (either found or lost/concealed),
     * and false if the buffer is still pre-buffering or empty.
     */
    bool pop(uint8_t* outBuf, size_t maxLen, size_t& outLen, bool& isLost) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (packets_.empty()) {
            isLost = true;
            outLen = 0;
            return false; // Silence/buffer starvation
        }

        if (!initialized_) {
            // Buffer at least 'targetDelay' packets before starting playback to absorb jitter
            if (packets_.size() < targetDelay_) {
                isLost = true;
                outLen = 0;
                return false;
            }
            // Play from the oldest sequence number currently in buffer
            nextSeq_ = packets_.begin()->first;
            initialized_ = true;
        }

        auto it = packets_.find(nextSeq_);
        if (it != packets_.end()) {
            outLen = it->second.data.size();
            if (outLen <= maxLen) {
                std::copy(it->second.data.begin(), it->second.data.end(), outBuf);
            }
            packets_.erase(it);
            nextSeq_++;
            isLost = false;
            return true;
        } else {
            // Packet loss detected (expected sequence number is missing)
            isLost = true;
            outLen = 0;
            nextSeq_++; // Progress expected sequence number to trigger PLC
            return true;
        }
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        packets_.clear();
        initialized_ = false;
        nextSeq_ = 0;
    }

private:
    // Compares two 16-bit sequence numbers handling wrap-around
    int seqCompare(uint16_t seq1, uint16_t seq2) {
        int diff = static_cast<int>(seq1) - static_cast<int>(seq2);
        if (diff > 32767) diff -= 65536;
        else if (diff < -32768) diff += 65536;
        return diff;
    }

    size_t targetDelay_;
    bool initialized_;
    uint16_t nextSeq_;
    std::map<uint16_t, AudioPacket> packets_;
    std::mutex mutex_;
};

} // namespace audiostream
