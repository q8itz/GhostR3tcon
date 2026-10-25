#include "graw_launcher.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>

#include <imgui.h>

#include <rex/logging.h>
#include <rex/string.h>

#include "graw_settings.h"
#include "sha1.h"

namespace graw {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kBad(1.0f, 0.45f, 0.4f, 1.0f);
const ImVec4 kWarn(1.0f, 0.75f, 0.3f, 1.0f);

float UiFontSize(const ImGuiIO& io) {
  return std::clamp(io.DisplaySize.y / 46.0f, 15.0f, 40.0f);
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

std::string Trim(const char* text) {
  std::string s(text);
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return {};
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::filesystem::path ExpandHome(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
    if (const char* home = std::getenv("HOME")) {
      return std::filesystem::path(home) / path.substr(path.size() > 1 && path[1] == '/' ? 2 : 1);
    }
  }
  return path;
}

std::filesystem::path HomeDir() {
  const char* home = std::getenv("HOME");
  return home ? std::filesystem::path(home) : std::filesystem::path("/");
}

void CopyToBuffer(char* buffer, size_t size, const std::filesystem::path& path) {
  rex::string::copy_truncating(buffer, path.string(), size);
}

std::string FormatBytes(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
  return text;
}

int64_t ModifiedTime(const std::filesystem::path& path) {
  std::error_code ec;
  auto time = std::filesystem::last_write_time(path, ec);
  return ec ? 0 : int64_t(time.time_since_epoch().count());
}

bool HasSaveData(const std::filesystem::path& dir) {
  std::error_code ec;
  return std::filesystem::is_directory(dir / kTitleId, ec);
}

}  // namespace

LauncherDialog::LauncherDialog(rex::ui::ImGuiDrawer* drawer, ImFont* font,
                               std::filesystem::path config_path, std::filesystem::path disc,
                               std::filesystem::path save_dir,
                               std::filesystem::path default_save_dir,
                               std::filesystem::path cache_root,
                               std::function<void(LaunchChoice)> on_play,
                               std::function<void()> on_quit)
    : ImGuiDialog(drawer),
      font_(font),
      config_path_(std::move(config_path)),
      on_play_(std::move(on_play)),
      on_quit_(std::move(on_quit)),
      save_dir_(std::move(save_dir)),
      initial_save_dir_(save_dir_),
      default_save_dir_(std::move(default_save_dir)),
      cache_root_(std::move(cache_root)) {
  CopyToBuffer(save_buf_, sizeof(save_buf_), save_dir_);
  GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  if (!disc.empty()) {
    SelectDisc(std::move(disc));
  }
}

LauncherDialog::~LauncherDialog() {
  StopVerify();
}

std::filesystem::path LauncherDialog::VerifyCachePath() const {
  return config_path_.parent_path() / "graw_disc_verified.txt";
}

void LauncherDialog::SelectDisc(std::filesystem::path disc) {
  StopVerify();
  allow_unverified_ = false;
  disc_ = std::move(disc);
  CopyToBuffer(disc_buf_, sizeof(disc_buf_), disc_);
  disc_message_.clear();

  std::error_code ec;
  if (disc_.empty()) {
    disc_state_ = DiscState::kNone;
    return;
  }
  if (!std::filesystem::is_regular_file(disc_, ec)) {
    disc_state_ = DiscState::kMissing;
    disc_message_ = "File not found.";
    return;
  }
  disc_size_ = std::filesystem::file_size(disc_, ec);
  if (ec || disc_size_ != kDiscSize) {
    disc_state_ = DiscState::kWrongSize;
    disc_message_ = "This file is " + std::to_string(disc_size_) +
                    " bytes. The Redump image of GRAW (USA, Europe) is 7834892288 bytes.";
    return;
  }
  StartVerify(true);
}

void LauncherDialog::StartVerify(bool use_cache) {
  StopVerify();
  int64_t mtime = ModifiedTime(disc_);

  if (use_cache) {
    std::ifstream cache(VerifyCachePath());
    uint64_t size = 0;
    int64_t cached_mtime = 0;
    std::string sha1, path;
    if (cache >> size >> cached_mtime >> sha1 && std::getline(cache >> std::ws, path) &&
        path == disc_.string() && size == disc_size_ && cached_mtime == mtime &&
        sha1 == kDiscSha1) {
      disc_state_ = DiscState::kVerified;
      disc_message_ = "Matches Redump (checked earlier, file unchanged since).";
      return;
    }
  }

  disc_state_ = DiscState::kHashing;
  disc_message_.clear();
  hashed_bytes_ = 0;
  worker_done_ = false;
  cancel_ = false;
  worker_ = std::thread(&LauncherDialog::HashWorker, this, disc_, disc_size_, mtime);
}

void LauncherDialog::StopVerify() {
  cancel_ = true;
  if (worker_.joinable()) {
    worker_.join();
  }
  cancel_ = false;
  if (disc_state_ == DiscState::kHashing) {
    disc_state_ = DiscState::kNone;
  }
}

void LauncherDialog::HashWorker(std::filesystem::path disc, uint64_t size, int64_t mtime) {
  std::string error;
  std::string digest;
  int fd = ::open(disc.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    error = "Could not open the file.";
  } else {
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    std::vector<uint8_t> buffer(8 << 20);
    Sha1 sha1;
    for (;;) {
      if (cancel_) {
        break;
      }
      ssize_t n = ::read(fd, buffer.data(), buffer.size());
      if (n < 0) {
        error = "Read error while hashing.";
        break;
      }
      if (n == 0) {
        digest = Sha1::ToHex(sha1.Finish());
        break;
      }
      sha1.Update(buffer.data(), size_t(n));
      hashed_bytes_ += uint64_t(n);
    }
    ::close(fd);
  }

  if (digest == kDiscSha1) {
    std::ofstream cache(VerifyCachePath(), std::ios::trunc);
    cache << size << ' ' << mtime << ' ' << digest << '\n' << disc.string() << '\n';
  }
  {
    std::lock_guard lock(result_mutex_);
    worker_sha1_ = digest;
    worker_error_ = error;
  }
  worker_done_ = true;
}

void LauncherDialog::OpenBrowser(BrowseMode mode) {
  browse_mode_ = mode;
  std::error_code ec;
  std::filesystem::path start;
  if (mode == BrowseMode::kDisc) {
    std::filesystem::path typed = ExpandHome(Trim(disc_buf_));
    start = typed.has_parent_path() ? typed.parent_path() : HomeDir();
  } else {
    std::filesystem::path typed = ExpandHome(Trim(save_buf_));
    start = std::filesystem::is_directory(typed, ec) ? typed : typed.parent_path();
  }
  while (!start.empty() && !std::filesystem::is_directory(start, ec)) {
    if (start == start.parent_path()) {
      break;
    }
    start = start.parent_path();
  }
  if (start.empty() || !std::filesystem::is_directory(start, ec)) {
    start = HomeDir();
  }
  BrowseTo(start);
  browser_open_request_ = true;
}

void LauncherDialog::BrowseTo(std::filesystem::path dir) {
  std::error_code ec;
  dir = std::filesystem::weakly_canonical(dir, ec);
  std::vector<BrowseEntry> entries;
  std::filesystem::directory_iterator it(
      dir, std::filesystem::directory_options::skip_permission_denied, ec);
  if (ec) {
    browse_error_ = "Cannot open " + dir.string() + ": " + ec.message();
    return;
  }
  for (const auto& entry : it) {
    std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') {
      continue;
    }
    std::error_code type_ec;
    bool is_dir = entry.is_directory(type_ec);
    if (!is_dir) {
      if (browse_mode_ != BrowseMode::kDisc || Lower(entry.path().extension().string()) != ".iso") {
        continue;
      }
    }
    entries.push_back({std::move(name), is_dir});
  }
  std::sort(entries.begin(), entries.end(), [](const BrowseEntry& a, const BrowseEntry& b) {
    if (a.is_dir != b.is_dir) {
      return a.is_dir;
    }
    return Lower(a.name) < Lower(b.name);
  });
  browse_error_.clear();
  browse_dir_ = dir;
  browse_entries_ = std::move(entries);
  browse_selected_ = -1;
  CopyToBuffer(browse_buf_, sizeof(browse_buf_), browse_dir_);
}

