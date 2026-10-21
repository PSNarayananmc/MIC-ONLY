#!/bin/bash
# focal_gcc.sh — set up Ubuntu 20.04's OWN compiler (g++-10) in user space.
#
# WHY: GCC-14's libstdc++ (host) references __libc_single_threaded and other
# symbols added in glibc >= 2.32, so code linked with it cannot run on focal
# (glibc 2.31) — statically OR bundled dynamically (host libstdc++.so.6 needs
# GLIBC up to 2.38). The only clean way to emit glibc-2.31-compatible code is
# to compile with focal's g++-10 and link focal's libstdc++ (GLIBCXX_3.4.28).
#
# Layout:
#   /home/z/focal-gcc            gcc-10/g++-10/c++ headers (GCC_EXEC_PREFIX)
#   /home/z/focal-gcc-rt         keep-list of compiler support libs (isl/mpc/
#                                mpfr/gmp) — NEVER shadows host libstdc++
#   /home/z/focal                (sysroot; extended with focal libstdc++ dev)
set -e
FOCAL_ROOT="${FOCAL_ROOT:-/home/z/focal}"
FOCAL_GCC="${FOCAL_GCC:-/home/z/focal-gcc}"
FOCAL_GCC_RT="${FOCAL_GCC_RT:-/home/z/focal-gcc-rt}"
F="/home/z/focal-apt"

APT_OPTS=(
  -o Dir::Etc::sourcelist="$F/focal.list"
  -o Dir::Etc::sourceparts=-
  -o Dir::State::Lists="$F/lists"
  -o Dir::State::status=/dev/null
  -o Dir::Cache="$F/cache"
  -o APT::Get::AllowUnauthenticated=true
)

echo "=== [1/3] Downloading focal compiler toolchain ==="
cd "$F/cache/archives"
PKGS=(gcc-10 g++-10 cpp-10 gcc-10-base libgcc-10-dev libstdc++-10-dev
      libgcc-s1 libstdc++6 libisl22 libmpc3 libmpfr6 libgmp10)
for p in "${PKGS[@]}"; do
  ls ${p}_*.deb >/dev/null 2>&1 || apt-get "${APT_OPTS[@]}" download -qq "$p" || \
    echo "WARN: failed to download $p"
done
cd - >/dev/null

echo "=== [2/3] Extracting ==="
mkdir -p "$FOCAL_GCC"
for p in gcc-10 g++-10 cpp-10 gcc-10-base libgcc-10-dev libstdc++-10-dev \
         libisl23 libmpc3 libmpfr6 libgmp10; do
  for f in "$F"/cache/archives/${p}_*.deb; do
    [ -e "$f" ] || continue
    dpkg -x "$f" "$FOCAL_GCC"
  done
done
# linker-facing libraries must ALSO be in the sysroot so --sysroot linking
# finds focal's libstdc++.so and libgcc
for p in libstdc++-10-dev libgcc-10-dev libgcc-s1 libstdc++6; do
  for f in "$F"/cache/archives/${p}_*.deb; do
    [ -e "$f" ] || continue
    dpkg -x "$f" "$FOCAL_ROOT"
  done
done

# compiler support libs, keep-list style (see focal_deps.sh reasoning)
mkdir -p "$FOCAL_GCC_RT"
for d in "$FOCAL_GCC/usr/lib/x86_64-linux-gnu"; do
  for f in "$d"/*.so*; do
    [ -e "$f" ] || continue
    b=$(basename "$f")
    case "$b" in
      libisl*|libmpc*|libmpfr*|libgmp*) ln -sf "$f" "$FOCAL_GCC_RT/$b" ;;
    esac
  done
done

# same absolute-symlink repair as the sysroot (focal debs ship /lib/... targets)
python3 - "$FOCAL_GCC" <<'PYEOF'
import os, sys
root = os.path.abspath(sys.argv[1])
fixed = 0
for dirpath, _, _ in os.walk(root):
    for name in os.listdir(dirpath):
        p = os.path.join(dirpath, name)
        if not os.path.islink(p):
            continue
        t = os.readlink(p)
        if t.startswith("/"):
            inner = os.path.join(root, t.lstrip("/"))
            if os.path.exists(inner):
                rel = os.path.relpath(inner, os.path.dirname(p))
                os.unlink(p); os.symlink(rel, p); fixed += 1
print(f"focal-gcc symlinks repaired: {fixed}")
PYEOF

echo "=== [3/3] Verification ==="
export GCC_EXEC_PREFIX="$FOCAL_GCC/usr/lib/gcc/"
export LD_LIBRARY_PATH="$FOCAL_GCC_RT:$LD_LIBRARY_PATH"
echo 'int main(){return 0;}' | "$FOCAL_GCC/usr/bin/x86_64-linux-gnu-g++-10" \
  --sysroot="$FOCAL_ROOT" -x c++ - -o /tmp/focal-gxx-test 2>&1 | head -5
/tmp/focal-gxx-test && echo "OK: trivial C++ program built with focal g++-10 and ran"
"$FOCAL_GCC/usr/bin/x86_64-linux-gnu-g++-10" --version | head -1
objdump -T /tmp/focal-gxx-test | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1
