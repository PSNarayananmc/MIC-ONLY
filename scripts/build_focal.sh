#!/bin/bash
# build_focal.sh — build + test + package VoiceCraft Studio FOR
# Ubuntu 20.04 LTS (glibc 2.31) from a newer host, without root:
#
#   1. scripts/focal_deps.sh          -> /home/z/focal sysroot (focal debs)
#   2. scripts/build_rnnoise_focal.sh -> /home/z/focal-opt (weighted RNNoise)
#   3. this script: cmake --sysroot build, glibc-ceiling verification,
#      ctest run, .deb (dpkg-deb) and AppImage packaging.
#
# The resulting binary links ONLY against focal libraries (Qt 5.12.8,
# PulseAudio 13.99, glibc 2.31) and statically links libstdc++/libgcc, so it
# RUNS on Ubuntu 20.04. A binary can always run on glibc >= the one it was
# linked against, so these artifacts also run on 22.04/24.04.
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FOCAL_ROOT="${FOCAL_ROOT:-/home/z/focal}"
FOCAL_OPT="${FOCAL_OPT:-/home/z/focal-opt}"
BUILD="$ROOT/build-focal"
VERSION=1.0.0

# Host build tools (cmake/ctest from the user-space prefix): source env.sh so
# cmake can load its own libs and is on PATH. We then OVERRIDE the library
# resolution below to the FOCAL sysroot — deliberate and important.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"
unset CMAKE_PREFIX_PATH   # must not let find_package(Qt5) see host ~/opt Qt
unset LIBRARY_PATH        # gcc link search must stay INSIDE the focal sysroot

# --- focal compiler toolchain (see scripts/focal_gcc.sh for rationale) ------
# Host GCC-14 libstdc++ needs glibc >= 2.32 symbols (__libc_single_threaded),
# so ALL C++ compilation uses focal's own g++-10, relocated into user space.
FOCAL_GCC="${FOCAL_GCC:-/home/z/focal-gcc}"
FOCAL_GCC_RT="${FOCAL_GCC_RT:-/home/z/focal-gcc-rt}"
CC10="$FOCAL_GCC/usr/bin/x86_64-linux-gnu-gcc-10"
CXX10="$FOCAL_GCC/usr/bin/x86_64-linux-gnu-g++-10"
[ -x "$CXX10" ] || { echo "focal g++-10 missing - run scripts/focal_gcc.sh first"; exit 1; }
export GCC_EXEC_PREFIX="$FOCAL_GCC/usr/lib/gcc/"
# compiler support libs first (isl/mpc/mpfr/gmp), then focal runtime keep-list
export LD_LIBRARY_PATH="$FOCAL_GCC_RT:$LD_LIBRARY_PATH"

