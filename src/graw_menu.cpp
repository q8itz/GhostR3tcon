#include "graw_menu.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include <SDL3/SDL_gamepad.h>
#include <imgui.h>

#include <rex/logging.h>

#include "graw_settings.h"

namespace graw {

namespace {

constexpr uint32_t Bit(SDL_GamepadButton button) {
  return uint32_t(1) << button;
}

constexpr uint32_t kBackStart = Bit(SDL_GAMEPAD_BUTTON_BACK) | Bit(SDL_GAMEPAD_BUTTON_START);

struct PadKey {
  SDL_GamepadButton button;
  ImGuiKey key;
};

constexpr PadKey kPadKeys[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, ImGuiKey_GamepadFaceDown},
    {SDL_GAMEPAD_BUTTON_EAST, ImGuiKey_GamepadFaceRight},
    {SDL_GAMEPAD_BUTTON_WEST, ImGuiKey_GamepadFaceLeft},
    {SDL_GAMEPAD_BUTTON_NORTH, ImGuiKey_GamepadFaceUp},
    {SDL_GAMEPAD_BUTTON_BACK, ImGuiKey_GamepadBack},
    {SDL_GAMEPAD_BUTTON_START, ImGuiKey_GamepadStart},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, ImGuiKey_GamepadL3},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, ImGuiKey_GamepadR3},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, ImGuiKey_GamepadL1},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, ImGuiKey_GamepadR1},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, ImGuiKey_GamepadDpadUp},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, ImGuiKey_GamepadDpadDown},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, ImGuiKey_GamepadDpadLeft},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, ImGuiKey_GamepadDpadRight},
};

}  // namespace

PadWatcher::PadWatcher(std::function<void()> on_toggle) : on_toggle_(std::move(on_toggle)) {
  registered_ = SDL_AddEventWatch(Watch, this);
  if (!registered_) {
    REXLOG_ERROR("Menu gamepad watch failed: {}", SDL_GetError());
  }
}

PadWatcher::~PadWatcher() {
  if (registered_) {
    SDL_RemoveEventWatch(Watch, this);
  }
}

PadWatcher::State PadWatcher::Get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  State merged;
  for (const auto& [id, pad] : pads_) {
    merged.buttons |= pad.buttons;
    if (std::abs(pad.left_x) > std::abs(merged.left_x)) {
      merged.left_x = pad.left_x;
    }
    if (std::abs(pad.left_y) > std::abs(merged.left_y)) {
      merged.left_y = pad.left_y;
    }
  }
  return merged;
}

bool SDLCALL PadWatcher::Watch(void* user, SDL_Event* event) {
  auto* self = static_cast<PadWatcher*>(user);
  bool toggle = false;
  switch (event->type) {
    case SDL_EVENT_GAMEPAD_REMOVED: {
      std::lock_guard<std::mutex> lock(self->mutex_);
      self->pads_.erase(event->gdevice.which);
      break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
      if (event->gbutton.button >= 32) {
        break;
      }
      std::lock_guard<std::mutex> lock(self->mutex_);
      uint32_t& buttons = self->pads_[event->gbutton.which].buttons;
      uint32_t bit = uint32_t(1) << event->gbutton.button;
      if (!event->gbutton.down) {
        buttons &= ~bit;
        break;
      }
      bool was_combo = (buttons & kBackStart) == kBackStart;
      buttons |= bit;
      toggle = event->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE ||
               (!was_combo && (buttons & kBackStart) == kBackStart);
      break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      std::lock_guard<std::mutex> lock(self->mutex_);
      State& pad = self->pads_[event->gaxis.which];
      if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
        pad.left_x = event->gaxis.value;
      } else if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
        pad.left_y = event->gaxis.value;
      }
      break;
    }
    default:
      break;
  }
  if (toggle && self->on_toggle_) {
    self->on_toggle_();
  }
  return true;
}

