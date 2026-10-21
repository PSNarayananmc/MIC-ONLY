#!/bin/bash
# focal_deps.sh — build an Ubuntu 20.04 (focal) user-space sysroot from the
# official Ubuntu archive, WITHOUT root, by downloading .deb files and
# extracting them with dpkg -x into $FOCAL_ROOT.
#
# Why: binaries that must RUN on Ubuntu 20.04 (glibc 2.31) have to be
# LINKED against focal's libc/Qt/PulseAudio. Binaries produced on Debian 13
# (glibc 2.41) carry GLIBC_2.34+ symbol requirements and abort on 20.04.
#
# The sysroot is consumed with:  gcc --sysroot=$FOCAL_ROOT  (see
# scripts/build_focal.sh). Focal's own ld/moc binaries run fine on newer
# hosts (glibc is backwards compatible), only not vice versa.
set -e
FOCAL_ROOT="${FOCAL_ROOT:-/home/z/focal}"
F="/home/z/focal-apt"
mkdir -p "$F/lists" "$F/cache/archives/partial"

cat > "$F/focal.list" <<EOF
deb [trusted=yes] http://archive.ubuntu.com/ubuntu focal main universe
deb [trusted=yes] http://archive.ubuntu.com/ubuntu focal-updates main universe
deb [trusted=yes] http://archive.ubuntu.com/ubuntu focal-security main universe
EOF

APT_OPTS=(
  -o Dir::Etc::sourcelist="$F/focal.list"
  -o Dir::Etc::sourceparts=-
  -o Dir::State::Lists="$F/lists"
  # status=/dev/null => apt assumes NOTHING is installed, so the resolved
  # closure includes focal's OWN libc6 etc. (exactly what a sysroot needs)
  -o Dir::State::status=/dev/null
  -o Dir::Cache="$F/cache"
  -o APT::Get::AllowUnauthenticated=true
  -o Acquire::AllowInsecureRepositories=true
)

echo "=== [1/3] apt update (focal lists only, isolated from host apt) ==="
apt-get "${APT_OPTS[@]}" update -qq

echo "=== [2/3] Resolving + downloading focal package closure ==="
ROOT_PKGS=(libc6-dev linux-libc-dev qtbase5-dev qtbase5-dev-tools
           libpulse-dev libsamplerate0-dev libsndfile1-dev
           libgl1-mesa-dev libglib2.0-0
           # runtime libs pulled in transitively by libpulse (libsystemd ->
           # libgcrypt/liblz4/liblzma) and libwrap0; without them ld walks
           # into HOST library paths (LIBRARY_PATH leftovers) and mixes
           # glibc-2.41 objects into a focal build
           libwrap0 libgcrypt20 liblz4-1 libgpg-error0 liblzma5)
ALL=""
for p in "${ROOT_PKGS[@]}"; do
  deps=$(apt-cache "${APT_OPTS[@]}" depends --recurse --no-recommends \
    --no-suggests --no-conflicts --no-breaks --no-replaces \
    --no-enhances --no-pre-depends "$p" 2>/dev/null | grep "^\w" | sort -u)
  ALL="$ALL $deps"
done
ALL=$(echo $ALL | tr ' ' '\n' | sort -u | grep -v '^$')
echo "Closure size: $(echo "$ALL" | wc -w) packages"
# NOTE: apt-get download always writes to the CURRENT directory, so cd into
# the cache dir first (learnt the hard way: 244 .deb files in the repo root).
cd "$F/cache/archives"
for p in $ALL; do
  if ls ${p}_*.deb >/dev/null 2>&1; then continue; fi
  apt-get "${APT_OPTS[@]}" download -qq "$p" 2>/dev/null || \
    echo "WARN: failed to download $p"
done
cd - >/dev/null

echo "=== [3/3] Extracting into $FOCAL_ROOT ==="
mkdir -p "$FOCAL_ROOT"
n=0
for f in "$F"/cache/archives/*.deb; do
  dpkg -x "$f" "$FOCAL_ROOT" || echo "WARN: failed to extract $f"
  n=$((n+1))
done
echo "Extracted $n packages into $FOCAL_ROOT"

# --- sanity checks ----------------------------------------------------------
check() { [ -e "$1" ] && echo "OK  $1" || echo "MISS $1"; }
check "$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/libc.so"
check "$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/crt1.o"
check "$FOCAL_ROOT/usr/include/pulse/pulseaudio.h"
check "$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/libQt5Core.so"
check "$FOCAL_ROOT/usr/include/x86_64-linux-gnu/qt5/QtCore/QtCore"
check "$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/libsndfile.so"
check "$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/qt5/bin/moc"
check "$FOCAL_ROOT/usr/include/GL/gl.h"

# focal's Qt5GuiConfigExtras.cmake hardcodes the BUILD-TIME absolute paths of
# libEGL/libGL (/usr/lib/x86_64-linux-gnu/...) and verifies their existence;
# outside the sysroot these don't exist -> find_package(Qt5) fails. Rewrite
# them into the sysroot.
GUI_EXTRAS="$FOCAL_ROOT/usr/lib/x86_64-linux-gnu/cmake/Qt5Gui/Qt5GuiConfigExtras.cmake"
if [ -f "$GUI_EXTRAS" ]; then
  sed -i 's|"/usr/lib/x86_64-linux-gnu/|"'$FOCAL_ROOT'/usr/lib/x86_64-linux-gnu/|g' "$GUI_EXTRAS"
  echo "Patched Qt5GuiConfigExtras.cmake paths into sysroot"
fi

# --- CRITICAL: repair absolute symlinks -------------------------------------
# focal debs ship dev symlinks with ABSOLUTE targets (/lib/x86_64-linux-gnu/
# libpthread.so.0 etc.). Valid on a real focal system, but in an extracted
# sysroot ld follows them at FILESYSTEM level (unlike linker scripts, which
# ld sysroot-prefixes) straight into the HOST /lib — e.g. -lpthread then
# links the host's glibc-2.41 pthread STUB and pthread_create is undefined.
# Map every absolute target into the sysroot and make it relative.
echo "=== Repairing absolute symlinks inside the sysroot ==="
python3 - "$FOCAL_ROOT" <<'PYEOF'
import os, sys
root = os.path.abspath(sys.argv[1])
fixed = missing = 0
for base in ("usr/lib/x86_64-linux-gnu", "lib/x86_64-linux-gnu"):
    d = os.path.join(root, base)
    if not os.path.isdir(d):
        continue
    for name in os.listdir(d):
        p = os.path.join(d, name)
        if not os.path.islink(p):
            continue
        t = os.readlink(p)
        if t.startswith("/"):
            inner = os.path.join(root, t.lstrip("/"))
            if os.path.exists(inner):
                rel = os.path.relpath(inner, os.path.dirname(p))
                os.unlink(p)
                os.symlink(rel, p)
                fixed += 1
            else:
                missing += 1
                print(f"  UNRESOLVED: {t} (no sysroot file)")
print(f"symlinks repaired: {fixed}, unresolved: {missing}")
if missing:
    print("WARNING: some targets are not in the sysroot - link errors possible")
PYEOF

echo "Qt5Core glibc deps (informational):"
objdump -T "$FOCAL_ROOT"/usr/lib/x86_64-linux-gnu/libQt5Core.so.5 2>/dev/null | \
  grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1
ls "$FOCAL_ROOT"/usr/lib/x86_64-linux-gnu/pulseaudio/ 2>/dev/null | head -3
