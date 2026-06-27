# AudioStream — CI Pipeline Documentation

This document explains the complete Continuous Integration (CI) pipeline for the AudioStream project, including how it works, what each workflow does, and how to use it.

---

## What is CI?

CI (Continuous Integration) is an automated system that builds your software on remote machines whenever you push a version tag. Instead of manually building the app on Windows, Linux, and Android separately, GitHub does it all automatically and uploads the finished files to your Releases page.

---

## Overview

AudioStream has **3 parallel CI pipelines**, each producing one downloadable file:

```
git push origin v1.x.x
        │
        ├─────────────────────────────────────────────┐
        │                        │                    │
        ▼                        ▼                    ▼
  Windows 11 PC           Ubuntu 22.04          Ubuntu 22.04
  (GitHub cloud)          (GitHub cloud)        (GitHub cloud)
        │                        │                    │
  MSVC + vcpkg            GCC + PulseAudio      Flutter + NDK
  PyInstaller             PyInstaller           Gradle
        │                        │                    │
        ▼                        ▼                    ▼
  AudioStream.exe        AudioStream           app-release.apk
        │                        │                    │
        └────────────────────────┴────────────────────┘
                                 │
                                 ▼
              github.com/0xSubhan/AudioStream/releases/v1.x.x
```

All 3 pipelines run **simultaneously** and each attaches its output file to the same GitHub Release.

---

## Trigger

All pipelines share the same trigger condition — a git version tag pushed to the repository:

```bash
git tag v1.2.0
git push origin v1.2.0
```

The tag must match the pattern `v*.*.*` (e.g. `v1.0.0`, `v2.3.1`). Pushing to `main` alone does **not** trigger any pipeline.

### Smart Path Filtering

Each pipeline only runs if the files relevant to it actually changed, saving time and CI minutes:

| Files changed in your commit | Windows rebuilds? | Linux rebuilds? | Android rebuilds? |
|------------------------------|:-----------------:|:---------------:|:-----------------:|
| `android_app/**` only        | ❌ skipped        | ❌ skipped      | ✅ runs           |
| `core/**` or `pc_app/**`     | ✅ runs           | ✅ runs         | ❌ skipped        |
| `CMakeLists.txt`             | ✅ runs           | ✅ runs         | ❌ skipped        |
| Changes across all areas     | ✅ runs           | ✅ runs         | ✅ runs           |

You can also trigger any pipeline manually from the GitHub **Actions tab** regardless of path changes, using the `workflow_dispatch` trigger.

---

## Pipeline 1 — Windows EXE

**File:** `.github/workflows/build-windows.yml`
**Machine:** `windows-latest` (Windows 11 x64)
**Output:** `AudioStream.exe`

### Step-by-Step

| Step | Action | Why |
|------|--------|-----|
| **Checkout** | Downloads the repo onto the Windows machine | Like `git clone` |
| **Set up Python 3.12** | Installs Python on the machine | The PyQt6 UI runs on Python |
| **pip install** | Installs `pybind11`, `pyinstaller`, `pillow`, `PyQt6`, `zeroconf` | Must happen **before** CMake so pybind11 is discoverable |
| **vcpkg install opus** | Installs the Opus audio codec library via vcpkg | Windows has no `apt-get`; vcpkg is the Windows package manager |
| **CMake configure** | Reads `CMakeLists.txt`, locates Opus via vcpkg toolchain, generates a Visual Studio build project | Prepares the C++ build system |
| **Build C++ engine** | MSVC compiles all C++ source files | Produces `audiostream_core.cpython-312-win-amd64.pyd` in `pc_app/` |
| **Verify .pyd** | Checks the `.pyd` file landed in `pc_app/` | Fails fast if C++ build silently went wrong |
| **Generate icon** | Converts `icon.png` → `icon.ico` using Pillow | Windows `.exe` files use `.ico` format |
| **PyInstaller** | Bundles Python runtime + PyQt6 + `.pyd` + all deps into a single file | Produces `dist/AudioStream.exe` (~50–70 MB) |
| **Upload to Release** | Attaches `AudioStream.exe` to the GitHub Release | End users download this |

### What the C++ Engine Compiles on Windows

```
wasapi_capture.cpp     ← WASAPI loopback audio capture (Windows-native)
stream_controller.cpp  ← Main streaming loop (#ifdef _WIN32 selects WASAPI)
udp_sender.cpp         ← Network socket using Winsock2 (not POSIX)
opus_encoder.cpp       ← Opus audio compression
opus_decoder.cpp       ← Opus audio decompression
rtp_packet.h           ← Uses winsock2.h for htons/htonl (not arpa/inet.h)
bindings.cpp           ← pybind11 bridge exposing C++ to Python
```

---

## Pipeline 2 — Linux Binary

**File:** `.github/workflows/build-linux.yml`
**Machine:** `ubuntu-22.04` (x64)
**Output:** `AudioStream` (no extension, ELF binary)

