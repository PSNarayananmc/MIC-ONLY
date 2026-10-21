#!/bin/bash
# Build RNNoise 0.2 (Xiph, BSD) WITH the official pretrained model into ~/opt
#
# IMPORTANT HISTORY (2026-10-20):
#  * The GitHub v0.2 tarball does NOT contain the trained model files
#    (src/rnnoise_data.c/h — ~29 MB of weights). Building without them
#    produces a library whose rnnoise_create(NULL) barely processes audio
#    (measured 0.1 dB suppression on white noise).
#  * Debian/Ubuntu's librnnoise0 0.2+ds package ALSO ships without usable
#    default weights (rnnoise_create(NULL) ≈ no-op, measured).
#  * The official weights live at:
#      https://media.xiph.org/rnnoise/models/rnnoise_data-<version>.tar.gz
#    where <version> = content of the model_version file (0b50c45).
#
# This script downloads the model, compiles the full source list and
# installs static + shared libraries plus headers into $PREFIX.
set -e
VERSION=0.2
WORK=/home/z/my-project/thirdparty
PREFIX="$HOME/opt"

mkdir -p "$WORK"
cd "$WORK"

if [ ! -f rnnoise-$VERSION.tar.gz ]; then
  echo "Downloading rnnoise-$VERSION ..."
  curl -sL -o rnnoise-$VERSION.tar.gz \
    https://github.com/xiph/rnnoise/archive/refs/tags/v$VERSION.tar.gz
fi
rm -rf rnnoise-$VERSION
tar xzf rnnoise-$VERSION.tar.gz
cd rnnoise-$VERSION

MODEL_VER=$(cat model_version)
if [ ! -f src/rnnoise_data.c ]; then
  echo "Downloading pretrained model rnnoise_data-$MODEL_VER ..."
  curl -sL -o /tmp/rnnoise_data-$MODEL_VER.tar.gz \
    "https://media.xiph.org/rnnoise/models/rnnoise_data-$MODEL_VER.tar.gz"
  tar xzf /tmp/rnnoise_data-$MODEL_VER.tar.gz src/rnnoise_data.c src/rnnoise_data.h
  # tarball paths are src/... which lands in the right place already
fi

# Full official source list (see Makefile.am RNNOISE_SOURCES).
# NOTE: parse_lpcnet_weights.c IS part of the library — it defines the
# linear_init/conv2d_init/gru_init helpers that rnnoise_data.c's model
# loader calls. Excluded: dump_features.c, dump_rnnoise_tables.c,
# write_weights.c (training/dev tools, contain main()).
SOURCES="src/denoise.c src/rnn.c src/pitch.c src/kiss_fft.c src/celt_lpc.c \
         src/nnet.c src/nnet_default.c src/parse_lpcnet_weights.c \
         src/rnnoise_data.c src/rnnoise_tables.c"

CFLAGS="-O2 -fPIC -DNDEBUG"
mkdir -p build-objs
# compile with hard failures (the old script swallowed errors with || true)
for f in $SOURCES; do
  gcc -c $CFLAGS "$f" -Iinclude -Isrc -o "build-objs/$(basename ${f%.c}).o"
done

rm -f build-objs/main.o   # safety: no dev tool object may slip in
if nm build-objs/*.o | grep -q " T main"; then
  echo "ERROR: a main() slipped into the library objects" >&2
  exit 1
fi

ar rcs librnnoise.a build-objs/*.o
gcc -shared -o librnnoise.so.0.4.1 -Wl,-soname,librnnoise.so.0 \
    build-objs/*.o -lm

mkdir -p "$PREFIX/lib" "$PREFIX/lib/pkgconfig" "$PREFIX/include"
cp librnnoise.a librnnoise.so.0.4.1 "$PREFIX/lib/"
ln -sf librnnoise.so.0.4.1 "$PREFIX/lib/librnnoise.so.0"
ln -sf librnnoise.so.0.4.1 "$PREFIX/lib/librnnoise.so"
cp include/rnnoise.h "$PREFIX/include/"

cat > "$PREFIX/lib/pkgconfig/rnnoise.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: rnnoise
Description: Recurrent neural network for audio noise reduction
Version: 0.2
Libs: -L\${libdir} -lrnnoise
Cflags: -I\${includedir}
EOF

echo "RNNoise 0.2 (with pretrained model) installed into $PREFIX"
echo "Symbols exported:"
nm -D "$PREFIX/lib/librnnoise.so.0.4.1" | grep " T rnnoise_" | head
