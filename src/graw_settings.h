#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, boot_to_campaign);
REXCVAR_DECLARE(bool, show_launcher);
REXCVAR_DECLARE(bool, graw_60fps);
REXCVAR_DECLARE(std::string, graw_window_size);

namespace graw {

// Redump: Tom Clancy's Ghost Recon - Advanced Warfighter (USA, Europe)
// (En,Fr,De,Es,It), Xbox 360 disc US-2005.
inline constexpr uint64_t kDiscSize = 7834892288ull;
inline constexpr const char* kDiscSha1 = "7b5cd0d7f6a0da0df8702890db65b440c17d6a3d";
inline constexpr const char* kTitleId = "555307D5";

// Sets the guest refresh rate from graw_60fps unless video_mode_refresh_rate
// was given on the command line. Applies live: the vblank timer follows it.
void ApplyFrameRate();

// Resizes the window to graw_window_size when it is not fullscreen.
void ApplyWindowSize();

// Writes the launcher-owned settings into the config file, keeping every
// other line of it intact.
void SaveSettings(const std::filesystem::path& config_path);

// Display and enhancement controls shared by the launcher and the in-game
// options overlay. Restart-only settings are disabled when `in_game`.
void DrawDisplayOptions(bool in_game);

// Shader compilation mode and the state of the saved shader cache under
// `cache_root`. Clearing the cache is only offered before the game starts.
void DrawShaderOptions(const std::filesystem::path& cache_root, bool in_game);

}  // namespace graw
