#!/usr/bin/env bash
# =============================================================================
# AudioStream — Release Builder
# Produces a single self-contained binary in dist/AudioStream
# Run this ONCE as the developer; users never need cmake or Python.
#
# Usage:
#   ./build_release.sh
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/venv"
PC_APP_DIR="$SCRIPT_DIR/pc_app"
BUILD_DIR="$SCRIPT_DIR/build"

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()  { echo -e "${GREEN}[build]${NC} $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
error() { echo -e "${RED}[error]${NC} $*"; exit 1; }

# ── Step 1: Build C++ engine ──────────────────────────────────────────────────
info "Step 1/4 — Building C++ capture engine..."

if ! command -v cmake &>/dev/null; then
    error "cmake not found. Install it with: sudo apt install cmake"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF > /dev/null
make -j"$(nproc)"
cd "$SCRIPT_DIR"

SO_FILE=$(ls "$PC_APP_DIR"/audiostream_core*.so 2>/dev/null | head -1)
[ -z "$SO_FILE" ] && error "C++ build succeeded but .so not found in pc_app/"
info "C++ engine built: $(basename "$SO_FILE")"

# ── Step 2: Set up venv ───────────────────────────────────────────────────────
info "Step 2/4 — Setting up Python virtual environment..."

if [ ! -d "$VENV_DIR" ]; then
    python3 -m venv "$VENV_DIR"
fi

"$VENV_DIR/bin/pip" install --quiet --upgrade pip
"$VENV_DIR/bin/pip" install --quiet -r "$PC_APP_DIR/requirements.txt"

# ── Step 3: Install PyInstaller ───────────────────────────────────────────────
info "Step 3/4 — Installing PyInstaller..."
"$VENV_DIR/bin/pip" install --quiet pyinstaller

# ── Step 4: Bundle into single binary ─────────────────────────────────────────
info "Step 4/4 — Bundling into single executable..."

cd "$PC_APP_DIR"
"$VENV_DIR/bin/pyinstaller" AudioStream.spec \
    --distpath "$SCRIPT_DIR/dist" \
    --workpath "$SCRIPT_DIR/build/pyinstaller_work" \
    --noconfirm

# ── Done ──────────────────────────────────────────────────────────────────────
BINARY="$SCRIPT_DIR/dist/AudioStream"
[ ! -f "$BINARY" ] && error "Binary not found after build. Check PyInstaller output above."

chmod +x "$BINARY"
SIZE=$(du -sh "$BINARY" | cut -f1)

echo ""
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}  ✓ Build complete!${NC}"
echo -e "  Binary : dist/AudioStream  (${SIZE})"
echo -e "  Run it : ./dist/AudioStream"
echo -e ""
echo -e "  Upload dist/AudioStream to GitHub Releases so users can"
echo -e "  download and run it with zero dependencies."
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
