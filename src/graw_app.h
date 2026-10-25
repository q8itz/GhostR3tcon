// graw - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>

#include <rex/rex_app.h>

#include "graw_launcher.h"
#include "graw_menu.h"

class GrawApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GrawApp>(new GrawApp(ctx, "graw",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override;
  void OnPostLoadXexImage() override;
  void OnPostSetup() override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  void OnConfigureFonts(ImFontAtlas* atlas) override;
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override;

 private:
  void ToggleMenu();

  ImFont* ui_font_ = nullptr;
  std::filesystem::path config_path_;
  std::unique_ptr<graw::LauncherDialog> launcher_;
  std::unique_ptr<graw::PadWatcher> pad_watcher_;
  std::unique_ptr<graw::MenuDialog> menu_;
  // Read by the guest's input threads.
  std::atomic<bool> menu_open_{false};
};
