#!/usr/bin/env bash
# Cross-compiles the game for 64-bit Windows from Linux.
#
#   scripts/build-windows.sh [path/to/disc.iso]
#
# Needs clang, lld and the llvm tools (llvm-ar, llvm-rc, llvm-mt), CMake and
# Ninja. The first run downloads xwin and uses it to fetch the MSVC C runtime and
# the Windows SDK (accepting Microsoft's license for them) into
# ~/.cache/graw-windows, or $GRAW_WINDOWS_CACHE.
#
# Codegen runs on this machine, so the Linux SDK is built too (scripts/setup-sdk.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/sdk/rexglue-sdk"
HOST_SDK_INSTALL="$SDK/out/install/linux-amd64"
WIN_SDK_BUILD="$SDK/out/build/win-amd64-cross"
WIN_SDK_INSTALL="$SDK/out/install/win-amd64"
BUILD="$ROOT/out/build/win-amd64-release"
TOOLCHAIN="$ROOT/cmake/windows-cross.cmake"

CACHE="${GRAW_WINDOWS_CACHE:-$HOME/.cache/graw-windows}"
XWIN_VERSION=0.10.0
XWIN="$CACHE/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl/xwin"
export XWIN_DIR="$CACHE/sysroot"

for tool in clang clang++ lld-link llvm-ar llvm-ranlib llvm-rc llvm-mt cmake ninja; do
    if ! command -v "$tool" >/dev/null; then
        echo "$tool not found: install clang, lld, llvm, cmake and ninja" >&2
        exit 1
    fi
done

if [ $# -ge 1 ]; then
    python3 "$ROOT/scripts/extract-game-files.py" "$1"
fi
for f in default.xex Yeti_Final_Xenon.exe; do
    if [ ! -f "$ROOT/assets/$f" ]; then
        echo "assets/$f is missing: run scripts/build-windows.sh path/to/your/disc.iso" >&2
        exit 1
    fi
done

if [ ! -f "$HOST_SDK_INSTALL/lib/cmake/rexglue/rexglueConfig.cmake" ] ||
   [ "$(cat "$SDK/.graw-patches" 2>/dev/null)" != \
     "$(cat "$ROOT"/sdk/patches/*.patch | sha256sum | cut -d' ' -f1)" ]; then
    "$ROOT/scripts/setup-sdk.sh"
fi

if [ ! -x "$XWIN" ]; then
    mkdir -p "$CACHE"
    curl -fsSL "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl.tar.gz" |
        tar -xz -C "$CACHE"
fi
if [ ! -d "$XWIN_DIR/crt/include" ]; then
    "$XWIN" --accept-license --arch x86_64 --cache-dir "$CACHE/xwin-cache" splat --output "$XWIN_DIR"
fi

# The Windows SDK build. D3D12 is off: the game's rendering fixes live in the
# Vulkan backend. Tracy's LZ4 needs BMI intrinsics the baseline target lacks.
cmake -S "$SDK" -B "$WIN_SDK_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_INSTALL_PREFIX="$WIN_SDK_INSTALL" \
    -DREXGLUE_USE_VULKAN=ON -DREXGLUE_USE_D3D12=OFF -DREXGLUE_ENABLE_TRACY=OFF >/dev/null
cmake --build "$WIN_SDK_BUILD"
cmake --install "$WIN_SDK_BUILD" >/dev/null

cd "$ROOT"
configure() {
    # shellcheck disable=SC2086
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_PREFIX_PATH="$WIN_SDK_INSTALL" \
        -DREXGLUE_HOST_TOOL="$HOST_SDK_INSTALL/bin/rexglue" ${GRAW_CMAKE_ARGS:-} >/dev/null
}
# The generated sources are only picked up at configure time, so a tree that
# has never run codegen configures again once codegen has written them.
configure
if [ ! -f "$ROOT/generated/default/sources.cmake" ]; then
    cmake --build "$BUILD" --target graw_codegen
    configure
fi
cmake --build "$BUILD"
echo "Built $BUILD/graw.exe"
