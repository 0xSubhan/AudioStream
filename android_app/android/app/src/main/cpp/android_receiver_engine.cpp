#include <jni.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <android/log.h>
#include <aaudio/AAudio.h>
#include <opus.h>
#include "jitter_buffer.h"

#define LOG_TAG "AudioStreamReceiver"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace audiostream {

class ReceiverEngine {
public:
    ReceiverEngine()
        : running_(false),
          socketFd_(-1),
          playStream_(nullptr),
          opusDecoder_(nullptr),
          packetCount_(0),
          underrunCount_(0) {}

    ~ReceiverEngine() {
        stop();
    }

    bool start(int port) {
        if (running_) {
            stop();
        }

        LOGI("Starting ReceiverEngine on port %d...", port);
        running_ = true;
        packetCount_ = 0;
        underrunCount_ = 0;
        jitterBuffer_.reset();
        {
            std::lock_guard<std::mutex> lock(pcmMutex_);
            pcmBuffer_.clear();
        }

        // 1. Initialize Opus Decoder
        int error = 0;
        opusDecoder_ = opus_decoder_create(48000, 2, &error);
        if (error != OPUS_OK || !opusDecoder_) {
            LOGE("Failed to create Opus Decoder: %s", opus_strerror(error));
            running_ = false;
            return false;
        }

        // 2. Open UDP Socket
        socketFd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketFd_ < 0) {
            LOGE("Failed to create UDP socket: %s", strerror(errno));
            cleanupOpus();
            running_ = false;
            return false;
        }

        struct sockaddr_in addr{};
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (bind(socketFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            LOGE("Failed to bind UDP socket: %s", strerror(errno));
            close(socketFd_);
            socketFd_ = -1;
            cleanupOpus();
            running_ = false;
            return false;
        }

        // 3. Initialize AAudio Playback
        AAudioStreamBuilder* builder = nullptr;
        aaudio_result_t result = AAudio_createStreamBuilder(&builder);
        if (result != AAUDIO_OK) {
            LOGE("Failed to create AAudio Stream Builder: %d", result);
            cleanupSocket();
            cleanupOpus();
            running_ = false;
            return false;
        }

        AAudioStreamBuilder_setSampleRate(builder, 48000);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
        AAudioStreamBuilder_setDataCallback(builder, dataCallback, this);

        result = AAudioStreamBuilder_openStream(builder, &playStream_);
        AAudioStreamBuilder_delete(builder);

        if (result != AAUDIO_OK || !playStream_) {
            LOGE("Failed to open AAudio Playback Stream: %d", result);
            cleanupSocket();
            cleanupOpus();
            running_ = false;
            return false;
        }

        result = AAudioStream_requestStart(playStream_);
        if (result != AAUDIO_OK) {
            LOGE("Failed to start AAudio stream: %d", result);
            AAudioStream_close(playStream_);
            playStream_ = nullptr;
            cleanupSocket();
            cleanupOpus();
            running_ = false;
            return false;
        }

        // 4. Start Background UDP Receiver Thread
        receiverThread_ = std::thread(&ReceiverEngine::receiverLoop, this, port);
        LOGI("ReceiverEngine successfully started.");
        return true;
    }

    void stop() {
        if (!running_) return;
        LOGI("Stopping ReceiverEngine...");
        running_ = false;

        // Stop AAudio
        if (playStream_) {
            AAudioStream_requestStop(playStream_);
            AAudioStream_close(playStream_);
            playStream_ = nullptr;
        }

        // Close Socket (forces recvfrom in receiver thread to unblock and return)
        cleanupSocket();

        // Join receiver thread
        if (receiverThread_.joinable()) {
            receiverThread_.join();
        }

        cleanupOpus();
        {
            std::lock_guard<std::mutex> lock(pcmMutex_);
            pcmBuffer_.clear();
        }
        LOGI("ReceiverEngine stopped.");
    }

