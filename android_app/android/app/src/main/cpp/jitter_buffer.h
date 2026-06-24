#pragma once

#include <map>
#include <vector>
#include <mutex>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace audiostream {

struct AudioPacket {
    uint16_t seq;
    uint32_t timestamp;
    std::vector<uint8_t> data;
};

class JitterBuffer {
public:
    explicit JitterBuffer(size_t defaultTargetDelay = 3)
        : defaultTargetDelay_(defaultTargetDelay),
          targetDelay_(defaultTargetDelay),
          initialized_(false),
          nextSeq_(0),
          jitter_(0.0),
          lastArrivalMs_(0.0),
          lastSenderTimestampMs_(0.0) {}

    /**
     * Enqueues an incoming RTP packet sorted by its sequence number.
     * Computes the arrival jitter to dynamically adjust the target playback buffer delay.
     */
    void push(uint16_t seq, uint32_t timestamp, const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // If we get an old packet that has already been played or skipped, drop it
        if (initialized_ && seqCompare(seq, nextSeq_) < 0) {
            return;
        }

        packets_[seq] = {seq, timestamp, std::vector<uint8_t>(data, data + len)};
        
        // Dynamic jitter estimation according to RFC 3550
        auto now = std::chrono::steady_clock::now();
        double nowMs = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() / 1000.0;
        
        // RTP timestamp is in samples. With 48kHz, timestamp / 48 gives milliseconds.
        double timestampMs = static_cast<double>(timestamp) / 48.0;

        if (lastArrivalMs_ > 0.0 && lastSenderTimestampMs_ > 0.0) {
            double transit = nowMs - timestampMs;
            double prevTransit = lastArrivalMs_ - lastSenderTimestampMs_;
            double d = std::abs(transit - prevTransit);
            
            // Exponential moving average: J = J + (|D(i,j)| - J) / 16
            jitter_ = jitter_ + (d - jitter_) / 16.0;

            // Packet size is 20ms. We set target delay in packets based on jitter:
            // targetDelay = ceil(jitter / 20) + safetyMargin. We use a safety margin of 2 packets.
            double calculatedDelayPackets = (jitter_ / 20.0) + 2.0;
            size_t newTargetDelay = static_cast<size_t>(std::ceil(calculatedDelayPackets));
            
            // Clamp target delay to reasonable boundaries [2 packets (40ms), 8 packets (160ms)]
            targetDelay_ = std::max(static_cast<size_t>(2), std::min(newTargetDelay, static_cast<size_t>(8)));
        }

        lastArrivalMs_ = nowMs;
        lastSenderTimestampMs_ = timestampMs;

        // Cap absolute buffer size to prevent runaway memory leak
        if (packets_.size() > 100) {
            packets_.erase(packets_.begin());
        }
    }

    /**
     * Retrieves the next packet in the sequence.
     * Drops older accumulated packets to recover latency if the queue is backing up.
     * Returns true if a packet was processed (either found or lost/concealed),
     * and false if the buffer is still pre-buffering or empty.
     */
    bool pop(uint8_t* outBuf, size_t maxLen, size_t& outLen, bool& isLost) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (packets_.empty()) {
            isLost = true;
            outLen = 0;
            initialized_ = false; // Reset initialization on underflow to trigger pre-buffering
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

        // --- Latency Catch-up / Drift Correction ---
        // If the queue size exceeds our maximum threshold, we have accumulated delay/lag.
        // We use a wider threshold std::max(targetDelay_ + 8, (size_t)12) to absorb packet bursts.
        size_t maxAllowedBuffered = std::max(targetDelay_ + 8, (size_t)12);
        if (packets_.size() > maxAllowedBuffered) {
            uint16_t newestSeqInBuf = packets_.rbegin()->first;
            // Target having targetDelay_ packets left in the buffer after catching up
            uint16_t catchUpSeq = newestSeqInBuf - targetDelay_ + 1;
            
            if (seqCompare(catchUpSeq, nextSeq_) > 0) {
                // Erase all packets in the map that are older than our catch-up sequence
                auto it = packets_.begin();
                while (it != packets_.end() && seqCompare(it->first, catchUpSeq) < 0) {
                    it = packets_.erase(it);
                }
                nextSeq_ = catchUpSeq;
            }
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
        jitter_ = 0.0;
        lastArrivalMs_ = 0.0;
        lastSenderTimestampMs_ = 0.0;
        targetDelay_ = defaultTargetDelay_;
    }

    double getJitterMs() const {
        return jitter_;
    }

    size_t getTargetDelayPackets() const {
        return targetDelay_;
    }

private:
    // Compares two 16-bit sequence numbers handling wrap-around
    int seqCompare(uint16_t seq1, uint16_t seq2) {
        int diff = static_cast<int>(seq1) - static_cast<int>(seq2);
        if (diff > 32767) diff -= 65536;
        else if (diff < -32768) diff += 65536;
        return diff;
    }

    size_t defaultTargetDelay_;
    size_t targetDelay_;
    bool initialized_;
    uint16_t nextSeq_;
    std::map<uint16_t, AudioPacket> packets_;
    std::mutex mutex_;
    
    double jitter_;
    double lastArrivalMs_;
    double lastSenderTimestampMs_;
};

} // namespace audiostream
