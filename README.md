# AudioStream

**Low-latency WiFi audio streaming from your PC to Android.**

AudioStream captures your system audio on Linux/Windows, compresses it with Opus, and streams it to your Android phone over local WiFi using RTP/UDP — all with a sub-40ms end-to-end latency target.

---

## Architecture

```
┌─────────────────────────┐          UDP/RTP          ┌──────────────────────────┐
│   PC (Python + C++)     │ ─────────────────────────▶ │  Android (Flutter + C++) │
│                         │                             │                          │
│  PulseAudio/WASAPI      │  Opus-compressed RTP        │  UDP Socket Receiver     │
│       ↓                 │  packets over WiFi          │       ↓                  │
│  Opus Encoder (C++)     │                             │  Adaptive Jitter Buffer  │
│       ↓                 │                             │       ↓                  │
│  UDP Sender (C++)       │                             │  Opus Decoder (C++)      │
│       ↓                 │                             │       ↓                  │
│  PyQt6 UI + mDNS        │                             │  AAudio Playback         │
└─────────────────────────┘                             └──────────────────────────┘
```

**mDNS auto-discovery** means the PC app automatically finds the Android receiver on your WiFi network — no manual IP entry needed.

---

## Prerequisites

### PC (Linux)
- Python 3.10+
- PulseAudio or PipeWire (pre-installed on Ubuntu 22.04+)
- CMake 3.22+, GCC/Clang with C++17 support
- `libpulse-dev`, `libopus-dev` (or use the local `third_party/extracted` copies)
- PyQt6: `pip install PyQt6`

### Android
- Android 8.0+ (API 26+, `minSdk = 26`)
- Connected to the same WiFi network as the PC

---

## Build & Run

### 1. Build the C++ PC engine

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The Python module `audiostream_core.*.so` will be output to `pc_app/`.

### 2. Start the PC broadcaster

```bash
./run_pc_app.sh
```

This script automatically creates the Python venv if missing, installs dependencies, and launches the GUI. You can also run it directly:

```bash
source venv/bin/activate
cd pc_app && python app.py
```

### 3. Install the Android APK

Build the release APK:
```bash
cd android_app
flutter build apk --release
```

Install via ADB:
```bash
adb install -r build/app/outputs/flutter-apk/app-release.apk
```

### 4. Stream audio

1. Open **AudioStream** on your Android phone and tap **START RECEIVER**.  
   A notification appears: *"AudioStream Active — Receiving audio from PC…"*  
   The receiver keeps running even with the screen off.

2. On your PC, open the AudioStream Controller app. Your Android device will appear automatically in the **Auto-Discovered Devices** dropdown.

3. Select your device and click **Start Streaming**. Audio from your PC speakers/headphones streams to your phone instantly.

4. To stop: tap **Stop Streaming** in the PC app, or tap **Stop** on the Android notification.

---

## Key Features

| Feature | Detail |
|---|---|
| **Transport** | RTP/UDP — low-overhead, latency-optimised |
| **Codec** | Opus (20ms frames, VBR, low-latency mode) |
| **Jitter Buffer** | RFC 3550 adaptive buffer, auto-scales 40–160ms |
| **Android Audio** | AAudio in exclusive + low-latency performance mode |
| **Discovery** | mDNS/Zeroconf — zero manual IP configuration |
| **Background** | Android Foreground Service — survives screen-off |
| **Telemetry** | Live packet count, jitter, buffer latency on both ends |

---

## Project Structure

```
AudioStream/
├── core/                   # C++ capture, encode, transport engine
│   ├── include/            # Headers (capture, opus, rtp, udp)
│   └── src/                # Implementations + pybind11 bindings
├── pc_app/                 # Python PyQt6 broadcaster UI
│   └── app.py
├── android_app/            # Flutter receiver app
│   ├── lib/main.dart       # Flutter UI + method channels
│   └── android/app/src/main/
│       ├── cpp/            # C++ AAudio engine + jitter buffer
│       └── kotlin/         # Kotlin JNI bridge + foreground service
├── tests/                  # C++ unit test binaries
├── docs/design_log.md      # Architecture decisions + changelog
├── run_pc_app.sh           # One-shot PC launcher
└── CMakeLists.txt          # Root CMake build file
```

---

## License

Private / Beta — not for redistribution.