# pkg-config must resolve FOCAL pc files (Qt 5.12.8, pulse 13.99) + our
# private weighted RNNoise; NEVER the host's.
export PKG_CONFIG_PATH="$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$FOCAL_ROOT/usr/share/pkgconfig:$FOCAL_ROOT/usr/lib/pkgconfig:$FOCAL_OPT/lib/pkgconfig"
# Runtime library dir for RUNTIME on this host: focal distro libs (Qt 5.12,
# pulse 13.99, sndfile...) run fine here because glibc is backwards
# compatible. We CANNOT expose the whole sysroot lib dir: it contains older
# versions of libs the HOST TOOLING links against (libstdc++, libpkgconf,
# libarchive, ...), shadowing the host's and breaking cmake/pkg-config. So
# build a KEEP-LIST dir with exactly the libraries the APP needs.
FOCAL_RT=/home/z/focal-rt
mkdir -p "$FOCAL_RT"
for d in "$FOCAL_ROOT/usr/lib/x86_64-linux-gnu" "$FOCAL_ROOT/lib/x86_64-linux-gnu" \
         "$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/pulseaudio"; do
  [ -d "$d" ] || continue
  for f in "$d"/*.so*; do
    [ -e "$f" ] || continue
    b=$(basename "$f")
    # NOTE: the libsystemd chain + libwrap MUST shadow ~/opt's versions
    # (same sonames, built for glibc 2.33+): ld consults LD_LIBRARY_PATH
    # before the sysroot -L dirs.
    case "$b" in
      libQt5*|libdouble-conversion*|libmd4c*|libpulse*|libsndfile*|\
      libFLAC*|libopus*|libvorbis*|libogg*|libsamplerate*|\
      libglib-2.0*|libpcre2-8*|libffi*|\
      libwrap*|libsystemd*|libgcrypt*|libgpg-error*|liblz4*|liblzma*|libicu*|\
      libpcre*)
        ln -sf "$f" "$FOCAL_RT/$b" ;;
    esac
  done
done
# IMPORTANT: GNU ld consults LD_LIBRARY_PATH at LINK time — any host library
# dir in it leaks glibc-2.41 objects into the focal link (real bug: focal
# libpulse needed libwrap; ld found the HOST ~/opt libwrap built against
# glibc 2.33 and failed). So each phase gets a MINIMAL environment:
#   configure : focal-gcc-rt (compiler libs) + ~/opt (cmake binary's own libs)
#   build     : focal-gcc-rt ONLY — linker must stay inside the sysroot
#   test      : focal runtime keep-list dirs (focal Qt/pulse/etc. on host)
HOST_LIBS="$HOME/opt/usr/lib/x86_64-linux-gnu:$HOME/opt/lib"
export LD_LIBRARY_PATH="$FOCAL_GCC_RT:$HOST_LIBS"
export QT_PLUGIN_PATH="$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/qt5/plugins"
export QT_QPA_PLATFORM=offscreen

echo "=== [1/5] CMake configure (cross to focal sysroot) ==="
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
  -DCMAKE_SYSROOT="$FOCAL_ROOT" \
  -DCMAKE_FIND_ROOT_PATH="$FOCAL_ROOT" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_C_COMPILER="$CC10" \
  -DCMAKE_CXX_COMPILER="$CXX10" \
  -DCMAKE_EXE_LINKER_FLAGS="--sysroot=$FOCAL_ROOT -L$FOCAL_ROOT/usr/lib/x86_64-linux-gnu -L$FOCAL_ROOT/lib/x86_64-linux-gnu" \
  -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib/voicecraft-studio' \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON

echo "=== [2/5] Build ==="
(
  # focal-gcc-rt: compiler support libs; focal-rt: focal copies of every
  # soname that ALSO exists in ~/opt (Qt5, pulse, sndfile, libwrap, ...)
  # so ld resolves them to FOCAL objects before ever seeing host dirs;
  # ~/opt last so the cmake binary itself can still load (librhash etc.)
  export LD_LIBRARY_PATH="$FOCAL_GCC_RT:$FOCAL_RT:$FOCAL_OPT/lib:$HOST_LIBS"
  cmake --build "$BUILD" -j2
)

echo "=== [3/5] glibc ceiling verification (must be <= 2.31) ==="
BIN="$BUILD/voicecraft-studio"
MAX=$(objdump -T "$BIN" | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)
echo "voicecraft-studio max glibc symbol: $MAX"
need_vers() { # needs $1 <= 2.31
  [ "$(printf '%s\n' "2.31" "$1" | sort -V | head -1)" = "$1" ] \
    && echo "OK   $1 <= 2.31" || { echo "FAIL $1 > 2.31"; exit 1; }
}
need_vers "${MAX#GLIBC_}"

echo "=== [4/5] ctest with focal binaries ==="
cd "$BUILD"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdgrt}"
(
  # focal runtime libs first (Qt 5.12, pulse 13.99), then host dirs so the
  # ctest binary itself can load its own ~/opt libs
  export LD_LIBRARY_PATH="$FOCAL_RT:$FOCAL_OPT/lib:$FOCAL_GCC_RT:$HOST_LIBS"
  ctest --output-on-failure
)

echo "=== [5/5] Packaging ==="
bash "$ROOT/scripts/package_focal.sh" "$BUILD" "$VERSION"
