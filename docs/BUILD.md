# AudioStream — Build Guide

This document explains how to build AudioStream from source for all platforms.
For most users, just download the pre-built binaries from the [Releases page](https://github.com/0xSubhan/AudioStream/releases/latest) instead.

---

## Table of Contents

- [Project Structure](#project-structure)
- [Build the PC App — Linux](#build-the-pc-app--linux)
- [Build the PC App — Windows](#build-the-pc-app--windows)
- [Build the Android App](#build-the-android-app)
- [Releasing a New Version](#releasing-a-new-version)
- [CI / GitHub Actions](#ci--github-actions)

---

## Project Structure

```
AudioStream/
├── core/                   # C++ audio engine (shared across PC platforms)
│   ├── include/            # Headers (.h)
│   └── src/                # Implementations (.cpp)
├── pc_app/                 # Python PyQt6 broadcaster UI
│   ├── app.py              # Main application
│   ├── AudioStream.spec    # PyInstaller bundling config
│   └── requirements.txt    # Python dependencies
├── android_app/            # Flutter receiver app
│   ├── lib/main.dart       # Flutter UI
│   └── android/app/src/main/cpp/  # C++ NDK audio engine
├── tests/                  # C++ unit/integration tests
├── .github/workflows/      # GitHub Actions CI pipelines
├── build_release.sh        # One-shot Linux release builder
└── build_release_windows.bat  # One-shot Windows release builder
```

---

## Build the PC App — Linux

### Prerequisites

Install system packages (Ubuntu/Debian):

```bash
sudo apt install cmake gcc g++ libpulse-dev libopus-dev python3 python3-venv
```

Minimum versions:
- CMake 3.12+
- GCC / Clang with C++17 support
- Python 3.10+

### Option A — Automated (recommended)

```bash
git clone https://github.com/0xSubhan/AudioStream.git
cd AudioStream
./build_release.sh
```

This script:
1. Builds the C++ engine with CMake
2. Creates a Python virtual environment
3. Installs all Python dependencies
4. Bundles everything into `dist/AudioStream` via PyInstaller

The final binary is at `dist/AudioStream`. Copy it anywhere and run it.

### Option B — Manual steps

**Step 1 — Build the C++ engine:**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

This produces `pc_app/audiostream_core.cpython-3XX-x86_64-linux-gnu.so`.

**Step 2 — Set up Python environment:**

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r pc_app/requirements.txt
```

**Step 3 — Run from source:**

```bash
source venv/bin/activate
cd pc_app
python app.py
```

**Step 4 — Bundle into standalone binary (optional):**

```bash
source venv/bin/activate
pip install pyinstaller
cd pc_app
pyinstaller AudioStream.spec --distpath ../dist --workpath ../build/pyinstaller_work --noconfirm
```

Output: `dist/AudioStream`

---

## Build the PC App — Windows

### Prerequisites

| Tool | Download |
|------|---------|
| Visual Studio 2022 Build Tools (C++ workload) | [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) |
| CMake 3.12+ | [cmake.org/download](https://cmake.org/download) |
| Python 3.10+ (add to PATH during install) | [python.org/downloads](https://www.python.org/downloads/) |
| Git | [git-scm.com](https://git-scm.com) |
| vcpkg (for Opus) | See below |

### Install vcpkg and Opus (one-time setup)

Open PowerShell as Administrator:

```powershell
# Clone vcpkg
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# Install Opus
C:\vcpkg\vcpkg install opus:x64-windows
C:\vcpkg\vcpkg integrate install

# Set permanent environment variable
[System.Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\vcpkg', 'User')
```

Restart your terminal after setting the environment variable.

### Option A — Automated (recommended)

```bat
git clone https://github.com/0xSubhan/AudioStream.git
cd AudioStream
build_release_windows.bat
```

Output: `dist\AudioStream.exe`

### Option B — Manual steps

**Step 1 — Build the C++ engine:**

```powershell
cmake -B build `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -A x64

cmake --build build --config Release --parallel
```

This produces `pc_app\audiostream_core.cpython-3XX-win-amd64.pyd`.

**Step 2 — Set up Python environment:**

```powershell
python -m venv venv
.\venv\Scripts\Activate.ps1
pip install -r pc_app\requirements.txt
```

**Step 3 — Run from source:**

```powershell
.\venv\Scripts\Activate.ps1
cd pc_app
python app.py
```

**Step 4 — Bundle into standalone .exe (optional):**

```powershell
pip install pyinstaller pillow
cd pc_app
pyinstaller AudioStream.spec --distpath ..\dist --workpath ..\build\pyinstaller_work --noconfirm
```

Output: `dist\AudioStream.exe`

---

## Build the Android App

### Prerequisites

| Tool | Notes |
|------|-------|
| Flutter SDK 3.x (stable) | [flutter.dev/docs/get-started/install](https://docs.flutter.dev/get-started/install) |
| Android Studio | Needed for Android SDK & NDK |
| Java 17 | Bundled with Android Studio or install separately |
| Android SDK API 26+ | Install via Android Studio SDK Manager |
| Android NDK | Install via Android Studio SDK Manager |

Verify your setup:
```bash
flutter doctor
```
All items should show a green checkmark ✅.

### Build Steps

```bash
cd android_app

# Install Dart/Flutter dependencies
flutter pub get

# Build a debug APK (faster, for testing)
flutter build apk --debug

# Build a release APK (for distribution)
flutter build apk --release
```

Output: `android_app/build/app/outputs/flutter-apk/app-release.apk`

### Install on a device

```bash
# Via ADB (USB debugging enabled on phone)
adb install android_app/build/app/outputs/flutter-apk/app-release.apk

# Or copy the APK to your phone and open it
```

---

## Running the C++ Tests (Linux only)

After building with CMake:

```bash
# Test Opus encoding/decoding round-trip
./build/bin/test_opus

# Test PulseAudio capture (requires audio playing)
./build/bin/test_capture

# Test full UDP streaming pipeline (requires a receiver on 127.0.0.1:8554)
./build/bin/test_sender
```

---

## Releasing a New Version

The CI pipeline handles building and uploading automatically. To release:

```bash
# 1. Commit all your changes
git add .
git commit -m "feat: your change description"
git push origin main

# 2. Create and push a version tag
git tag v1.2.0
git push origin v1.2.0
```

GitHub Actions then:
- Builds `AudioStream.exe` on a Windows machine → uploads to Release
- Builds `AudioStream` binary on a Linux machine → uploads to Release
- Builds `app-release.apk` using Flutter → uploads to Release

All 3 pipelines run in parallel. Total time: ~10 minutes.

### Version Naming

```
v MAJOR . MINOR . PATCH

PATCH  →  Bug fixes only          (v1.1.0 → v1.1.1)
MINOR  →  New features            (v1.1.0 → v1.2.0)
MAJOR  →  Breaking changes        (v1.0.0 → v2.0.0)
```

### Path-Based Smart Rebuilds

The CI only rebuilds what actually changed:

| Files changed | Windows | Linux | Android |
|--------------|:-------:|:-----:|:-------:|
| `android_app/**` | ❌ | ❌ | ✅ |
| `core/**` or `pc_app/**` | ✅ | ✅ | ❌ |
| Everything | ✅ | ✅ | ✅ |

To force-rebuild all pipelines regardless of path changes, trigger manually from the **Actions tab** on GitHub.

---

## CI / GitHub Actions

Workflow files are in `.github/workflows/`:

| File | Trigger | Produces |
|------|---------|---------|
| `build-windows.yml` | Tag + `core/` or `pc_app/` changed | `AudioStream.exe` |
| `build-linux.yml` | Tag + `core/` or `pc_app/` changed | `AudioStream` |
| `build-android.yml` | Tag + `android_app/` changed | `app-release.apk` |

Each workflow:
1. Rents a fresh cloud machine (Windows 11 or Ubuntu 22.04)
2. Installs all dependencies from scratch
3. Builds the project
4. Uploads the binary to the GitHub Release
5. Deletes the rented machine

The `GITHUB_TOKEN` secret is automatically provided by GitHub — no manual setup needed.
