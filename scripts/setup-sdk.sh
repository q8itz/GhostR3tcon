#!/usr/bin/env bash
# Checks out the upstream ReXGlue SDK at the release this project is based on,
# applies the patches in sdk/patches, then builds and installs it into
# sdk/rexglue-sdk/out/install/linux-amd64.
#
# Extra CMake arguments for the SDK (for example a CMAKE_PREFIX_PATH holding
# development headers the system lacks) can be passed in SDK_CMAKE_ARGS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/sdk/rexglue-sdk"
UPSTREAM="https://github.com/rexglue/rexglue-sdk.git"
# Tag v0.10.0.
BASE="c94f5ebdcb3c9d1a460ca48e04f9758448f8d518"
STAMP="$SDK/.graw-patches"

patch_hash() {
    cat "$ROOT"/sdk/patches/*.patch | sha256sum | cut -d' ' -f1
}

if [ ! -d "$SDK/.git" ]; then
    git clone "$UPSTREAM" "$SDK"
fi

cd "$SDK"
if [ "$(cat "$STAMP" 2>/dev/null)" != "$(patch_hash)" ]; then
    git fetch -q origin "$BASE" 2>/dev/null || git fetch -q origin
    git checkout -q -f -B graw "$BASE"
    git -c user.name="graw" -c user.email="graw@localhost" am -q "$ROOT"/sdk/patches/*.patch
    patch_hash > "$STAMP"
fi
git submodule update --init --recursive --jobs 8

# shellcheck disable=SC2086
cmake --preset linux-amd64 ${SDK_CMAKE_ARGS:-}
cmake --build out/build/linux-amd64 --config Release
cmake --install out/build/linux-amd64 --config Release >/dev/null
echo "SDK installed in $SDK/out/install/linux-amd64"
