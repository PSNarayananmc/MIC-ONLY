#!/bin/bash
# VoiceCraft Studio - user-space dependency installer (no root required)
# Downloads .deb packages via apt, extracts them into ~/opt with dpkg -x
set -e

PREFIX="$HOME/opt"
DEBDIR="/home/z/my-project/debs"
mkdir -p "$PREFIX" "$DEBDIR"
cd "$DEBDIR"

ROOT_PKGS=(
  qtbase5-dev
  qtbase5-dev-tools
  libqt5widgets5
  libqt5gui5
  libqt5core5a
  libqt5network5
  libqt5dbus5
  libqt5opengl5
  libpulse-dev
  libsamplerate0-dev
  cmake
  cmake-data
)

echo "=== [1/4] Resolving dependency closure ==="
ALL_PKGS=""
for p in "${ROOT_PKGS[@]}"; do
  deps=$(apt-cache depends --recurse --no-recommends --no-suggests \
    --no-conflicts --no-breaks --no-replaces --no-enhances \
    --no-pre-depends "$p" 2>/dev/null | grep "^\w" | sort -u)
  ALL_PKGS="$ALL_PKGS $deps"
done
ALL_PKGS=$(echo $ALL_PKGS | tr ' ' '\n' | sort -u | grep -v '^$')

echo "=== [2/4] Filtering out already-installed packages ==="
TODO=""
for p in $ALL_PKGS; do
  if ! dpkg -s "$p" >/dev/null 2>&1; then
    TODO="$TODO $p"
  fi
done
echo "Packages to download ($(echo $TODO | wc -w)):"
echo "$TODO" | tr ' ' '\n' | head -80

echo "=== [3/4] Downloading ==="
for p in $TODO; do
  if ls ${p}_*.deb ${p}:amd64_*.deb >/dev/null 2>&1; then continue; fi
  apt-get download "$p" 2>/dev/null || echo "WARN: failed to download $p"
done

echo "=== [4/4] Extracting into $PREFIX ==="
for f in *.deb; do
  dpkg -x "$f" "$PREFIX" || echo "WARN: failed to extract $f"
done

echo "DONE. Extracted $(ls *.deb | wc -l) packages."
