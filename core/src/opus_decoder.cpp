#include "../include/opus_decoder.h"
#include <iostream>

namespace audiostream {

OpusDecoder::OpusDecoder(int sampleRate, int channels)
    : sampleRate_(sampleRate),
      channels_(channels),
      decoder_(nullptr) {}

OpusDecoder::~OpusDecoder() {
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
}

bool OpusDecoder::init() {
    int error = OPUS_OK;
    decoder_ = opus_decoder_create(sampleRate_, channels_, &error);
    if (error != OPUS_OK || !decoder_) {
        std::cerr << "[OpusDecoder] Failed to create decoder: " << opus_strerror(error) << std::endl;
        return false;
    }
    return true;
}

int OpusDecoder::decode(const uint8_t* encodedData, int len, float* outPcm, int frameSize) {
    if (!decoder_) {
        std::cerr << "[OpusDecoder] Decoder is not initialized!" << std::endl;
        return OPUS_UNIMPLEMENTED;
    }

    // opus_decode_float decodes the compressed Opus buffer to float32 PCM samples.
    // The last argument is the decode_fec flag (0 for regular decode, 1 for forward error correction).
    int samplesDecoded = opus_decode_float(decoder_, encodedData, len, outPcm, frameSize, 0);
    if (samplesDecoded < 0) {
        std::cerr << "[OpusDecoder] Decoding failed: " << opus_strerror(samplesDecoded) << std::endl;
    }
    return samplesDecoded;
}

} // namespace audiostream
