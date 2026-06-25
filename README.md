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

## Screenshots

<table>
  <tr>
    <th align="center">Android — Idle</th>
    <th align="center">Android — Receiving</th>
    <th align="center">PC Controller</th>
  </tr>
  <tr>
    <td align="center">
      <img src="docs/screenshots/android_receiver_idle.png" width="200" alt="Android app idle state"/>
    </td>
    <td align="center">
      <img src="docs/screenshots/android_receiver_active.png" width="200" alt="Android app active state with telemetry"/>
    </td>
    <td align="center">
      <img src="docs/screenshots/pc_controller.png" width="300" alt="PC broadcaster controller UI"/>
    </td>
  </tr>
</table>

---

## Prerequisites

### PC (Linux) — for end users
- PulseAudio or PipeWire — pre-installed on Ubuntu 20.04+, Fedora, Arch, etc.
- That's it. **No Python, no cmake, no build tools needed.**

### Android
- Android 8.0+ (API 26+, `minSdk = 26`)
- Connected to the same WiFi network as the PC

---

## Getting Started

### PC Broadcaster (Linux)

**Option A — Pre-built binary (recommended, zero dependencies)**

1. Download `AudioStream` from the [**Releases page**](../../releases/latest).
2. Make it executable and run:

```bash
chmod +x AudioStream
./AudioStream
```

No Python, no cmake, no pip — the binary is fully self-contained.

**Option B — Run from source**

Requires Python 3.10+ and a pre-built `audiostream_core*.so` (see [Developer Build](#developer-build) below).

```bash
./run_pc_app.sh
```

This script creates a venv, installs Python dependencies, and launches the GUI automatically.

---

### Android App

Install the APK from the [**Releases page**](../../releases/latest), or build it yourself:

```bash
cd android_app
flutter build apk --release
adb install -r build/app/outputs/flutter-apk/app-release.apk
```

---

### Streaming

1. Open **AudioStream** on your Android phone and tap **START RECEIVER**.  
   A notification appears: *"AudioStream Active — Receiving audio from PC…"*  
   The receiver keeps running even with the screen off.

2. On your PC, open the AudioStream Controller. Your Android device appears automatically in the **Auto-Discovered Devices** dropdown (mDNS).

3. Select your device and click **Start Streaming**. Audio streams to your phone instantly.

4. To stop: click **Stop Streaming** in the PC app, or tap **Stop** in the Android notification.

---

## Developer Build

To build the single-binary release yourself (requires `cmake`, `gcc`, `libpulse-dev`, `libopus-dev`):

```bash
chmod +x build_release.sh
./build_release.sh
```

This compiles the C++ engine, bundles Python + all dependencies via PyInstaller, and outputs a self-contained `dist/AudioStream` binary ready to upload to GitHub Releases.

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

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.
