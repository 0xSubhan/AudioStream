#!/usr/bin/env bash
# AudioStream PC Broadcaster — one-shot launcher
# Usage: ./run_pc_app.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/venv"
APP="$SCRIPT_DIR/pc_app/app.py"

if [ ! -d "$VENV_DIR" ]; then
    echo "[AudioStream] Virtual environment not found. Creating one..."
    python3 -m venv "$VENV_DIR"
    echo "[AudioStream] Installing dependencies..."
    "$VENV_DIR/bin/pip" install -r "$SCRIPT_DIR/pc_app/requirements.txt" --quiet
fi

echo "[AudioStream] Starting AudioStream Controller..."
cd "$SCRIPT_DIR/pc_app"
exec "$VENV_DIR/bin/python" "$APP" "$@"
