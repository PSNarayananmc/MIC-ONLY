#!/bin/bash
# package_focal.sh — produce the Ubuntu 20.04 deliverables from a focal
# (cross) build directory:
#   voicecraft-studio-<v>-focal-amd64.deb       (dpkg-deb, hand-rolled)
#   voicecraft-studio-<v>-focal-x86_64.AppImage (via build_appimage.sh)
#
# .deb layout:
#   /usr/bin/voicecraft-studio                       (focal-linked binary)
#   /usr/lib/voicecraft-studio/librnnoise.so.0       (private, WITH weights)
#   /usr/share/{applications,icons,doc}/..., /etc/udev/rules.d/...
#
# The binary carries RUNPATH $ORIGIN/../lib/voicecraft-studio so it finds
# the private RNNoise; everything else comes from focal's distro packages
# (declared in Depends). Distro librnnoise0 is NOT used because it ships
# without pretrained weights (rnnoise_create(nullptr) would be a no-op).
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$1"; VERSION="$2"
[ -x "$BUILD/voicecraft-studio" ] || { echo "usage: package_focal.sh <builddir> <version>"; exit 1; }
FOCAL_ROOT="${FOCAL_ROOT:-/home/z/focal}"
FOCAL_OPT="${FOCAL_OPT:-/home/z/focal-opt}"
STAGE="$BUILD/.deb-stage"
DEB="$BUILD/voicecraft-studio-$VERSION-focal-amd64.deb"

# ---------------- staging ----------------
rm -rf "$STAGE"; mkdir -p "$STAGE"
install -D -m 0755 "$BUILD/voicecraft-studio" "$STAGE/usr/bin/voicecraft-studio"
install -D -m 0755 "$FOCAL_OPT/lib/librnnoise.so.0.4.1" \
  "$STAGE/usr/lib/voicecraft-studio/librnnoise.so.0.4.1"
ln -sf librnnoise.so.0.4.1 "$STAGE/usr/lib/voicecraft-studio/librnnoise.so.0"
install -D -m 0644 "$ROOT/resources/voicecraft-studio.desktop" \
  "$STAGE/usr/share/applications/voicecraft-studio.desktop"
install -D -m 0644 "$ROOT/resources/icons/voicecraft-studio.svg" \
  "$STAGE/usr/share/icons/hicolor/scalable/apps/voicecraft-studio.svg"
for d in README.md USER_GUIDE.md OBS_GUIDE.md TROUBLESHOOTING.md LICENSES.md; do
  install -D -m 0644 "$ROOT/docs/$d" "$STAGE/usr/share/doc/voicecraft-studio/$d"
done
install -D -m 0644 "$ROOT/resources/51-voicecraft-realtime.rules" \
  "$STAGE/etc/udev/rules.d/51-voicecraft-realtime.rules"

# ---------------- DEBIAN control ----------------
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: voicecraft-studio
Version: $VERSION
Section: sound
Priority: optional
Architecture: amd64
Installed-Size: $(du -sk "$STAGE" | cut -f1)
Maintainer: PSNarayananmc <PSNarayananmc@users.noreply.github.com>
Depends: libqt5core5a (>= 5.12.8), libqt5gui5 (>= 5.12.8), libqt5widgets5 (>= 5.12.8), libpulse0 (>= 13.99), libsamplerate0 (>= 0.1.9), libsndfile1 (>= 1.0.28)
Recommends: pulseaudio | pipewire-pulse, obs-studio
Suggests: gnome-software | packagekit
Homepage: https://github.com/PSNarayananmc/MIC-ONLY
Description: Real-time voice processing studio - podcast-grade microphone for Linux
 VoiceCraft Studio turns a normal microphone into a podcast-grade source
 and integrates seamlessly with OBS Studio via a PulseAudio virtual
 microphone. Full DSP chain: high-pass, RNNoise denoise, noise gate,
 parametric EQ, de-esser, compressor, saturation, BS.1770 loudness and
 true-peak limiter. Measured <30 ms end-to-end latency, fully offline,
 no root required.
 .
 Built for Ubuntu 20.04 LTS (glibc 2.31): links focal's Qt 5.12 / libpulse
 13.99 and statically links libstdc++. Bundles a private RNNoise build
 WITH the official pretrained model.
EOF
sed -e 's/^#!\/bin\/sh$/#!\/bin\/sh/' "$ROOT/resources/deb-postinst" > "$STAGE/DEBIAN/postinst"
chmod 0755 "$STAGE/DEBIAN/postinst"

dpkg-deb --build --root-owner-group "$STAGE" "$DEB"
echo "Built: $DEB"

# ---------------- AppImage ----------------
APPIMAGE_LIB_ROOTS="$FOCAL_ROOT/usr/lib/x86_64-linux-gnu $FOCAL_ROOT/usr/lib/x86_64-linux-gnu/pulseaudio $FOCAL_ROOT/lib/x86_64-linux-gnu $FOCAL_OPT/lib" \
APPIMAGE_PLUGIN_BASES="$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/qt5/plugins" \
APPIMAGE_NAME_SUFFIX="-focal" \
bash "$ROOT/scripts/build_appimage.sh" "$BUILD"

# rename for clarity
if [ -f "$BUILD/voicecraft-studio-focal.AppImage" ]; then
  mv "$BUILD/voicecraft-studio-focal.AppImage" \
     "$BUILD/voicecraft-studio-$VERSION-focal-x86_64.AppImage"
fi
echo "DONE."
ls -lh "$DEB" "$BUILD"/voicecraft-studio-*focal*.AppImage
