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
# FORCE_PKGS: runtime libs whose -dev counterparts we need. dpkg considers
# them "installed" system-wide (so the filter below skips them), but ~/opt
# MUST carry the MATCHING runtime .so.N — otherwise the dev package's .so
# symlinks dangle ("libpulse.so -> libpulse.so.0" with no target) and the
# linker fails with "cannot find -lpulse" even with correct -L dirs.
# Real bug seen 2026-10-21: system libpulse0 0.24.3 vs libpulse-dev 17.0.
for p in libpulse0 libsamplerate0; do
  ls ${p}_*.deb ${p}:amd64_*.deb >/dev/null 2>&1 || \
    apt-get download "$p" 2>/dev/null || echo "WARN: failed to download $p"
done
for p in $TODO; do
  if ls ${p}_*.deb ${p}:amd64_*.deb >/dev/null 2>&1; then continue; fi
  apt-get download "$p" 2>/dev/null || echo "WARN: failed to download $p"
done

echo "=== [4/4] Extracting into $PREFIX ==="
for f in *.deb; do
  dpkg -x "$f" "$PREFIX" || echo "WARN: failed to extract $f"
done

echo "DONE. Extracted $(ls *.deb | wc -l) packages."

# --- [5/5] Patch pkg-config prefixes ---------------------------------------
# Debian ships .pc files with prefix=/usr, but we extracted into $PREFIX.
# Without this, pkg-config resolves includes/libs to the SYSTEM /usr where
# the dev headers are NOT installed (no root). Real bug seen in practice:
#   fatal error: pulse/pulseaudio.h: No such file or directory
# Fix: rewrite prefix=/usr -> prefix=$PREFIX/usr in every extracted .pc.
echo "=== [5/5] Patching pkg-config prefixes to $PREFIX/usr ==="
for pcdir in "$PREFIX"/usr/lib/*/pkgconfig "$PREFIX"/usr/lib/pkgconfig "$PREFIX"/usr/share/pkgconfig; do
  [ -d "$pcdir" ] || continue
  for f in "$pcdir"/*.pc; do
    [ -f "$f" ] || continue
    sed -i "s|^prefix=/usr\$|prefix=$PREFIX/usr|" "$f"
    # Some files hardcode exec_prefix=/usr as well
    sed -i "s|^exec_prefix=/usr\$|exec_prefix=$PREFIX/usr|" "$f"
    # Debian libpulse*.pc files hardcode ABSOLUTE libdir/includedir instead of
    # using ${prefix}. pkg-config silently suppresses default system dirs, so
    # these resolve to nothing and the linker dies with "cannot find -lpulse".
    sed -i "s|^libdir=/usr/lib/x86_64-linux-gnu\$|libdir=$PREFIX/usr/lib/x86_64-linux-gnu|" "$f"
    sed -i "s|^libdir=/usr/lib\$|libdir=$PREFIX/usr/lib|" "$f"
    sed -i "s|^includedir=/usr/include\$|includedir=$PREFIX/usr/include|" "$f"
  done
done
echo "Patch applied to $(grep -l "^prefix=$PREFIX/usr" "$PREFIX"/usr/lib/*/pkgconfig/*.pc 2>/dev/null | wc -l) pc files."
echo "Verify libpulse resolves: $(pkg-config --libs-only-L libpulse 2>/dev/null)"