    int getPacketCount() const { return packetCount_.load(); }
    int getUnderrunCount() const { return underrunCount_.load(); }
    double getJitterMs() const { return jitterBuffer_.getJitterMs(); }
    int getTargetDelayPackets() const { return static_cast<int>(jitterBuffer_.getTargetDelayPackets()); }

private:
    static aaudio_data_callback_result_t dataCallback(
            AAudioStream* stream, void* userData, void* audioData, int32_t numFrames) {
        auto* engine = reinterpret_cast<ReceiverEngine*>(userData);
        auto* outFloat = reinterpret_cast<float*>(audioData);

        std::lock_guard<std::mutex> lock(engine->pcmMutex_);

        const int channels = 2;
        int requiredSamples = numFrames * channels;

        // Decode more packets if we don't have enough samples in our buffer
        while (engine->pcmBuffer_.size() < requiredSamples && engine->running_) {
            uint8_t packetData[1024];
            size_t packetLen = 0;
            bool isLost = false;

            if (engine->jitterBuffer_.pop(packetData, sizeof(packetData), packetLen, isLost)) {
                // Expected Opus frame size (960 frames = 20ms at 48kHz)
                const int opusFrameSize = 960;
                float decodedPcm[opusFrameSize * channels];

                int decodedFrames = 0;
                if (isLost) {
                    decodedFrames = opus_decode_float(
                            engine->opusDecoder_, nullptr, 0, decodedPcm, opusFrameSize, 0);
                } else {
                    decodedFrames = opus_decode_float(
                            engine->opusDecoder_, packetData, packetLen, decodedPcm, opusFrameSize, 0);
                }

                if (decodedFrames > 0) {
                    engine->pcmBuffer_.insert(engine->pcmBuffer_.end(), decodedPcm, decodedPcm + decodedFrames * channels);
                } else {
                    LOGE("Opus decode/PLC failed: %d", decodedFrames);
                    break;
                }
            } else {
                // Jitter buffer underflow/starvation
                break;
            }
        }

        // Copy samples from PCM buffer to output float buffer
        if (engine->pcmBuffer_.size() >= requiredSamples) {
            std::copy(engine->pcmBuffer_.begin(), engine->pcmBuffer_.begin() + requiredSamples, outFloat);
            engine->pcmBuffer_.erase(engine->pcmBuffer_.begin(), engine->pcmBuffer_.begin() + requiredSamples);
        } else {
            // Buffer starvation: copy whatever we have, fill the remainder with silence
            int availableSamples = engine->pcmBuffer_.size();
            if (availableSamples > 0) {
                std::copy(engine->pcmBuffer_.begin(), engine->pcmBuffer_.end(), outFloat);
                engine->pcmBuffer_.clear();
            }
            std::memset(outFloat + availableSamples, 0, (requiredSamples - availableSamples) * sizeof(float));
            engine->underrunCount_++;
        }

        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    void receiverLoop(int port) {
        LOGI("UDP Receiver thread active.");
        std::vector<uint8_t> recvBuf(2048);

        while (running_) {
            struct sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
            
            ssize_t bytesRecv = recvfrom(socketFd_, recvBuf.data(), recvBuf.size(), 0,
                                         reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
            
            if (bytesRecv < 0) {
                if (running_) {
                    LOGE("recvfrom() failed: %s", strerror(errno));
                }
                break;
            }

            // Parse RTP header (must be at least 12 bytes)
            if (bytesRecv < 12) {
                continue;
            }

            uint16_t seq = (recvBuf[2] << 8) | recvBuf[3];
            uint32_t timestamp = (recvBuf[4] << 24) | (recvBuf[5] << 16) | (recvBuf[6] << 8) | recvBuf[7];
            
            // Extract Opus payload immediately following the 12-byte RTP header
            const uint8_t* opusPayload = recvBuf.data() + 12;
            size_t opusPayloadLen = bytesRecv - 12;

            jitterBuffer_.push(seq, timestamp, opusPayload, opusPayloadLen);
            packetCount_++;
        }
        LOGI("UDP Receiver thread exiting.");
    }

    void cleanupSocket() {
        if (socketFd_ >= 0) {
            shutdown(socketFd_, SHUT_RDWR);
            close(socketFd_);
            socketFd_ = -1;
        }
    }

    void cleanupOpus() {
        if (opusDecoder_) {
            opus_decoder_destroy(opusDecoder_);
            opusDecoder_ = nullptr;
        }
    }

    std::atomic<bool> running_;
    int socketFd_;
    AAudioStream* playStream_;
    OpusDecoder* opusDecoder_;
    JitterBuffer jitterBuffer_;
    std::thread receiverThread_;
    std::atomic<int> packetCount_;
    std::atomic<int> underrunCount_;
    std::vector<float> pcmBuffer_;
    std::mutex pcmMutex_;
};

static ReceiverEngine g_receiverEngine;

} // namespace audiostream

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_audiostream_audiostream_1app_ReceiverEngine_nativeStart(JNIEnv* env, jobject thiz, jint port) {
    return audiostream::g_receiverEngine.start(port) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_audiostream_audiostream_1app_ReceiverEngine_nativeStop(JNIEnv* env, jobject thiz) {
    audiostream::g_receiverEngine.stop();
}

JNIEXPORT jint JNICALL
Java_com_audiostream_audiostream_1app_ReceiverEngine_nativeGetPacketCount(JNIEnv* env, jobject thiz) {
    return audiostream::g_receiverEngine.getPacketCount();
}

JNIEXPORT jint JNICALL
Java_com_audiostream_audiostream_1app_ReceiverEngine_nativeGetUnderrunCount(JNIEnv* env, jobject thiz) {
    return audiostream::g_receiverEngine.getUnderrunCount();
}

JNIEXPORT jdouble JNICALL
Java_com_audiostream_audiostream_1app_ReceiverEngine_nativeGetJitterMs(JNIEnv* env, jobject thiz) {
    return audiostream::g_receiverEngine.getJitterMs();
}

JNIEXPORT jint JNICALL
Java_com_audiostream_audiostream_1app_ReceiverEngine_nativeGetTargetDelayPackets(JNIEnv* env, jobject thiz) {
    return audiostream::g_receiverEngine.getTargetDelayPackets();
}

}