### Step-by-Step

| Step | Action | Why |
|------|--------|-----|
| **Set up Python 3.12** | Runs first — Ubuntu 22.04 ships Python 3.10 by default | Ensures CMake finds the correct Python version |
| **apt-get install** | Installs `cmake`, `gcc`, `g++`, `libpulse-dev`, `libopus-dev` | System-level C++ dependencies |
| **pip install** | Installs `pybind11`, `pyinstaller`, `PyQt6`, `zeroconf` | Must happen before CMake |
| **CMake configure** | `-DPython3_EXECUTABLE=$(which python)` forces the 3.12 binary | Prevents CMake from picking up the system Python 3.10 |
| **Build C++ engine** | GCC compiles the Linux path of the source | Produces `audiostream_core.cpython-312-x86_64-linux-gnu.so` in `pc_app/` |
| **Verify .so** | Checks the `.so` file is in `pc_app/` | Fails fast if build silently failed |
| **PyInstaller** | Same `.spec` file — auto-detects Linux, picks `.so` + `icon.png` | Produces `dist/AudioStream` |
| **Upload to Release** | Attaches `AudioStream` binary to the GitHub Release | Linux users download this |

### What the C++ Engine Compiles on Linux

```
pulse_capture.cpp      ← PulseAudio monitor source capture (Linux-native)
stream_controller.cpp  ← Main loop (#ifdef _WIN32 is false → uses PulseAudio)
udp_sender.cpp         ← Network socket using POSIX (sys/socket.h)
opus_encoder.cpp
opus_decoder.cpp
bindings.cpp
```

The same source files compile differently on each platform due to `#ifdef _WIN32` guards placed throughout the code.

---

## Pipeline 3 — Android APK

**File:** `.github/workflows/build-android.yml`
**Machine:** `ubuntu-22.04` (x64)
**Output:** `app-release.apk`

### Step-by-Step

| Step | Action | Why |
|------|--------|-----|
| **Set up Java 17** | Installs Temurin JDK 17 | Gradle (Android's build system) requires Java to run |
| **Set up Flutter** | Installs Flutter SDK stable 3.x | Flutter compiles Dart and manages the Android toolchain |
| **Accept SDK licenses** | Auto-accepts Android SDK legal agreements | Normally requires manual "Yes" clicks in Android Studio |
| **flutter pub get** | Downloads all Dart packages from `pubspec.yaml` | Equivalent to `pip install` or `npm install` |
| **flutter build apk --release** | Full release build | Compiles Dart UI → native ARM64 code; compiles `android_receiver_engine.cpp` + `jitter_buffer.h` via NDK; packages into APK |
| **Upload to Release** | Attaches `app-release.apk` to the GitHub Release | Android users download and sideload this |

### What the NDK Compiles

```
android_receiver_engine.cpp  ← AAudio playback engine, RTP packet parsing
jitter_buffer.h              ← RFC 3550 adaptive jitter buffer with drift correction
```

---

## Permissions & Security

```yaml
permissions:
  contents: write
```

Each workflow declares `contents: write` so the auto-generated `GITHUB_TOKEN` can create and update GitHub Releases. Without this, the upload step fails with a `403 Forbidden` error.

The `GITHUB_TOKEN` is a temporary secret automatically injected by GitHub into every workflow run. It expires when the run finishes. You never set it up manually.

---

## Workflow Files at a Glance

```
.github/workflows/
├── build-windows.yml    → Windows 11 → MSVC → PyInstaller → AudioStream.exe
├── build-linux.yml      → Ubuntu 22.04 → GCC → PyInstaller → AudioStream
└── build-android.yml    → Ubuntu 22.04 → Flutter + NDK → Gradle → app-release.apk
```

---

## Release Workflow for Developers

```bash
# 1. Make your changes and commit
git add .
git commit -m "feat: describe your change"
git push origin main          # saves code — does NOT trigger CI

# 2. Tag the release to trigger CI
git tag v1.2.0
git push origin v1.2.0        # triggers all relevant pipelines

# 3. Wait ~10 minutes
# 4. Check github.com/0xSubhan/AudioStream/releases — all files appear automatically
```

### Version Naming Convention

```
v MAJOR . MINOR . PATCH
        │         │
        │         └── Bug fix only     (v1.1.0 → v1.1.1)
        └──────────── New feature      (v1.1.0 → v1.2.0)
  └─────────────────── Breaking change (v1.0.0 → v2.0.0)
```

---

## Monitoring a Pipeline Run

1. Go to `https://github.com/0xSubhan/AudioStream/actions`
2. Click a workflow run to see all steps
3. Click any step to see its live or historical log output
4. Green ✅ = passed, Red ❌ = failed, Yellow 🟡 = running

If a step fails, the log shows the exact error on the exact line — use that to diagnose the issue.
