#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "graw_disc.h"

struct ImFont;

namespace graw {

struct LaunchChoice {
  std::filesystem::path disc;
  std::filesystem::path save_dir;
};

// Front-end shown before the runtime starts: disc selection with a check of
// its executables, save folder, and display options.
class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, ImFont* font, std::filesystem::path config_path,
                 std::filesystem::path disc, std::filesystem::path save_dir,
                 std::filesystem::path default_save_dir, std::filesystem::path cache_root,
                 std::function<void(LaunchChoice)> on_play, std::function<void()> on_quit);
  ~LauncherDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class DiscState { kNone, kMissing, kChecking, kVerified, kMismatch, kError };
  enum class BrowseMode { kDisc, kSaveFolder };
  struct BrowseEntry {
    std::string name;
    bool is_dir;
  };

  void SelectDisc(std::filesystem::path disc);
  void StartVerify();
  void StopVerify();

  void OpenBrowser(BrowseMode mode);
  void BrowseTo(std::filesystem::path dir);
  void DrawBrowser();
  void DrawDiscSection();
  void DrawSaveSection();
  void Play();

  ImFont* font_;
  std::filesystem::path config_path_;
  std::function<void(LaunchChoice)> on_play_;
  std::function<void()> on_quit_;

  std::filesystem::path disc_;
  char disc_buf_[1024] = {};
  DiscState disc_state_ = DiscState::kNone;
  std::string disc_message_;
  bool allow_unverified_ = false;

  std::thread worker_;
  std::atomic<bool> worker_done_{false};
  DiscCheck worker_result_;

  std::filesystem::path save_dir_;
  std::filesystem::path initial_save_dir_;
  std::filesystem::path default_save_dir_;
  std::filesystem::path cache_root_;
  char save_buf_[1024] = {};
  bool copy_saves_ = true;

  bool browser_open_request_ = false;
  BrowseMode browse_mode_ = BrowseMode::kDisc;
  std::filesystem::path browse_dir_;
  char browse_buf_[1024] = {};
  std::vector<BrowseEntry> browse_entries_;
  int browse_selected_ = -1;
  std::string browse_error_;

  bool launched_ = false;
};

}  // namespace graw
