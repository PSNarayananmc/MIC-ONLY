#!/bin/bash
# build_appimage.sh — package VoiceCraft Studio as an AppImage.
#
# Run AFTER a successful cmake build (see build.sh). Bundles the app binary
# together with the non-glibc libraries it needs (Qt5, PulseAudio client,
# RNNoise, sndfile tree, Qt platform plugins) so the AppImage is
# self-contained on any distro whose glibc is >= the glibc of the machine
# that BUILT it (AppImages cannot be newer-glibc compatible than the builder).
#
#   Build on Ubuntu 20.04  ->  AppImage runs on 20.04 and newer
#   Build on Debian 13     ->  AppImage runs on glibc >= 2.34 systems
#
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
BUILD="${1:-$ROOT/build}"
PREFIX="$HOME/opt"          # user-space dependency prefix (sandbox builds)
APPDIR="$BUILD/AppDir"
OUT="$BUILD/voicecraft-studio.AppImage"
VERSION="$(grep -oP 'project\(VoiceCraftStudio VERSION \K[0-9.]+' "$ROOT/CMakeLists.txt" | head -1)"

# ---- locate libraries ------------------------------------------------------
LIBDIRS=("$PREFIX/usr/lib/x86_64-linux-gnu" "$PREFIX/usr/lib/x86_64-linux-gnu/pulseaudio" "$PREFIX/lib" "/usr/lib/x86_64-linux-gnu" "/usr/lib")
find_lib() { # prints first existing path for $1 across LIBDIRS
  local name="$1" d
  for d in "${LIBDIRS[@]}"; do
    [ -e "$d/$name" ] && { echo "$d/$name"; return 0; }
  done
  return 1
}
copy_lib() {
  local p; p="$(find_lib "$1")" || { echo "WARN: $1 not found (will resolve from host)"; return 0; }
  install -D -m 0755 "$p" "$APPDIR/usr/lib/$(basename "$p")"
}

rm -rf "$APPDIR"
install -D -m 0755 "$BUILD/voicecraft-studio" "$APPDIR/usr/bin/voicecraft-studio"

# Qt core + the xcb platform plugin chain (Qt5XcbQpa is required by libqxcb.so)
copy_lib "libQt5Widgets.so.5"
copy_lib "libQt5Gui.so.5"
copy_lib "libQt5Core.so.5"
copy_lib "libQt5DBus.so.5"
copy_lib "libQt5XcbQpa.so.5"
copy_lib "libQt5Network.so.5"
copy_lib "libmd4c.so.0"
copy_lib "libdouble-conversion.so.3"
# audio stack
copy_lib "libpulse.so.0"
copy_lib "libpulse-simple.so.0"
copy_lib "libpulsecommon-17.0.so"
copy_lib "librnnoise.so.0"
copy_lib "libsamplerate.so.0"
copy_lib "libsndfile.so.1"
copy_lib "libFLAC.so.14"
copy_lib "libopus.so.0"
copy_lib "libvorbis.so.0"
copy_lib "libvorbisenc.so.2"
copy_lib "libogg.so.0"

# Qt platform plugins (find them under the prefix or system Qt)
PLUGIN_BASES=("$PREFIX/usr/lib/x86_64-linux-gnu/qt5/plugins" "/usr/lib/x86_64-linux-gnu/qt5/plugins" "/usr/lib/qt5/plugins")
for plug in platforms/libqxcb.so platforms/libqoffscreen.so; do
  for b in "${PLUGIN_BASES[@]}"; do
    if [ -e "$b/$plug" ]; then
      install -D -m 0755 "$b/$plug" "$APPDIR/usr/lib/qt5/plugins/$plug"
      break
    fi
  done
done
# xcb plugin dependencies that are NOT guaranteed on hosts
copy_lib "libxkbcommon-x11.so.0"
copy_lib "libxkbcommon.so.0"

# desktop + icon (AppImage spec: at AppDir root)
install -m 0644 "$ROOT/resources/voicecraft-studio.desktop" "$APPDIR/voicecraft-studio.desktop"
install -D -m 0644 "$ROOT/resources/voicecraft-studio.desktop" "$APPDIR/usr/share/applications/voicecraft-studio.desktop"
install -D -m 0644 "$ROOT/resources/icons/voicecraft-studio.svg" "$APPDIR/voicecraft-studio.svg"
install -D -m 0644 "$ROOT/resources/icons/voicecraft-studio.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/voicecraft-studio.svg"

# AppRun: sets up the bundled library path, then execs the app
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$HERE/usr/lib/qt5/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/lib/qt5/plugins/platforms"
exec "$HERE/usr/bin/voicecraft-studio" "$@"
EOF
chmod 0755 "$APPDIR/AppRun"

# ---- run appimagetool (no FUSE needed with --appimage-extract-and-run) ----
TOOL="$BUILD/.appimagetool-x86_64.AppImage"
if [ ! -x "$TOOL" ]; then
  echo "Downloading appimagetool..."
  curl -sL -o "$TOOL" \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x "$TOOL"
fi
rm -f "$OUT"
"$TOOL" --appimage-extract-and-run "$APPDIR" "$OUT"
echo "DONE: $OUT"
