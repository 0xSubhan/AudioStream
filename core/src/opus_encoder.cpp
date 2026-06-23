#include "../include/opus_encoder.h"
#include <iostream>

namespace audiostream {

OpusEncoder::OpusEncoder(int sampleRate, int channels, int bitrate)
    : sampleRate_(sampleRate),
      channels_(channels),
      bitrate_(bitrate),
      encoder_(nullptr) {}

OpusEncoder::~OpusEncoder() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
}

bool OpusEncoder::init() {
    int error = OPUS_OK;

    // Use OPUS_APPLICATION_AUDIO for general high-fidelity music and speech.
    encoder_ = opus_encoder_create(sampleRate_, channels_, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK || !encoder_) {
        std::cerr << "[OpusEncoder] Failed to create encoder: " << opus_strerror(error) << std::endl;
        return false;
    }

    // Configure encoder parameters
    // 1. Bitrate
    if (opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate_)) != OPUS_OK) {
        std::cerr << "[OpusEncoder] Failed to set bitrate to " << bitrate_ << std::endl;
        return false;
    }

    // 2. VBR (Variable Bitrate) - enabled for optimal compression and quality
    if (opus_encoder_ctl(encoder_, OPUS_SET_VBR(1)) != OPUS_OK) {
        std::cerr << "[OpusEncoder] Failed to enable VBR" << std::endl;
        return false;
    }

    // 3. Complexity (0 to 10, where 10 is highest quality/CPU compression. PC handles 10 easily).
    if (opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(10)) != OPUS_OK) {
        std::cerr << "[OpusEncoder] Failed to set complexity to 10" << std::endl;
        return false;
    }

    // 4. Signal Type (Auto detection)
    if (opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_AUTO)) != OPUS_OK) {
        std::cerr << "[OpusEncoder] Failed to set signal type" << std::endl;
        return false;
    }

    return true;
}

int OpusEncoder::encode(const float* pcm, int frameSize, uint8_t* outBuffer, int maxBytes) {
    if (!encoder_) {
        std::cerr << "[OpusEncoder] Encoder is not initialized!" << std::endl;
        return OPUS_UNIMPLEMENTED;
    }

    int bytesEncoded = opus_encode_float(encoder_, pcm, frameSize, outBuffer, maxBytes);
    if (bytesEncoded < 0) {
        std::cerr << "[OpusEncoder] Encoding failed: " << opus_strerror(bytesEncoded) << std::endl;
    }
    return bytesEncoded;
}

} // namespace audiostream
