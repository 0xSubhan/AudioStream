#pragma once

#include <opus/opus.h>
#include <cstdint>

namespace audiostream {

class OpusDecoder {
public:
    /**
     * Constructs an Opus decoder wrapper.
     * @param sampleRate Target playback sample rate (default 48000).
     * @param channels Channel count (1 for mono, 2 for stereo).
     */
    OpusDecoder(int sampleRate = 48000, int channels = 2);
    ~OpusDecoder();

    // Prevent copy/assignment to avoid double-freeing the internal OpusDecoder state
    OpusDecoder(const OpusDecoder&) = delete;
    OpusDecoder& operator=(const OpusDecoder&) = delete;

    /**
     * Initializes the Opus decoder state.
     * @return true if initialization succeeded, false otherwise.
     */
    bool init();

    /**
     * Decodes a compressed Opus packet back into float32 PCM samples.
     * @param encodedData Pointer to the compressed Opus packet buffer.
     * @param len Size of the compressed packet in bytes.
     * @param outPcm Destination buffer for decoded interleaved float32 PCM samples.
     * @param frameSize Output frame size (number of samples per channel). Must match the frame size encoded.
     * @return Number of decoded samples per channel, or a negative Opus error code.
     */
    int decode(const uint8_t* encodedData, int len, float* outPcm, int frameSize);

private:
    int sampleRate_;
    int channels_;
    ::OpusDecoder* decoder_;
};

} // namespace audiostream