MenuDialog::MenuDialog(rex::ui::ImGuiDrawer* drawer, ImFont* font,
                       std::filesystem::path config_path, const PadWatcher* pad,
                       std::function<void()> on_close, std::function<void()> on_quit)
    : ImGuiDialog(drawer),
      font_(font),
      config_path_(std::move(config_path)),
      pad_(pad),
      on_close_(std::move(on_close)),
      on_quit_(std::move(on_quit)) {
  // Buttons already held, such as the ones that opened the menu, are not presses.
  if (pad_) {
    last_buttons_ = pad_->Get().buttons;
  }
}

void MenuDialog::FeedPad(ImGuiIO& io, const PadWatcher::State& pad) {
  uint32_t changed = pad.buttons ^ last_buttons_;  for (const PadKey& mapping : kPadKeys) {
    if (changed & Bit(mapping.button)) {
      io.AddKeyEvent(mapping.key, (pad.buttons & Bit(mapping.button)) != 0);
    }
  }
  auto stick = [&io](ImGuiKey key, float value) {
    constexpr float kDeadZone = 8000.0f;
    float amount = std::clamp((value - kDeadZone) / (32767.0f - kDeadZone), 0.0f, 1.0f);
    io.AddKeyAnalogEvent(key, amount > 0.1f, amount);
  };
  stick(ImGuiKey_GamepadLStickLeft, -float(pad.left_x));
  stick(ImGuiKey_GamepadLStickRight, float(pad.left_x));
  stick(ImGuiKey_GamepadLStickUp, -float(pad.left_y));
  stick(ImGuiKey_GamepadLStickDown, float(pad.left_y));
}

void MenuDialog::OnDraw(ImGuiIO& io) {
  PadWatcher::State pad = pad_ ? pad_->Get() : PadWatcher::State{};
  if (closing_) {
    if (!pad.buttons && !closed_) {
      closed_ = true;
      on_close_();
    }
    return;
  }

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

  uint32_t pressed = pad.buttons & ~last_buttons_;
  bool popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
  FeedPad(io, pad);
  last_buttons_ = pad.buttons;
  if (!popup_open && (pressed & (Bit(SDL_GAMEPAD_BUTTON_EAST) | Bit(SDL_GAMEPAD_BUTTON_START)))) {
    Close();
    return;
  }

  const float font_size = std::clamp(io.DisplaySize.y / 40.0f, 14.0f, 36.0f);
  ImGui::PushFont(font_, font_size);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowBgAlpha(0.94f);
  if (first_frame_) {
    ImGui::SetNextWindowFocus();
  }
  bool open = true;
  if (ImGui::Begin("GRAW menu", &open,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoSavedSettings)) {
    const ImVec2 button_size(ImGui::GetFontSize() * 7.0f, 0.0f);
    if (first_frame_) {
      ImGui::SetKeyboardFocusHere();
      ImGui::SetNavCursorVisible(true);
    }
    if (ImGui::Button("Resume", button_size)) {
      Close();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save settings", button_size)) {
      SaveSettings(config_path_);
      status_ = "Saved to " + config_path_.filename().string();
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit game", button_size)) {
      ImGui::OpenPopup("Quit GRAW?");
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quit GRAW?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Progress since the last checkpoint will be lost.");
      if (ImGui::Button("Quit", button_size)) {
        ImGui::CloseCurrentPopup();
        on_quit_();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", button_size)) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::SetItemDefaultFocus();
      ImGui::EndPopup();
    }

    ImGui::Separator();
    DrawDisplayOptions(true);
    ImGui::Separator();
    ImGui::TextDisabled("Guide or Back+Start: menu    A: select    B: back");
    ImGui::TextDisabled("The game keeps running while the menu is open.");
    if (!status_.empty()) {
      ImGui::TextDisabled("%s", status_.c_str());
    }
  }
  ImGui::End();
  ImGui::PopFont();
  first_frame_ = false;
  if (!open) {
    Close();
  }
}

}  // namespace graw
