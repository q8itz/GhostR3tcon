# GhostR3tcon

A native Linux and Windows build of **Tom Clancy's Ghost Recon Advanced Warfighter** (Xbox 360),
made with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) static recompiler.

This is a passion project and was built entirely through AI (Grok and Claude).

** Multi-player is not contained in this release. The game entry-point is the single player segment only.

This repository contains no game data. It is your responsibility to dump the game from your own physical copy. To build or play you need your own copy of
the game as a disc image: *Tom Clancy's Ghost Recon - Advanced Warfighter (USA, Europe)
(En,Fr,De,Es,It)*, disc US-2005. Any full dump of that disc works; the build script and
the launcher check its two executables (`default.xex` and `Yeti_Final_Xenon.exe`) by SHA-1,
so other versions and regions are rejected with a clear message.

Known Issues:
- Higher resolutions introduce texture issues in certain circumstances
- Higher than default FPS + higher than default resolutions can cause stutter

## What's here

| Path | Contents |
| --- | --- |
| `src/` | The app: launcher, in-game menu, settings and startup hooks |
| `graw_manifest.toml`, `*_hints.toml` | Codegen input: which executables to recompile and fixes for what the analyser misses |
| `sdk/patches/` | Changes to ReXGlue SDK v0.10.0 that this game needs |
| `scripts/` | SDK setup, game-file extraction and the Linux and Windows builds |
| `cmake/` | Toolchain for cross-compiling the Windows build from Linux |
| `packaging/` | AppImage and Windows zip packaging |

## Building

You need clang 18 or newer, CMake 3.25+, Ninja, git, Python 3, and the development
packages for X11, Wayland, ALSA, PulseAudio and PipeWire. On Debian or Ubuntu:

```sh
sudo apt install clang cmake ninja-build git python3 squashfs-tools \
    libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxss-dev libxtst-dev libxfixes-dev libxkbcommon-dev libwayland-dev \
    libasound2-dev libpulse-dev libpipewire-0.3-dev libffi-dev
```

Then build, pointing the script at your disc image the first time:

```sh
scripts/build.sh "/path/to/Ghost Recon Advanced Warfighter.iso"
```

This will:

1. Copy `default.xex` and `Yeti_Final_Xenon.exe` from the disc into `assets/` and check them against the supported release.
2. Clone the ReXGlue SDK at v0.10.0 into `sdk/rexglue-sdk`, apply `sdk/patches`, then build and install it.
3. Recompile the two executables into C++ under `generated/` and build the game.

`assets/`, `generated/`, `sdk/rexglue-sdk/` and `out/` are git-ignored. The game ends up
in `out/build/linux-amd64-release/graw`. Later builds only need `scripts/build.sh`.

If some development headers live outside the system paths, pass them to the SDK build
with `SDK_CMAKE_ARGS="-DCMAKE_PREFIX_PATH=/your/prefix"`.

## AppImage

```sh
packaging/build-appimage.sh
```

This writes `GhostR3tcon-x86_64.AppImage`, containing the recompiled code and the
runtime. It holds no disc data, and the launcher asks for the disc image on first run.
Settings are saved to `~/.config/graw/graw.toml`; saves and the shader cache go to
`~/.local/share/graw`.

## Windows

The Windows build is cross-compiled from Linux with clang. On top of the Linux build's
requirements you need lld and the LLVM tools (`sudo apt install lld llvm`). Then:

```sh
scripts/build-windows.sh "/path/to/Ghost Recon Advanced Warfighter.iso"
packaging/build-windows-zip.sh
```

The first run downloads [xwin](https://github.com/Jake-Shadle/xwin) and uses it to fetch
the MSVC C runtime and the Windows SDK into `~/.cache/graw-windows` (set `GRAW_WINDOWS_CACHE`
to put them elsewhere); doing so accepts Microsoft's license for them. The Linux SDK is
built as well, because codegen runs on the build machine. The game ends up in
`out/build/win-amd64-release`, and the zip script packs it into `GhostR3tcon-windows-x64.zip`.

The Windows build uses the Vulkan renderer, as on Linux. Players need a Vulkan driver and the
[Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).
Settings are saved to `graw.toml` next to `graw.exe`, so the folder must be writable; saves
and the shader cache go to `Documents\graw`. It also runs under Wine with a Vulkan driver.

## Playing

* The launcher picks the disc image (its executables are checked against the supported release, which takes a second), the save folder, and display and shader options.
* In game, open the menu with **Guide**, **Back + Start** or **F6**. Use the D-pad or stick to move, **A** to select and **B** to go back.
* Performance: the game holds 60 FPS at the native 1280x720 render resolution, but struggles
  at higher ones. Emulating the Xbox 360's graphics chip adds a large fixed GPU cost to every
  frame, and the extra pixels of 2x and above push it over the 60 FPS budget, even on a
  GeForce RTX 4070 Super. For a smooth 60 FPS, use *Render resolution: 1280 x 720 (original)*
  and let the output filter (FSR or CAS) scale it to the window, or play at 30 FPS for higher
  render resolutions.
* The training level can be skipped from the game's own pause menu (Start, then *Skip the training*).
