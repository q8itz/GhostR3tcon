#!/usr/bin/env bash
# Packs the Windows build into GhostR3tcon-windows-x64.zip. Run
# scripts/build-windows.sh first. Like the AppImage, the zip holds the
# recompiled code and the runtime only: the disc image is chosen in the launcher.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/out/build/win-amd64-release}"
STAGE="$ROOT/packaging/windows/GhostR3tcon"
OUT="$ROOT/GhostR3tcon-windows-x64.zip"

if [ ! -f "$BUILD/graw.exe" ]; then
    echo "$BUILD/graw.exe not found: run scripts/build-windows.sh first" >&2
    exit 1
fi

rm -rf "$(dirname "$STAGE")"
mkdir -p "$STAGE"
# The build folder already holds every DLL graw.exe loads, copied there by the
# SDK's post-build steps.
cp "$BUILD/graw.exe" "$BUILD"/*.dll "$STAGE/"
cat > "$STAGE/README.txt" <<'EOF'
GhostR3tcon - Ghost Recon Advanced Warfighter (Xbox 360) recompiled for Windows

Run graw.exe and choose your own disc image of Tom Clancy's Ghost Recon -
Advanced Warfighter (USA, Europe) in the launcher, which checks it is that
version. No game data is included.

Needs a Vulkan 1.2 GPU driver and the Microsoft Visual C++ Redistributable
(x64): https://aka.ms/vs/17/release/vc_redist.x64.exe

Settings are saved to graw.toml next to graw.exe, so keep this folder somewhere
writable (not Program Files).
EOF
sed -i 's/$/\r/' "$STAGE/README.txt"

if find "$STAGE" \( -iname '*.iso' -o -iname '*.xex' -o -iname '*.big' -o -iname '*.str' \
        -o -iname '*.bik' -o -iname '*.toml' -o \( -iname '*.exe' ! -name graw.exe \) \) -print | grep .; then
    echo "refusing to pack game files or local settings into the zip" >&2
    exit 1
fi

rm -f "$OUT"
(cd "$(dirname "$STAGE")" && python3 - "$OUT" <<'EOF'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for dirpath, _, files in os.walk("GhostR3tcon"):
        for f in sorted(files):
            z.write(os.path.join(dirpath, f))
EOF
)

echo "wrote $OUT ($(du -h "$OUT" | cut -f1))"
find "$STAGE" -type f -printf '%s %p\n' | sort -n
