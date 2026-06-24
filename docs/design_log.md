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

* **Phase 0: Foundation & Directory Structure**: 1 week (Completed)
* **Phase 1: Audio Capture (WASAPI / PulseAudio)**: 2-3 weeks (Completed)
* **Phase 2: Opus Encoder & Decoder (C++)**: 1-2 weeks (Completed)
* **Phase 3: UDP Transport & RTP Layer (C++)**: 2 weeks (Completed)
* **Phase 4: pybind11 bindings & PyQt6 PC UI**: 1-2 weeks (Completed)
* **Phase 5: Android App (Flutter + AAudio)**: 3-4 weeks (Completed)
* **Phase 6: mDNS Zeroconf Discovery**: 1 week (Completed)
* **Phase 7: Latency Optimization (Adaptive Jitter)**: 2-3 weeks (Completed)
* **Phase 8: Cross-Platform & Linux Testing**: 2 weeks (Completed)
* **Phase 9: Polishing, Packaging & Beta Release**: 3-4 weeks (Current Phase)

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

### 2026-06-23 (Phase 3 Execution: UDP Sockets & RTP Transport)
* Implemented standard 12-byte RTP packet serialization helper `packRtpHeader` in `core/include/rtp_packet.h`.
* Developed `UdpSender` wrapper in `core/include/udp_sender.h` and `core/src/udp_sender.cpp` utilizing socket connections and optimized OS buffers (`SO_SNDBUF` to 8KB) to prevent network backlog lag.
* Modified root and library `CMakeLists.txt` to build `udp_sender.cpp` and define the `test_sender` runner target.
* Created `tests/stream.sdp` session description descriptor for media players.
* Developed `tests/test_sender.cpp` to drive the loopback capture-encode-RTP-UDP network streaming.
* Verified real-time streaming locally by running the sender and listening through `ffplay` with low latency.

### 2026-06-24 (Phase 4 Execution: pybind11 C++ Bindings & PyQt6 PC UI)
* Designed and implemented `StreamController` class in `core/include/stream_controller.h` and `core/src/stream_controller.cpp`. This class wraps the capture-encode-stream engine and runs it in a background `std::thread`, using atomic primitives (`std::atomic<bool>`, `std::atomic<float>`, `std::atomic<int>`) for thread-safe telemetry access.
* Implemented Python binding wrapper in `core/src/bindings.cpp` using `pybind11`, exposing the C++ `StreamController` class, its lifecycle controls (`start()`, `stop()`), and live telemetry methods directly to Python.
* Configured local Python virtual environment (`venv/`) and defined application dependencies in `pc_app/requirements.txt`.
* Updated root and core library `CMakeLists.txt` files to dynamically query the python environment's `pybind11` configuration, generate the `audiostream_core` shared library module, and output it directly to the `pc_app/` folder for seamless import.
* Developed the PyQt6 desktop client application `pc_app/app.py` featuring a premium dark mode, glassmorphism telemetry cards, and dynamic UI updates via a `QTimer` query loop.
* Integrated thread-safe mDNS auto-discovery in `pc_app/app.py` using `zeroconf` which listens for remote receivers and safely propagates events to the main Qt event loop using custom signals.

### 2026-06-24 (Phase 5 Execution: Android App Receiver & AAudio Engine)
* Created a zero-dependency Flutter app configuration under `android_app/` with a high-fidelity, glowing, dark-themed user interface.
* Implemented `JitterBuffer` in `android_app/android/app/src/main/cpp/jitter_buffer.h` utilizing std::map sorting to automatically order incoming RTP packets, absorb packet arrival jitter, and signal packet gaps to the decoder.
* Implemented `ReceiverEngine` in `android_app/android/app/src/main/cpp/android_receiver_engine.cpp` which receives UDP packets on port 8554 and plays them back using the low-latency native **AAudio** NDK API.
* Configured AAudio in low-latency performance mode (`AAUDIO_PERFORMANCE_MODE_LOW_LATENCY`) and exclusive device sharing mode (`AAUDIO_SHARING_MODE_EXCLUSIVE`) for sub-10ms driver playback.
* Integrated Opus decoding and Packet Loss Concealment (PLC) inside the C++ audio thread to guarantee zero JNI crossing overhead in the fast rendering path.
* Created `android_app/android/app/src/main/cpp/CMakeLists.txt` using CMake's built-in `FetchContent` module to automatically download and compile the Opus codec from source for the target Android device's instruction set architecture.
* Developed `ReceiverEngine.kt` to bind native C++ methods to Kotlin.

### 2026-06-25 (Phase 6 Execution: mDNS Zeroconf Discovery)
* Configured Android service registration in `android_app/android/app/src/main/kotlin/com/audiostream/audiostream_app/MainActivity.kt` using `NsdManager` to dynamically broadcast the receiver's device name (e.g., `AudioStream-Pixel 6a`) on the local network under the `_audiostream._udp` service type.
* Implemented a background discovery browser in `pc_app/app.py` using the `zeroconf` library to scan for active `_audiostream._udp.local.` services.
* Integrated a thread-safe `DiscoverySignaler` using PyQt6 custom signals in `pc_app/app.py` to safely propagate device additions and removals from the background Zeroconf thread to the main UI event loop.
* Updated the PyQt6 `MainWindow` connection manager to dynamically update the device dropdown list and auto-populate target IP and port parameters upon receiver selection.

### 2026-06-25 (Phase 7 Execution: Latency Optimization & Adaptive Jitter)
* Designed and implemented dynamic inter-arrival jitter estimation in `android_app/android/app/src/main/cpp/jitter_buffer.h` adhering to RFC 3550 standard formulas.
* Added auto-scaling buffer target delay (`targetDelay_`) clamping between 2 packets (40ms) and 8 packets (160ms) based on real-time packet arrival delay variance.
* Implemented queue-draining catch-up mechanism in C++: When the buffer queue size exceeds `targetDelay_ + 2` packets, the buffer discards older frames and advances `nextSeq_` to immediately recover low latency.
* Extended C++ JNI interface, Kotlin bindings (`ReceiverEngine.kt`), and Flutter native method channels (`MainActivity.kt`) to expose real-time estimated jitter and target buffer latency telemetry.
* Redesigned the Flutter UI dashboard with a premium 2x2 grid card showing Packets Received, Starvations, Estimated Jitter (with dynamic color warning thresholds), and Target Buffer Latency.

### 2026-06-25 (Phase 8 Execution: Cross-Platform & Linux Testing)
* Configured and compiled the full CMake C++ workspace on Linux, building targets `audiostream_core`, `test_capture`, `test_opus`, `test_sender`, and Python pybind11 wrappers `audiostream_pybindings`.
* Successfully executed `./build/bin/test_capture` on Linux, validating PulseAudio monitor loopback capture and writing 5 seconds of captured system audio to a valid `.wav` file (`output_capture.wav`).
* Successfully executed `./build/bin/test_opus` on Linux, validating the real-time Opus capture-encode-decode pipeline and writing a fully reconstructed `.wav` file (`output_opus.wav`).
* Verified Python pybind11 import compatibility by successfully importing `audiostream_core` from within the virtual environment Python interpreter.