void LauncherDialog::DrawBrowser() {
  if (browser_open_request_) {
    ImGui::OpenPopup("Browse##graw");
    browser_open_request_ = false;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.8f, viewport->Size.y * 0.8f),
                           ImGuiCond_Always);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Browse##graw", nullptr,
                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoTitleBar)) {
    return;
  }
  const bool disc_mode = browse_mode_ == BrowseMode::kDisc;
  ImGui::TextUnformatted(disc_mode ? "Select the Ghost Recon Advanced Warfighter disc image (.iso)"
                                   : "Select the folder for save data");
  ImGui::Separator();

  auto place = [this](const char* label, const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
      return;
    }
    if (ImGui::Button(label)) {
      BrowseTo(path);
    }
    ImGui::SameLine();
  };
  if (ImGui::Button("Up")) {
    BrowseTo(browse_dir_.parent_path());
  }
  ImGui::SameLine();
  place("Home", HomeDir());
  place("/mnt", "/mnt");
  place("/media", "/media");
  place("/run/media", "/run/media");
  place("/", "/");
  ImGui::NewLine();

  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputText("##browse_path", browse_buf_, sizeof(browse_buf_),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    std::filesystem::path typed = ExpandHome(Trim(browse_buf_));
    std::error_code ec;
    if (disc_mode && std::filesystem::is_regular_file(typed, ec)) {
      SelectDisc(typed);
      ImGui::CloseCurrentPopup();
    } else {
      BrowseTo(typed);
    }
  }
  if (!browse_error_.empty()) {
    ImGui::TextColored(kBad, "%s", browse_error_.c_str());
  }

  const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;
  ImGui::BeginChild("##entries", ImVec2(0, -footer), ImGuiChildFlags_Borders);
  std::filesystem::path open_dir;
  std::filesystem::path chosen_file;
  for (int i = 0; i < int(browse_entries_.size()); ++i) {
    const auto& entry = browse_entries_[i];
    std::string label = entry.is_dir ? entry.name + "/" : entry.name;
    if (!entry.is_dir) {
      std::error_code ec;
      uint64_t size = std::filesystem::file_size(browse_dir_ / entry.name, ec);
      if (!ec) {
        label += "   (" + FormatBytes(size) + ")";
      }
    }
    ImGui::PushID(i);
    if (ImGui::Selectable(label.c_str(), browse_selected_ == i,
                          ImGuiSelectableFlags_AllowDoubleClick)) {
      browse_selected_ = i;
      bool activate = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ||
                      ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                      ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
      if (activate) {
        if (entry.is_dir) {
          open_dir = browse_dir_ / entry.name;
        } else {
          chosen_file = browse_dir_ / entry.name;
        }
      }
    }
    ImGui::PopID();
  }
  if (browse_entries_.empty()) {
    ImGui::TextDisabled(disc_mode ? "No folders or .iso files here." : "No folders here.");
  }
  ImGui::EndChild();

  const bool has_selection =
      browse_selected_ >= 0 && browse_selected_ < int(browse_entries_.size());
  if (disc_mode) {
    bool file_selected = has_selection && !browse_entries_[browse_selected_].is_dir;
    ImGui::BeginDisabled(!file_selected);
    if (ImGui::Button("Select disc") && file_selected) {
      chosen_file = browse_dir_ / browse_entries_[browse_selected_].name;
    }
    ImGui::EndDisabled();
  } else {
    std::filesystem::path target = browse_dir_;
    if (has_selection && browse_entries_[browse_selected_].is_dir) {
      target /= browse_entries_[browse_selected_].name;
    }
    std::string label = "Use " + target.string();
    if (ImGui::Button(label.c_str())) {
      save_dir_ = target;
      CopyToBuffer(save_buf_, sizeof(save_buf_), save_dir_);
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }

  if (!chosen_file.empty()) {
    SelectDisc(chosen_file);
    ImGui::CloseCurrentPopup();
  } else if (!open_dir.empty()) {
    BrowseTo(open_dir);
  }
  ImGui::EndPopup();
}

