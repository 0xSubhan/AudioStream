# AudioStream Design & Decision Log

This document serves as a persistent record of architectural decisions, planning details, timeline estimates, and change history for AudioStream.

---

## 1. Project Overview & Latency Goal
* **Objective**: Stream local PC audio to Android over local WiFi.
* **Target Latency**: Sub-40ms end-to-end.
* **Key Differentiator**: UDP-based RTP transport with adaptive jitter buffering instead of TCP/HTTP.

---

## 2. Directory Structure

```
/home/subhan/Desktop/AudioStream/
├── core/
│   ├── include/          # C++ Headers
│   │   ├── capture.h      # Abstract capture interface
│   │   └── pulse_capture.h # PulseAudio capture interface
│   ├── src/              # C++ Source Code
│   │   └── pulse_capture.cpp # PulseAudio capture implementation
│   └── CMakeLists.txt    # CMake for core component
├── pc_app/               # Python PC Client (UI PyQt6, mDNS discovery)
├── android_app/          # Flutter Android Client (mDNS receiver, Oboe playback)
├── docs/                 # Project documentation and logs
│   └── design_log.md     # Decisions and changelog (this file)
├── tests/                # Testing scripts & test output files
│   └── test_capture.cpp  # Test utility for Phase 1 capture
├── third_party/          # Locally extracted dependencies (libpulse, libopus)
├── CMakeLists.txt        # Root CMake build file
└── README.md             # Project README
```

---

## 3. Total Project Time Estimate
Based on 2-3 hours of focused daily development, the total project timeline is estimated at **21 to 24 weeks (~5.5 to 6 months)**:

* **Phase 0: Foundation & Directory Structure**: 1 week
* **Phase 1: Audio Capture (WASAPI / PulseAudio)**: 2-3 weeks (Current Phase)
* **Phase 2: Opus Encoder & Decoder (C++)**: 1-2 weeks
* **Phase 3: UDP Transport & RTP Layer (C++)**: 2 weeks
* **Phase 4: pybind11 bindings & PyQt6 PC UI**: 1-2 weeks
* **Phase 5: Android App (Flutter + Oboe)**: 3-4 weeks
* **Phase 6: mDNS Zeroconf Discovery**: 1 week
* **Phase 7: Latency Optimization (Adaptive Jitter)**: 2-3 weeks
* **Phase 8: Cross-Platform & Linux Testing**: 2 weeks
* **Phase 9: Polishing, Packaging & Beta Release**: 3-4 weeks

---

## 4. Key Design Decisions

### [Decision 1] Local Dependency Extraction
* **Date**: June 23, 2026
* **Details**: To bypass password prompt restrictions for `sudo apt-get`, we download the `.deb` files for `libpulse-dev` and `libopus-dev` using `apt-get download` and unpack them locally inside the `third_party/extracted` directory.
* **Linking Strategy**: We point the local `.so` symlinks directly to the system runtime libraries `/usr/lib/x86_64-linux-gnu/libpulse.so.0` and `/usr/lib/x86_64-linux-gnu/libpulse-simple.so.0`. This ensures compilation works locally, and runtime linking resolves to the system packages already present.

### [Decision 2] PulseAudio Simple API for Capture on Linux
* **Date**: June 23, 2026
* **Details**: We selected PulseAudio's Simple API (`pa_simple`) for Linux audio capture. Since modern Ubuntu (24.04) uses PipeWire, it supports the `pipewire-pulse` compatibility server natively. The simple API provides a straightforward synchronous read interface (`pa_simple_read()`), which is ideal for a low-latency blocking loop running in a dedicated thread.
* **Monitor Loopback Resolution**: To capture system output (what plays on PC speakers), we target the default sink's monitor source. We will implement auto-resolution of the monitor source name by reading `pactl get-default-sink` and appending `.monitor`.

---

## 5. Changelog

### 2026-06-23 (Initial Project Setup & Phase 1 Execution)
* Created workspace directories: `core/include`, `core/src`, `pc_app`, `android_app`, `docs`, `tests`.
* Downloaded and extracted `libpulse-dev` and `libopus-dev` locally.
* Fixed broken symlinks in `third_party/extracted/usr/lib/x86_64-linux-gnu/` to point directly to system libraries.
* Initialized `design_log.md` to document planning and project timeline.
* Designed and implemented `AudioCapture` interface in C++ (`core/include/capture.h`).
* Implemented `PulseAudioCapture` using PulseAudio Simple API in `core/include/pulse_capture.h` and `core/src/pulse_capture.cpp`.
* Configured CMake build system in root `CMakeLists.txt` and `core/CMakeLists.txt` supporting local extracted dependencies.
* Developed `tests/test_capture.cpp` to verify capturing 5 seconds of system audio to a `.wav` file.
* Compiled targets successfully.
* Ran `./build/bin/test_capture` and successfully recorded 5 seconds of system audio, creating a valid `output_capture.wav` file (938 KB).
* Created `.vscode/c_cpp_properties.json` and `.clangd` configuration files and updated `#include` directives to relative paths to resolve IDE search path warnings (red squiggles).

### 2026-06-23 (Phase 2 Execution: Opus Compression & Decompression)
* Implemented `OpusEncoder` wrapper in `core/include/opus_encoder.h` and `core/src/opus_encoder.cpp` configured for low-latency VBR (Variable Bitrate) audio.
* Implemented `OpusDecoder` wrapper in `core/include/opus_decoder.h` and `core/src/opus_decoder.cpp` for real-time decompressing.
* Modified `core/CMakeLists.txt` and root `CMakeLists.txt` to compile new source files, link `audiostream_core` against `libopus`, and define the `test_opus` target.
* Developed `tests/test_opus.cpp` to verify the capture-encode-decode-WAV loopback pipeline.
* Compiled and ran `./build/bin/test_opus` successfully, proving transparent audio compression and reconstruction.

