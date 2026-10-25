#!/usr/bin/env bash
# Packs the built game into GhostR3tcon-x86_64.AppImage. Run scripts/build.sh
# first. The AppImage holds the recompiled code and the runtime only: the disc
# image is chosen in the launcher and read from wherever it is on disk.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/out/build/linux-amd64-release}"
SDK_LIB="${SDK_LIB:-$ROOT/sdk/rexglue-sdk/out/install/linux-amd64/lib}"
STAGE="$ROOT/packaging/AppDir"
CACHE="$ROOT/packaging/.cache"
RUNTIME="$CACHE/runtime-x86_64"
OUT="$ROOT/GhostR3tcon-x86_64.AppImage"

if [ ! -x "$BUILD/graw" ]; then
    echo "$BUILD/graw not found: run scripts/build.sh first" >&2
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/usr/bin" "$CACHE"
# The executable, the recompiled modules and the GPU plugin load each other
# from their own folder ($ORIGIN), so they all go in usr/bin.
cp "$BUILD/graw" "$STAGE/usr/bin/"
for lib in "$BUILD"/libgraw_*.so; do
    cp "$lib" "$STAGE/usr/bin/"
done
cp -L "$SDK_LIB/librexruntime.so" "$SDK_LIB/librexgpu-xenos.so" "$STAGE/usr/bin/"
strip --strip-unneeded "$STAGE"/usr/bin/*
chmod 755 "$STAGE"/usr/bin/*

cp "$ROOT/packaging/AppRun" "$STAGE/AppRun"
chmod 755 "$STAGE/AppRun"
cp "$ROOT/packaging/ghostr3tcon.desktop" "$STAGE/"
python3 "$ROOT/packaging/make_icon.py" "$STAGE/ghostr3tcon.png"
ln -s ghostr3tcon.png "$STAGE/.DirIcon"

if find "$STAGE" \( -iname '*.iso' -o -iname '*.xex' -o -iname '*.exe' -o -iname '*.big' \
        -o -iname '*.str' -o -iname '*.bik' -o -iname '*.toml' \) -print | grep .; then
    echo "refusing to pack game files or local settings into the AppImage" >&2
    exit 1
fi

if [ ! -s "$RUNTIME" ]; then
    curl -fsSL -o "$RUNTIME" \
        https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
    chmod 755 "$RUNTIME"
fi

SQUASH="$CACHE/image.squashfs"
rm -f "$SQUASH"
mksquashfs "$STAGE" "$SQUASH" -root-owned -noappend -comp zstd >/dev/null
cat "$RUNTIME" "$SQUASH" > "$OUT"
chmod 755 "$OUT"

echo "wrote $OUT ($(du -h "$OUT" | cut -f1))"
find "$STAGE" -type f -printf '%s %p\n' | sort -n
