#!/bin/bash
# Gyrovault build (WSL). Usage:
#   ./build.sh        release build  -> build/gyrovault.vpk       (tilt only)
#   ./build.sh test   emulator build -> build-test/gyrovault.vpk  (-DGV_TEST_INPUT: d-pad adds tilt)
set -euo pipefail

export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$HOME/tools/cmake-3.30.5-linux-x86_64/bin:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-release}"

case "$MODE" in
  release) BUILD_DIR="$ROOT/build";      TEST_INPUT=OFF ;;
  test)    BUILD_DIR="$ROOT/build-test"; TEST_INPUT=ON ;;
  *) echo "usage: $0 [test]" >&2; exit 2 ;;
esac

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DGV_TEST_INPUT="$TEST_INPUT"
cmake --build "$BUILD_DIR" -j"$(nproc)"

VPK="$BUILD_DIR/gyrovault.vpk"
if [ ! -f "$VPK" ]; then
  echo "ERROR: $VPK was not produced" >&2
  exit 1
fi

echo "VPK:  $VPK"
echo "size: $(stat -c %s "$VPK") bytes"
echo "md5:  $(md5sum "$VPK" | cut -d' ' -f1)"
