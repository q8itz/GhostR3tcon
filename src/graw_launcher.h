#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/ui/imgui_dialog.h>

struct ImFont;

namespace graw {

struct LaunchChoice {
  std::filesystem::path disc;
  std::filesystem::path save_dir;
};

// Front-end shown before the runtime starts: disc selection with Redump
// verification, save folder, and display options.
class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, ImFont* font, std::filesystem::path config_path,
                 std::filesystem::path disc, std::filesystem::path save_dir,
                 std::filesystem::path default_save_dir,
                 std::function<void(LaunchChoice)> on_play, std::function<void()> on_quit);
  ~LauncherDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class DiscState { kNone, kMissing, kWrongSize, kHashing, kVerified, kMismatch, kError };
  enum class BrowseMode { kDisc, kSaveFolder };
  struct BrowseEntry {
    std::string name;
    bool is_dir;
  };

  void SelectDisc(std::filesystem::path disc);
  void StartVerify(bool use_cache);
  void StopVerify();
  void HashWorker(std::filesystem::path disc, uint64_t size, int64_t mtime);
  std::filesystem::path VerifyCachePath() const;

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
  std::atomic<bool> cancel_{false};
  std::atomic<bool> worker_done_{false};
  std::atomic<uint64_t> hashed_bytes_{0};
  uint64_t disc_size_ = 0;
  std::mutex result_mutex_;
  std::string worker_sha1_;
  std::string worker_error_;

  std::filesystem::path save_dir_;
  std::filesystem::path initial_save_dir_;
  std::filesystem::path default_save_dir_;
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

// In-game options window (toggled with F6) for settings that apply live.
class OptionsDialog final : public rex::ui::ImGuiDialog {
 public:
  OptionsDialog(rex::ui::ImGuiDrawer* drawer, ImFont* font, std::filesystem::path config_path,
                std::function<void()> on_close);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  ImFont* font_;
  std::filesystem::path config_path_;
  std::function<void()> on_close_;
  std::string status_;
};

}  // namespace graw
