#include "../core/include/pulse_capture.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>

#pragma pack(push, 1)
struct WavHeader {
    char riffHeader[4] = {'R', 'I', 'F', 'F'};
    int32_t wavSize = 0; // Size of file in bytes - 8
    char waveHeader[4] = {'W', 'A', 'V', 'E'};
    char fmtHeader[4] = {'f', 'm', 't', ' '};
    int32_t fmtChunkSize = 16;
    int16_t audioFormat = 1; // PCM = 1
    int16_t numChannels = 2;
    int32_t sampleRate = 48000;
    int32_t byteRate = 0; // sampleRate * numChannels * (bitsPerSample / 8)
    int16_t blockAlign = 0; // numChannels * (bitsPerSample / 8)
    int16_t bitsPerSample = 16;
    char dataHeader[4] = {'d', 'a', 't', 'a'};
    int32_t dataBytes = 0; // Size of audio data in bytes
};
#pragma pack(pop)

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  AudioStream Audio Capture Test (Linux) " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "[Test] Preparing PulseAudio Capture (48kHz, Stereo)..." << std::endl;

    audiostream::PulseAudioCapture capture(48000, 2);

    if (!capture.start()) {
        std::cerr << "[ERROR] Failed to start audio capture. Make sure PulseAudio/PipeWire is running." << std::endl;
        return 1;
    }

    std::cout << "[Test] Capture started successfully." << std::endl;
    std::cout << "[Test] IMPORTANT: Please play some music or video audio on your PC now." << std::endl;
    std::cout << "[Test] Recording will begin in 2 seconds and run for 5 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    const int chunkFrames = 960; // 20ms of audio at 48kHz
    const int sampleRate = capture.getSampleRate();
    const int channels = capture.getChannels();
    const int totalDurationSeconds = 5;
    const int totalFramesToRecord = sampleRate * totalDurationSeconds;
    const int numChunks = totalFramesToRecord / chunkFrames;

    std::vector<float> floatBuffer(chunkFrames * channels);
    std::vector<int16_t> pcm16Buffer;
    pcm16Buffer.reserve(totalFramesToRecord * channels);

    std::cout << "[Test] Recording started..." << std::endl;
    int recordedFrames = 0;

    for (int i = 0; i < numChunks; ++i) {
        int readResult = capture.read(floatBuffer.data(), chunkFrames);
        if (readResult < 0) {
            std::cerr << "[ERROR] Error reading from capture device." << std::endl;
            break;
        }

        // Convert Float32 (-1.0 to 1.0) to PCM 16-bit (-32768 to 32767)
        for (float sample : floatBuffer) {
            float clamped = std::clamp(sample, -1.0f, 1.0f);
            auto val = static_cast<int16_t>(clamped * 32767.0f);
            pcm16Buffer.push_back(val);
        }

        recordedFrames += readResult;
        if (i % 25 == 0 || i == numChunks - 1) {
            std::cout << "Captured " << (recordedFrames / (float)sampleRate) << "s / " 
                      << totalDurationSeconds << "s... Hardware Latency: " 
                      << capture.getLatencyMs() << " ms" << std::endl;
        }
    }

    capture.stop();
    std::cout << "[Test] Capture stopped. Total frames recorded: " << recordedFrames << std::endl;

    // Create WAV header
    WavHeader header;
    header.numChannels = channels;
    header.sampleRate = sampleRate;
    header.bitsPerSample = 16;
    header.blockAlign = channels * (header.bitsPerSample / 8);
    header.byteRate = sampleRate * header.blockAlign;
    header.dataBytes = pcm16Buffer.size() * sizeof(int16_t);
    header.wavSize = 36 + header.dataBytes;

    const std::string filename = "output_capture.wav";
    std::cout << "[Test] Writing WAV file to: " << filename << std::endl;

    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        std::cerr << "[ERROR] Could not open file to write: " << filename << std::endl;
        return 1;
    }

    outFile.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
    outFile.write(reinterpret_cast<const char*>(pcm16Buffer.data()), header.dataBytes);
    outFile.close();

    std::cout << "[Test] File successfully written! Size: " 
              << (header.wavSize + 8) / 1024.0f << " KB" << std::endl;
    std::cout << "[Test] To verify, play this file using your favorite media player." << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
