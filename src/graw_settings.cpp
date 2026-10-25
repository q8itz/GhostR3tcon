#include "graw_settings.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <rex/graphics/video_mode_util.h>
#include <rex/logging.h>
#include <rex/string.h>
#include <rex/ui/flags.h>

REXCVAR_DEFINE_BOOL(boot_to_campaign, true, "GRAW",
                    "Start Yeti_Final_Xenon.exe (Single Player) directly instead of the "
                    "default.xex shell menu");

REXCVAR_DEFINE_BOOL(show_launcher, true, "GRAW",
                    "Show the launcher (disc, save folder and display options) before the game "
                    "starts");

REXCVAR_DEFINE_BOOL(graw_60fps, false, "GRAW",
                    "Run the guest display at 60 Hz instead of the original 30 Hz "
                    "(experimental)");

REXCVAR_DEFINE_STRING(graw_window_size, "1280x720", "GRAW",
                      "Window size when not fullscreen (WIDTHxHEIGHT)");

namespace graw {

namespace {

// Settings the launcher owns in the config file.
constexpr std::array kSavedSettings = {
    "game_data_root",   "user_data_root",       "show_launcher", "boot_to_campaign",
    "graw_60fps",       "graw_window_size",     "fullscreen",    "resolution_scale",
    "anisotropic_override", "present_effect", "present_cas_additional_sharpness",
    "present_fsr_sharpness_reduction", "swap_post_effect",
};

struct Choice {
  const char* value;
  const char* label;
};

constexpr Choice kWindowSizes[] = {
    {"1280x720", "1280 x 720"},
    {"1280x800", "1280 x 800 (Steam Deck)"},
    {"1600x900", "1600 x 900"},
    {"1920x1080", "1920 x 1080"},
    {"2560x1440", "2560 x 1440"},
    {"3840x2160", "3840 x 2160"},
};

constexpr Choice kRenderScales[] = {
    {"1", "1280 x 720 (original)"},
    {"2", "2560 x 1440 (2x)"},
    {"3", "3840 x 2160 (3x)"},
    {"4", "5120 x 2880 (4x)"},
};

constexpr Choice kAntiAliasing[] = {
    {"none", "Off"},
    {"fxaa", "FXAA"},
    {"fxaa_extreme", "FXAA (strong)"},
};

constexpr Choice kOutputFilters[] = {
    {"bilinear", "Bilinear"},
    {"cas", "AMD FidelityFX CAS"},
    {"fsr", "AMD FidelityFX FSR 1"},
    {"fsr2", "AMD FidelityFX FSR 2 (experimental)"},
    {"fsr3", "AMD FidelityFX FSR 3 (experimental)"},
};

constexpr Choice kAnisotropy[] = {
    {"-1", "Game default"}, {"0", "Off"}, {"2", "2x"}, {"3", "4x"}, {"4", "8x"}, {"5", "16x"},
};

const rex::cvar::FlagEntry* FindEntry(std::string_view name) {
  for (const auto& entry : rex::cvar::GetRegistry()) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

std::string ConfigLine(const rex::cvar::FlagEntry& entry) {
  std::string value = entry.getter();
  if (entry.type == rex::cvar::FlagType::String) {
    std::string escaped;
    for (char c : value) {
      if (c == '\\' || c == '"') {
        escaped.push_back('\\');
      }
      escaped.push_back(c);
    }
    return entry.name + " = \"" + escaped + "\"";
  }
  return entry.name + " = " + value;
}

std::string LineKey(const std::string& line) {
  size_t start = line.find_first_not_of(" \t");
  if (start == std::string::npos || line[start] == '#' || line[start] == '[') {
    return {};
  }
  size_t eq = line.find('=', start);
  if (eq == std::string::npos) {
    return {};
  }
  size_t end = line.find_last_not_of(" \t", eq - 1);
  return line.substr(start, end - start + 1);
}

// Combo bound to a cvar; returns true when the value changed.
bool CvarCombo(const char* label, const char* cvar, const Choice* choices, size_t count) {
  std::string current = rex::cvar::GetFlagByName(cvar);
  const char* preview = current.c_str();
  for (size_t i = 0; i < count; ++i) {
    if (current == choices[i].value) {
      preview = choices[i].label;
    }
  }
  bool changed = false;
  if (ImGui::BeginCombo(label, preview)) {
    for (size_t i = 0; i < count; ++i) {
      bool selected = current == choices[i].value;
      if (ImGui::Selectable(choices[i].label, selected) && !selected) {
        changed = rex::cvar::SetFlagByName(cvar, choices[i].value);
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

void NextLaunchNote(bool in_game) {
  if (in_game) {
    ImGui::SameLine();
    ImGui::TextDisabled("(next launch)");
  }
}

}  // namespace

void ApplyFrameRate() {
  if (rex::cvar::GetFlagSource("video_mode_refresh_rate") == rex::cvar::Source::kCommandLine) {
    return;
  }
  // The campaign was built for 30 Hz. At 60 Hz its squad logic has been seen
  // to run ahead of the frames and read a null object after a few minutes.
  const char* rate = REXCVAR_GET(graw_60fps) ? "60" : "30";
  if (rex::cvar::GetFlagByName("video_mode_refresh_rate") != rate) {
    rex::cvar::SetFlagByName("video_mode_refresh_rate", rate);
    REXLOG_INFO("Guest display set to {} Hz", rate);
  }
}

void ApplyWindowSize() {
  if (REXCVAR_GET(fullscreen)) {
    return;
  }
  int32_t width = 0, height = 0;
  if (!rex::graphics::video_mode_util::TryParseResolutionPreset(REXCVAR_GET(graw_window_size),
                                                               width, height)) {
    return;
  }
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  if (windows) {
    for (int i = 0; i < count; ++i) {
      SDL_SetWindowSize(windows[i], width, height);
    }
    SDL_free(windows);
  }
}

void SaveSettings(const std::filesystem::path& config_path) {
  std::vector<std::string> kept;
  if (std::ifstream in(config_path); in) {
    std::string line;
    while (std::getline(in, line)) {
      std::string key = LineKey(line);
      bool owned = std::find(kSavedSettings.begin(), kSavedSettings.end(), key) !=
                   kSavedSettings.end();
      if (!owned && line != "# Launcher settings") {
        kept.push_back(line);
      }
    }
  }
  while (!kept.empty() && kept.back().empty()) {
    kept.pop_back();
  }

  std::ostringstream out;
  for (const auto& line : kept) {
    out << line << '\n';
  }
  if (!kept.empty()) {
    out << '\n';
  }
  out << "# Launcher settings\n";
  for (const char* name : kSavedSettings) {
    const auto* entry = FindEntry(name);
    if (entry && entry->getter() != entry->default_value) {
      out << ConfigLine(*entry) << '\n';
    }
  }

  std::filesystem::path tmp = config_path;
  tmp += ".tmp";
  {
    std::ofstream file(tmp, std::ios::trunc);
    if (!file) {
      REXLOG_ERROR("Could not write {}", tmp.string());
      return;
    }
    file << out.str();
  }
  std::error_code ec;
  std::filesystem::rename(tmp, config_path, ec);
  if (ec) {
    REXLOG_ERROR("Could not replace {}: {}", config_path.string(), ec.message());
    return;
  }
  REXLOG_INFO("Saved launcher settings to {}", config_path.string());
}

void DrawDisplayOptions(bool in_game) {
  const float combo_width = ImGui::GetFontSize() * 14.0f;

  int mode = REXCVAR_GET(fullscreen) ? 0 : 1;
  ImGui::SetNextItemWidth(combo_width);
  if (ImGui::Combo("Display mode", &mode, "Fullscreen\0Windowed\0")) {
    rex::cvar::SetFlagByName("fullscreen", mode == 0 ? "true" : "false");
    ApplyWindowSize();
  }

  ImGui::BeginDisabled(mode == 0);
  ImGui::SetNextItemWidth(combo_width);
  if (CvarCombo("Window size", "graw_window_size", kWindowSizes, std::size(kWindowSizes))) {
    ApplyWindowSize();
  }
  ImGui::EndDisabled();

  ImGui::SetNextItemWidth(combo_width);
  CvarCombo("Render resolution", "resolution_scale", kRenderScales, std::size(kRenderScales));
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Renders the game at a multiple of its native 1280x720 and scales the\n"
        "result to the window. Higher values need more GPU memory.");
  }
  NextLaunchNote(in_game);

  int fps = REXCVAR_GET(graw_60fps) ? 1 : 0;
  ImGui::SetNextItemWidth(combo_width);
  if (ImGui::Combo("Frame rate", &fps, "30 FPS (original)\0" "60 FPS (experimental)\0")) {
    rex::cvar::SetFlagByName("graw_60fps", fps ? "true" : "false");
  }
  if (fps) {
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                       "60 FPS doubles the game's display rate. Campaign missions\n"
                       "have crashed after a few minutes at 60 Hz; switch back to\n"
                       "30 FPS if a mission becomes unstable.");
  }

  ImGui::SetNextItemWidth(combo_width);
  CvarCombo("Texture filtering", "anisotropic_override", kAnisotropy, std::size(kAnisotropy));

  ImGui::SetNextItemWidth(combo_width);
  CvarCombo("Anti-aliasing", "swap_post_effect", kAntiAliasing, std::size(kAntiAliasing));
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("FXAA smooths jagged edges in the final image. It costs very\n"
                      "little, but softens fine detail slightly.");
  }

  // Only the filters this SDK build was compiled with are offered.
  std::vector<Choice> filters;
  if (const auto* effect = FindEntry("present_effect")) {
    for (const Choice& choice : kOutputFilters) {
      const auto& allowed = effect->constraints.allowed_values;
      if (std::find(allowed.begin(), allowed.end(), choice.value) != allowed.end()) {
        filters.push_back(choice);
      }
    }
  }
  ImGui::SetNextItemWidth(combo_width);
  CvarCombo("Output filter", "present_effect", filters.data(), filters.size());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "How the rendered image is scaled to the window.\n"
        "Bilinear: plain smooth scaling.\n"
        "CAS: contrast adaptive sharpening, restores detail lost to scaling.\n"
        "FSR 1: edge-preserving upscaling plus sharpening. Best when the\n"
        "render resolution is below the window size.");
  }

  std::string filter = rex::cvar::GetFlagByName("present_effect");
  if (filter == "cas" || filter == "fsr") {
    // CAS adds sharpness from 0; FSR's RCAS takes a reduction in stops from
    // full sharpness. Both are shown as one 0-100% scale.
    bool is_cas = filter == "cas";
    const char* cvar =
        is_cas ? "present_cas_additional_sharpness" : "present_fsr_sharpness_reduction";
    float value = std::stof(rex::cvar::GetFlagByName(cvar));
    float percent = is_cas ? value * 100.0f : (1.0f - value / 2.0f) * 100.0f;
    ImGui::SetNextItemWidth(combo_width);
    if (ImGui::SliderFloat("Sharpness", &percent, 0.0f, 100.0f, "%.0f%%")) {
      float new_value = is_cas ? percent / 100.0f : (1.0f - percent / 100.0f) * 2.0f;
      rex::cvar::SetFlagByName(cvar, std::to_string(new_value));
    }
  }
}

}  // namespace graw