void LauncherDialog::DrawDiscSection() {
  ImGui::SeparatorText("Game disc");
  const float browse_width = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SetNextItemWidth(-(browse_width + ImGui::GetStyle().ItemSpacing.x));
  if (ImGui::InputTextWithHint("##disc", "Path to the GRAW Xbox 360 .iso", disc_buf_,
                               sizeof(disc_buf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
    SelectDisc(ExpandHome(Trim(disc_buf_)));
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse...##disc")) {
    OpenBrowser(BrowseMode::kDisc);
  }

  if (disc_state_ == DiscState::kHashing && worker_done_) {
    worker_.join();
    std::lock_guard lock(result_mutex_);
    if (!worker_error_.empty()) {
      disc_state_ = DiscState::kError;
      disc_message_ = worker_error_;
    } else if (worker_sha1_ == kDiscSha1) {
      disc_state_ = DiscState::kVerified;
      disc_message_ = "Matches Redump. SHA-1 " + worker_sha1_;
    } else {
      disc_state_ = DiscState::kMismatch;
      disc_message_ = "SHA-1 " + worker_sha1_ + " does not match Redump (" + kDiscSha1 + ").";
    }
  }

  switch (disc_state_) {
    case DiscState::kNone:
      ImGui::TextDisabled("Choose the Ghost Recon Advanced Warfighter (USA, Europe) disc image.");
      break;
    case DiscState::kHashing: {
      float fraction = disc_size_ ? float(double(hashed_bytes_) / double(disc_size_)) : 0.0f;
      char overlay[64];
      std::snprintf(overlay, sizeof(overlay), "Verifying disc... %d%%", int(fraction * 100.0f));
      ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
      break;
    }
    case DiscState::kVerified:
      ImGui::TextColored(kGood, "Verified: %s", disc_message_.c_str());
      break;
    case DiscState::kMissing:
    case DiscState::kWrongSize:
    case DiscState::kMismatch:
    case DiscState::kError:
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(kBad, "Not verified: %s", disc_message_.c_str());
      ImGui::PopTextWrapPos();
      break;
  }

  if (disc_state_ == DiscState::kVerified || disc_state_ == DiscState::kMismatch ||
      disc_state_ == DiscState::kError) {
    if (ImGui::SmallButton("Verify again")) {
      StartVerify(false);
    }
  }
  if (disc_state_ == DiscState::kWrongSize || disc_state_ == DiscState::kMismatch ||
      disc_state_ == DiscState::kError) {
    ImGui::Checkbox("Start anyway with this unverified image", &allow_unverified_);
  }
}

void LauncherDialog::DrawSaveSection() {
  ImGui::SeparatorText("Save data");
  const ImGuiStyle& style = ImGui::GetStyle();
  const float buttons_width = ImGui::CalcTextSize("Browse...Default").x + style.FramePadding.x * 4 +
                              style.ItemSpacing.x * 2;
  ImGui::SetNextItemWidth(-buttons_width);
  if (ImGui::InputText("##save", save_buf_, sizeof(save_buf_))) {
    save_dir_ = ExpandHome(Trim(save_buf_));
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse...##save")) {
    OpenBrowser(BrowseMode::kSaveFolder);
  }
  ImGui::SameLine();
  if (ImGui::Button("Default")) {
    save_dir_ = default_save_dir_;
    CopyToBuffer(save_buf_, sizeof(save_buf_), save_dir_);
  }
  ImGui::TextDisabled("Profiles and saves are written to %s/%s",
                      save_dir_.string().c_str(), kTitleId);

  std::error_code ec;
  bool moved = !save_dir_.empty() &&
               std::filesystem::weakly_canonical(save_dir_, ec) !=
                   std::filesystem::weakly_canonical(initial_save_dir_, ec);
  if (moved && HasSaveData(initial_save_dir_)) {
    std::string label = "Copy existing saves from " + initial_save_dir_.string();
    ImGui::Checkbox(label.c_str(), &copy_saves_);
  }
}

void LauncherDialog::Play() {
  if (launched_) {
    return;
  }
  std::filesystem::path save_dir = ExpandHome(Trim(save_buf_));
  if (save_dir.empty()) {
    save_dir = default_save_dir_;
  }
  std::error_code ec;
  std::filesystem::create_directories(save_dir, ec);
  if (!std::filesystem::is_directory(save_dir, ec)) {
    rex::ui::ImGuiDialog::ShowMessageBox(imgui_drawer(), "Save folder",
                                         "Could not create " + save_dir.string());
    return;
  }

  bool moved = std::filesystem::weakly_canonical(save_dir, ec) !=
               std::filesystem::weakly_canonical(initial_save_dir_, ec);
  if (moved && copy_saves_ && HasSaveData(initial_save_dir_)) {
    for (const auto& entry : std::filesystem::directory_iterator(initial_save_dir_, ec)) {
      if (entry.path().filename() == "cache") {
        continue;
      }
      std::error_code copy_ec;
      std::filesystem::copy(entry.path(), save_dir / entry.path().filename(),
                            std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::skip_existing,
                            copy_ec);
      if (copy_ec) {
        REXLOG_WARN("Copying {} failed: {}", entry.path().string(), copy_ec.message());
      }
    }
    REXLOG_INFO("Copied save data from {} to {}", initial_save_dir_.string(), save_dir.string());
  }

  rex::cvar::SetFlagByName("game_data_root", disc_.string());
  rex::cvar::SetFlagByName("user_data_root", save_dir.string());
  SaveSettings(config_path_);

  launched_ = true;
  StopVerify();
  on_play_({disc_, save_dir});
}

void LauncherDialog::OnDraw(ImGuiIO& io) {
  if (launched_) {
    return;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.06f, 1.0f));
  ImGui::Begin("GRAW launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();
  const float font_size = UiFontSize(io);
  ImGui::PushFont(font_, font_size);

  const float column = std::min(viewport->Size.x * 0.9f, font_size * 46.0f);
  const ImVec2 button(font_size * 8.0f, font_size * 2.0f);
  const float footer = button.y + ImGui::GetStyle().ItemSpacing.y * 3.0f + font_size;
  const float column_x = (viewport->Size.x - column) * 0.5f;
  ImGui::SetCursorPosX(column_x);
  ImGui::BeginChild("##column", ImVec2(column, -footer), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoBackground);

  ImGui::Dummy(ImVec2(0, font_size * 0.5f));
  ImGui::PushFont(nullptr, font_size * 1.6f);
  ImGui::TextUnformatted("GHOST RECON ADVANCED WARFIGHTER");
  ImGui::PopFont();
  ImGui::TextDisabled("Xbox 360 recompilation  -  launcher");
  ImGui::Dummy(ImVec2(0, font_size * 0.5f));

  DrawDiscSection();
  ImGui::Dummy(ImVec2(0, font_size * 0.4f));
  DrawSaveSection();
  ImGui::Dummy(ImVec2(0, font_size * 0.4f));
  ImGui::SeparatorText("Display");
  DrawDisplayOptions(false);
  ImGui::Dummy(ImVec2(0, font_size * 0.4f));
  ImGui::SeparatorText("Shaders");
  DrawShaderOptions(cache_root_, false);
  ImGui::Dummy(ImVec2(0, font_size * 0.4f));
  ImGui::SeparatorText("Startup");
  bool campaign = REXCVAR_GET(boot_to_campaign);
  if (ImGui::Checkbox("Go straight to Single Player", &campaign)) {
    rex::cvar::SetFlagByName("boot_to_campaign", campaign ? "true" : "false");
  }
  bool show = REXCVAR_GET(show_launcher);
  if (ImGui::Checkbox("Show this launcher at startup", &show)) {
    rex::cvar::SetFlagByName("show_launcher", show ? "true" : "false");
  }
  if (!show) {
    ImGui::SameLine();
    ImGui::TextDisabled("(turn it back on from the in-game menu)");
  }

  DrawBrowser();
  ImGui::EndChild();

  ImGui::Separator();
  ImGui::SetCursorPosX(column_x);
  const bool can_play = disc_state_ == DiscState::kVerified ||
                        (allow_unverified_ && disc_state_ != DiscState::kHashing &&
                         disc_state_ != DiscState::kMissing && disc_state_ != DiscState::kNone);
  ImGui::BeginDisabled(!can_play);
  if (ImGui::Button("Play", button)) {
    Play();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Quit", button)) {
    StopVerify();
    on_quit_();
  }
  if (!can_play && disc_state_ != DiscState::kHashing) {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Play is available once the disc is verified.");
  }
  ImGui::PopFont();
  ImGui::End();
}

}  // namespace graw
