#include "generated/default/graw_init.h"

#include "graw_app.h"

#include <chrono>
#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/string/utf8.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/ui/keybinds.h>

#include "graw_settings.h"

REXCVAR_DECLARE(std::string, game_data_root);
REXCVAR_DECLARE(std::string, user_data_root);

void GrawApp::OnConfigurePaths([[maybe_unused]] rex::PathConfig& paths) {
#ifndef _WIN32
  // Inside an AppImage (or any read-only install) the executable's folder
  // can't hold the config or the logs, so they move to the XDG config
  // directory and the user data folder.
  std::filesystem::path exe_dir = paths.config_path.parent_path();
  if (access(exe_dir.c_str(), W_OK) == 0) {
    return;
  }
  std::error_code ec;
  if (REXCVAR_GET(log_file).empty() && !paths.user_data_root.empty()) {
    std::filesystem::create_directories(paths.user_data_root / "logs", ec);
    rex::cvar::SetFlagByName("log_file", (paths.user_data_root / "logs" / "graw.log").string());
  }
  std::filesystem::path config_home;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    config_home = xdg;
  } else if (const char* home = std::getenv("HOME"); home && *home) {
    config_home = std::filesystem::path(home) / ".config";
  } else {
    return;
  }
  std::filesystem::create_directories(config_home / "graw", ec);
  paths.config_path = config_home / "graw" / paths.config_path.filename();
#endif
}

void GrawApp::OnPreSetup(rex::RuntimeConfig& config) {
  // The game only runs with the Xenos GPU emulation.
  if (config.gpu_plugin.empty()) {
    config.gpu_plugin = "xenos";
  }
  graw::ApplyFrameRate();
  rex::cvar::RegisterChangeCallback("graw_60fps",
                                    [](std::string_view, std::string_view) {
                                      graw::ApplyFrameRate();
                                    });
}

void GrawApp::OnConfigureFonts(ImFontAtlas* atlas) {
  for (const char* path : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                           "/usr/share/fonts/TTF/DejaVuSans.ttf",
                           "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
                           "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                           "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"}) {
    if (std::filesystem::exists(path)) {
      ImFontConfig config;
      config.OversampleH = 2;
      ui_font_ = atlas->AddFontFromFileTTF(path, 32.0f, &config);
      if (ui_font_) {
        return;
      }
    }
  }
}

void GrawApp::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  // The app name stays "graw" because it also names the config file and data folder.
  window()->SetTitle("GhostR3tcon");
  rex::ui::RegisterBind("bind_graw_options", "F6", "Toggle GRAW menu", [this] { ToggleMenu(); });

  SetGuestFrameStats([] {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last_time = Clock::now();
    static uint64_t last_count = rex::kernel::xboxkrnl::VdSwapCount();
    static rex::ui::FrameStats stats;
    auto now = Clock::now();
    double elapsed = std::chrono::duration<double>(now - last_time).count();
    if (elapsed >= 0.5) {
      uint64_t count = rex::kernel::xboxkrnl::VdSwapCount();
      uint64_t frames = count - last_count;
      stats.fps = double(frames) / elapsed;
      stats.frame_time_ms = frames ? elapsed * 1000.0 / double(frames) : 0.0;
      stats.frame_count = count;
      last_count = count;
      last_time = now;
    }
    return stats;
  });
}

std::optional<rex::PathConfig> GrawApp::OnFinalizePaths(
    const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) {
  config_path_ = defaults.config_path;
  cache_root_ = defaults.cache_root;
  graw::ApplyWindowSize();

  // The defaults are resolved before the config file loads; pick up the
  // paths the launcher saved there.
  rex::PathConfig paths = defaults;
  if (!REXCVAR_GET(game_data_root).empty()) {
    paths.game_data_root = REXCVAR_GET(game_data_root);
  }
  if (!REXCVAR_GET(user_data_root).empty()) {
    paths.user_data_root = REXCVAR_GET(user_data_root);
  }

  std::error_code ec;
  bool have_disc = !paths.game_data_root.empty() && std::filesystem::exists(paths.game_data_root, ec);
  if ((!REXCVAR_GET(show_launcher) && have_disc) || !imgui_drawer()) {
    return paths;
  }

  launcher_ = std::make_unique<graw::LauncherDialog>(
      imgui_drawer(), ui_font_, config_path_, paths.game_data_root, paths.user_data_root,
      rex::filesystem::GetUserFolder() / "graw", cache_root_,
      [this, paths, resume](graw::LaunchChoice choice) mutable {
        paths.game_data_root = choice.disc;
        paths.user_data_root = choice.save_dir;
        REXLOG_INFO("Launcher: disc {}, saves in {}", choice.disc.string(),
                    choice.save_dir.string());
        app_context().CallInUIThreadDeferred([this, paths, resume] {
          launcher_.reset();
          graw::ApplyFrameRate();
          resume(paths);
        });
      },
      [this] {
        app_context().CallInUIThreadDeferred([this] {
          launcher_.reset();
          app_context().QuitFromUIThread();
        });
      });
  return std::nullopt;
}

void GrawApp::ToggleMenu() {
  if (launcher_ || !imgui_drawer()) {
    return;
  }
  if (menu_) {
    menu_->Close();
    return;
  }
  menu_ = std::make_unique<graw::MenuDialog>(
      imgui_drawer(), ui_font_, config_path_, cache_root_, pad_watcher_.get(),
      [this] {
        app_context().CallInUIThreadDeferred([this] {
          menu_.reset();
          menu_open_ = false;
        });
      },
      // Closing the window takes the title shutdown path; QuitFromUIThread
      // alone would leave the running title's threads blocking teardown.
      [this] { app_context().CallInUIThreadDeferred([this] { window()->RequestClose(); }); });
  menu_open_ = true;
}

void GrawApp::OnPostSetup() {
  pad_watcher_ = std::make_unique<graw::PadWatcher>(
      [this] { app_context().CallInUIThreadDeferred([this] { ToggleMenu(); }); });

  // The game gets no input while the menu is open, and none from mouse clicks
  // on other overlays.
  auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
  if (input && imgui_drawer()) {
    input->SetActiveCallback([this] {
      return !menu_open_ &&
             (!imgui_drawer()->HasDialogs() || !imgui_drawer()->GetIO().WantCaptureMouse);
    });
  }
}

void GrawApp::OnPostLoadXexImage() {
  if (!REXCVAR_GET(boot_to_campaign)) {
    return;
  }
  rex::Runtime* rt = runtime();
  auto shell = rt->kernel_state()->GetExecutableModule();
  if (!shell) {
    return;
  }
  // Same swap the shell's Single Player entry performs through
  // XamLoaderLaunchTitle. The campaign reads no launch data from the shell.
  std::string campaign = rex::string::utf8_join_guest_paths(
      rex::string::utf8_find_base_guest_path(shell->path()), "Yeti_Final_Xenon.exe");
  if (!rt->kernel_state()->FindRecompiledModule(campaign)) {
    REXLOG_ERROR("No recompiled module for {}; starting the shell menu", campaign);
    return;
  }
  REXLOG_INFO("Booting Single Player directly: {}", campaign);
  // The shell image is released before the campaign loads, so a failure here
  // leaves no executable and the launch reports it.
  if (XFAILED(rt->ReplaceExecutable(campaign))) {
    REXLOG_ERROR("Direct campaign boot failed for {}", campaign);
  }
}
