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
NAME_SUFFIX="${APPIMAGE_NAME_SUFFIX:-}"
OUT="$BUILD/voicecraft-studio$NAME_SUFFIX.AppImage"
VERSION="$(grep -oP 'project\(VoiceCraftStudio VERSION \K[0-9.]+' "$ROOT/CMakeLists.txt" | head -1)"

# ---- locate libraries ------------------------------------------------------
# Library search roots can be overridden for cross builds (e.g. the focal
# sysroot):  APPIMAGE_LIB_ROOTS="dir1 dir2 ..."
if [ -n "$APPIMAGE_LIB_ROOTS" ]; then
  read -r -a LIBDIRS <<< "$APPIMAGE_LIB_ROOTS"
else
  LIBDIRS=("$PREFIX/usr/lib/x86_64-linux-gnu" "$PREFIX/usr/lib/x86_64-linux-gnu/pulseaudio" "$PREFIX/lib" "/usr/lib/x86_64-linux-gnu" "/usr/lib")
fi
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
# copy ALL existing matches of a glob (for sonames that differ between
# distros, e.g. libpulsecommon-17.0 vs -13.99, and multi-lib sets like ICU).
# Symlinks are preserved (cp -P): ld.so looks up the exact DT_SONAME name,
# so BOTH "libX.so.N" links and "libX.so.N.M" real files must be bundled.
copy_lib_glob() {
  local pat="$1" d f copied=0
  for d in "${LIBDIRS[@]}"; do
    for f in "$d"/$pat; do
      if [ -e "$f" ]; then
        if [ -L "$f" ]; then
          cp -P "$f" "$APPDIR/usr/lib/$(basename "$f")"
        else
          install -m 0755 "$f" "$APPDIR/usr/lib/$(basename "$f")"
        fi
        copied=$((copied+1))
      fi
    done
    # stop after the first root that had any match (roots are ordered by
    # preference: focal sysroot before ~/opt before host system)
    [ "$copied" -gt 0 ] && break
  done
  [ "$copied" -gt 0 ] || echo "WARN: $pat not found (will resolve from host)"
  return 0
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
copy_lib "libmd4c.so.0"                     # Qt >= 5.14 (focal Qt 5.12 lacks it - optional)
copy_lib_glob "libdouble-conversion.so.*"   # soname differs across distros
# audio stack
copy_lib "libpulse.so.0"
copy_lib "libpulse-simple.so.0"
copy_lib_glob "libpulsecommon-*.so"         # private PA lib, version-dependent name
copy_lib "librnnoise.so.0"
copy_lib "libsamplerate.so.0"
copy_lib "libsndfile.so.1"
copy_lib_glob "libFLAC.so.*"
copy_lib "libopus.so.0"
copy_lib "libvorbis.so.0"
copy_lib "libvorbisenc.so.2"
copy_lib "libogg.so.0"
# glib (focal Qt5Core links it; host glib may be absent on minimal systems)
copy_lib "libglib-2.0.so.0"
copy_lib "libpcre2-8.so.0"
# focal glib 2.64 uses OLD libpcre (soname .so.3) and libffi.so.7 — both MUST
# come from the focal root (hosts ship pcre2 and libffi.so.8)
copy_lib "libpcre.so.3"
copy_lib "libffi.so.7"
# ICU (focal Qt 5.12 links libicu66; sonames differ per distro -> glob)
copy_lib_glob "libicu*.so.*"

# Qt platform plugins (find them under the prefix or system Qt)
if [ -n "$APPIMAGE_PLUGIN_BASES" ]; then
  read -r -a PLUGIN_BASES <<< "$APPIMAGE_PLUGIN_BASES"
else
  PLUGIN_BASES=("$PREFIX/usr/lib/x86_64-linux-gnu/qt5/plugins" "/usr/lib/x86_64-linux-gnu/qt5/plugins" "/usr/lib/qt5/plugins")
fi
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

# ---- dependency closure completion -----------------------------------------
# Curated lists above cover the common case, but distros differ (focal's
# libpulse pulls libwrap+libsystemd->libgcrypt/lz4, trixie's does not...).
# Iterate: ldd everything bundled, copy any "not found" lib from LIBDIRS,
# until the closure is complete.
resolve_missing() {
  local pass name p changed missing
  for pass in 1 2 3 4 5 6; do
    changed=0
    missing=$({
      ldd "$APPDIR/usr/bin/voicecraft-studio" 2>/dev/null
      ldd "$APPDIR"/usr/lib/*.so* 2>/dev/null
      ldd "$APPDIR"/usr/lib/qt5/plugins/platforms/*.so 2>/dev/null
    } | grep "not found" | awk '{print $1}' | sort -u)
    if [ -z "$missing" ]; then break; fi
    for name in $missing; do
      p="$(find_lib "$name")" || { echo "UNRESOLVED (not in LIBDIRS): $name"; continue; }
      # dereference the found lib under the exact SONAME ld.so asked for
      install -m 0755 "$p" "$APPDIR/usr/lib/$name"
      echo "  bundled missing dep: $name"
      changed=1
    done
    if [ "$changed" -eq 0 ]; then break; fi
  done
}
resolve_missing

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
