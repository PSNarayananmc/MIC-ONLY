#!/bin/bash
# build_rnnoise_focal.sh — build RNNoise 0.2 WITH the official pretrained
# model, TARGETING Ubuntu 20.04 (glibc <= 2.31), by compiling with the host
# gcc but --sysroot=/home/z/focal (focal libc/headers).
#
# Installs into $FOCAL_OPT (default /home/z/focal-opt) with a pkg-config
# file so the focal build of VoiceCraft Studio links THIS library.
#
# Why bundle our own: distro librnnoise0 (focal included) ships without
# usable pretrained weights; rnnoise_create(nullptr) is then a near no-op
# (measured 0.1 dB suppression). VoiceCraft Studio calls rnnoise_create(),
# so the 20.04 artifacts bundle THIS library privately.
set -e
VERSION=0.2
WORK=/home/z/my-project/thirdparty
FOCAL_ROOT="${FOCAL_ROOT:-/home/z/focal}"
FOCAL_OPT="${FOCAL_OPT:-/home/z/focal-opt}"
SYSROOT_CFLAGS="--sysroot=$FOCAL_ROOT -L$FOCAL_ROOT/usr/lib/x86_64-linux-gnu -L$FOCAL_ROOT/lib/x86_64-linux-gnu"

mkdir -p "$WORK"
cd "$WORK"

if [ ! -f rnnoise-$VERSION.tar.gz ]; then
  echo "Downloading rnnoise-$VERSION ..."
  curl -sL -o rnnoise-$VERSION.tar.gz \
    https://github.com/xiph/rnnoise/archive/refs/tags/v$VERSION.tar.gz
fi
rm -rf rnnoise-$VERSION-focal
mkdir rnnoise-$VERSION-focal
tar xzf rnnoise-$VERSION.tar.gz -C rnnoise-$VERSION-focal --strip-components=1
cd rnnoise-$VERSION-focal

MODEL_VER=$(cat model_version)
if [ ! -f src/rnnoise_data.c ]; then
  echo "Downloading pretrained model rnnoise_data-$MODEL_VER ..."
  curl -sL -o /tmp/rnnoise_data-$MODEL_VER.tar.gz \
    "https://media.xiph.org/rnnoise/models/rnnoise_data-$MODEL_VER.tar.gz"
  tar xzf /tmp/rnnoise_data-$MODEL_VER.tar.gz src/rnnoise_data.c src/rnnoise_data.h
fi

SOURCES="src/denoise.c src/rnn.c src/pitch.c src/kiss_fft.c src/celt_lpc.c \
         src/nnet.c src/nnet_default.c src/parse_lpcnet_weights.c \
         src/rnnoise_data.c src/rnnoise_tables.c"

CFLAGS="-O2 -fPIC -DNDEBUG"
mkdir -p build-objs
for f in $SOURCES; do
  gcc $SYSROOT_CFLAGS -c $CFLAGS "$f" -Iinclude -Isrc -o "build-objs/$(basename ${f%.c}).o"
done

rm -f build-objs/main.o
if nm build-objs/*.o | grep -q " T main"; then
  echo "ERROR: a main() slipped into the library objects" >&2
  exit 1
fi

gcc $SYSROOT_CFLAGS -shared -o librnnoise.so.0.4.1 -Wl,-soname,librnnoise.so.0 \
    build-objs/*.o -lm

mkdir -p "$FOCAL_OPT/lib" "$FOCAL_OPT/lib/pkgconfig" "$FOCAL_OPT/include"
cp librnnoise.so.0.4.1 "$FOCAL_OPT/lib/"
ln -sf librnnoise.so.0.4.1 "$FOCAL_OPT/lib/librnnoise.so.0"
ln -sf librnnoise.so.0.4.1 "$FOCAL_OPT/lib/librnnoise.so"
cp include/rnnoise.h "$FOCAL_OPT/include/"

cat > "$FOCAL_OPT/lib/pkgconfig/rnnoise.pc" <<EOF
prefix=$FOCAL_OPT
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: rnnoise
Description: RNNoise for Ubuntu 20.04 (official pretrained weights)
Version: 0.2
Libs: -L\${libdir} -lrnnoise
Cflags: -I\${includedir}
EOF

echo "RNNoise (focal) installed into $FOCAL_OPT"
echo "Max glibc symbol required:"
objdump -T "$FOCAL_OPT/lib/librnnoise.so.0.4.1" | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1
