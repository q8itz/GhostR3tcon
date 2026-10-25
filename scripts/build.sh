#!/usr/bin/env bash
# Builds the SDK (if needed) and the game.
#
#   scripts/build.sh [path/to/disc.iso]
#
# The disc image is only needed the first time, to copy the two executables
# codegen reads into assets/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_INSTALL="$ROOT/sdk/rexglue-sdk/out/install/linux-amd64"
BUILD="$ROOT/out/build/linux-amd64-release"

if [ $# -ge 1 ]; then
    python3 "$ROOT/scripts/extract-game-files.py" "$1"
fi
for f in default.xex Yeti_Final_Xenon.exe; do
    if [ ! -f "$ROOT/assets/$f" ]; then
        echo "assets/$f is missing: run scripts/build.sh path/to/your/disc.iso" >&2
        exit 1
    fi
done

if [ ! -f "$SDK_INSTALL/lib/cmake/rexglue/rexglueConfig.cmake" ] ||
   [ "$(cat "$ROOT/sdk/rexglue-sdk/.graw-patches" 2>/dev/null)" != \
     "$(cat "$ROOT"/sdk/patches/*.patch | sha256sum | cut -d' ' -f1)" ]; then
    "$ROOT/scripts/setup-sdk.sh"
fi

cd "$ROOT"
configure() {
    # shellcheck disable=SC2086
    cmake --preset linux-amd64-release -DCMAKE_PREFIX_PATH="$SDK_INSTALL" \
        ${GRAW_CMAKE_ARGS:-} >/dev/null
}
# The generated sources are only picked up at configure time, so a tree that
# has never run codegen configures again once codegen has written them.
configure
if [ ! -f "$ROOT/generated/default/sources.cmake" ]; then
    cmake --build "$BUILD" --target graw_codegen
    configure
fi
cmake --build "$BUILD"
echo "Built $BUILD/graw"
