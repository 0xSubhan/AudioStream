#pragma once

namespace audiostream {

class AudioCapture {
public:
    virtual ~AudioCapture() = default;

    /**
     * Starts the audio capture stream.
     * @return true if successfully started, false otherwise.
     */
    virtual bool start() = 0;

    /**
     * Stops the audio capture stream.
     * @return true if successfully stopped, false otherwise.
     */
    virtual bool stop() = 0;

    /**
     * Reads raw captured audio into a float buffer.
     * The format of the audio samples is normalized floats (-1.0 to 1.0).
     * Interleaved layout: L, R, L, R... for stereo.
     * 
     * @param buffer Pointer to the destination float array. Must be sized to at least (frames * channels).
     * @param frames Number of audio frames to read.
     * @return Number of frames read, or -1 on error.
     */
    virtual int read(float* buffer, int frames) = 0;

    /**
     * Gets the sample rate of the capture stream (e.g. 48000).
     */
    virtual int getSampleRate() const = 0;

    /**
     * Gets the number of channels (e.g. 2 for stereo).
     */
    virtual int getChannels() const = 0;

    /**
     * Gets the current hardware capture latency in milliseconds.
     * @return Latency in milliseconds, or -1.0 on error.
     */
    virtual double getLatencyMs() const = 0;
};

} // namespace audiostream
