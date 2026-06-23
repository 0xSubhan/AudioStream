#pragma once

#include <opus/opus.h>
#include <cstdint>

namespace audiostream {

class OpusEncoder {
public:
    /**
     * Constructs an Opus encoder wrapper.
     * @param sampleRate Target sample rate (default 48000). Only 8000, 12000, 16000, 24000, 48000 are supported by Opus.
     * @param channels Channel count (1 for mono, 2 for stereo).
     * @param bitrate Target bitrate in bits per second (default 96000 for high-quality stereo).
     */
    OpusEncoder(int sampleRate = 48000, int channels = 2, int bitrate = 96000);
    ~OpusEncoder();

    // Prevent copy/assignment to avoid double-freeing the internal OpusEncoder state
    OpusEncoder(const OpusEncoder&) = delete;
    OpusEncoder& operator=(const OpusEncoder&) = delete;

    /**
     * Initializes the Opus encoder state.
     * @return true if initialization succeeded, false otherwise.
     */
    bool init();

    /**
     * Encodes a block of float32 PCM samples into a compressed Opus packet.
     * @param pcm Pointer to interleaved float32 PCM samples.
     * @param frameSize Number of samples per channel in the input frame (e.g. 960 for 20ms @ 48kHz).
     * @param outBuffer Destination byte buffer for the compressed Opus packet.
     * @param maxBytes Maximum capacity of the destination buffer.
     * @return Size of the compressed packet in bytes, or a negative Opus error code.
     */
    int encode(const float* pcm, int frameSize, uint8_t* outBuffer, int maxBytes);

private:
    int sampleRate_;
    int channels_;
    int bitrate_;
    ::OpusEncoder* encoder_;
};

} // namespace audiostream
