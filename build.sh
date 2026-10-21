#!/bin/bash
# build.sh — one-command build for native systems (Ubuntu 20.04+ / Debian).
# Run on the machine you USE, so the produced binary matches its glibc.
#
#   sudo not required for the build itself; apt needs sudo to install deps.
#
# What you get afterwards:
#   build/voicecraft-studio            — the application binary
#   build/voicecraft-studio-1.0.0-amd64.deb   — installable package
#   build/voicecraft-studio.AppImage          — portable package
#
# Usage:
#   bash build.sh            # full: deps + build + tests + packages
#   bash build.sh --skip-apt # if you already have the dependencies
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$1" != "--skip-apt" ]; then
  echo "=== [1/4] Installing build dependencies ==="
  sudo apt-get update -qq
  sudo apt-get install -y -qq \
    build-essential cmake pkg-config git \
    qtbase5-dev qtbase5-dev-tools \
    libpulse-dev libsamplerate0-dev libsndfile1-dev librnnoise-dev
fi

echo "=== [2/4] Configuring + building ==="
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"

echo "=== [3/4] Running test suite ==="
ctest --output-on-failure

echo "=== [4/4] Building packages ==="
cpack -G DEB
bash "$ROOT/scripts/build_appimage.sh" "$ROOT/build" || \
  echo "NOTE: AppImage step failed (network/FUSE?) - the .deb is complete."

echo
echo "SUCCESS. Artifacts:"
ls -lh "$ROOT"/build/*.deb "$ROOT"/build/*.AppImage 2>/dev/null || true
echo
echo "Install:  sudo apt install ./build/voicecraft-studio-*.deb"
echo "  or run: ./build/voicecraft-studio.AppImage"
