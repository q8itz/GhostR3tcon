#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include <SDL3/SDL_events.h>

#include <rex/ui/imgui_dialog.h>

struct ImFont;

namespace graw {

// Follows every gamepad through an SDL event watch, independently of the
// game's input (which is blocked while the menu is open). Guide, or Back and
// Start together, calls on_toggle on whichever thread SDL posts the event.
class PadWatcher {
 public:
  struct State {
    uint32_t buttons = 0;  // Bit per SDL_GamepadButton, merged across pads.
    int16_t left_x = 0;
    int16_t left_y = 0;
  };

  explicit PadWatcher(std::function<void()> on_toggle);
  ~PadWatcher();
  PadWatcher(const PadWatcher&) = delete;
  PadWatcher& operator=(const PadWatcher&) = delete;

  State Get() const;

 private:
  static bool SDLCALL Watch(void* user, SDL_Event* event);

  std::function<void()> on_toggle_;
  bool registered_ = false;
  mutable std::mutex mutex_;
  std::unordered_map<SDL_JoystickID, State> pads_;
};

// In-game menu, opened with F6 or from the pad, for settings that apply live.
// Navigable with the D-pad or left stick, A to select and B to go back.
class MenuDialog final : public rex::ui::ImGuiDialog {
 public:
  MenuDialog(rex::ui::ImGuiDrawer* drawer, ImFont* font, std::filesystem::path config_path,
             std::filesystem::path cache_root, const PadWatcher* pad, std::function<void()> on_close,
             std::function<void()> on_quit);

  // Hides the menu, and closes it once no pad button is held so the button
  // that closed it doesn't reach the game.
  void Close() { closing_ = true; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void FeedPad(ImGuiIO& io, const PadWatcher::State& pad);

  ImFont* font_;
  std::filesystem::path config_path_;
  std::filesystem::path cache_root_;
  const PadWatcher* pad_;
  std::function<void()> on_close_;
  std::function<void()> on_quit_;
  uint32_t last_buttons_ = 0;
  bool first_frame_ = true;
  bool closing_ = false;
  bool closed_ = false;
  std::string status_;
};

}  // namespace graw
