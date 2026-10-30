#pragma once
#include <array>
#include <functional>
#include <filesystem>
#include <memory>
#include <string>
namespace refare {
enum OptionsButton { Toggle, Up, Down, Left, Right, Back, Confirm, Count };
using OptionsInput = std::array<bool, Count>;
// The controller button and keyboard key that open the options menu, by
// Dolphin input name (SDL controller / macOS keyboard).
struct MenuButtonBinding { std::string pad = "Shoulder L"; std::string key = "Tab"; };
// What is held right now while the menu waits for a new menu button.
struct MenuButtonCapture {
  bool cancel = false;  // Circle or Escape
  bool held = false;    // anything at all is held
  std::string pad;      // a bindable controller button, if one is held
  std::string key;      // a bindable key, if one is held
};
class OptionsMenu {
public:
  // request_exit is called (from the render thread) after the player confirms
  // "Exit The Game". It must only request a shutdown, never block.
  explicit OptionsMenu(std::filesystem::path user_directory,
                       std::function<OptionsInput()> input_source = {},
                       void* render_view = nullptr,
                       std::function<void()> request_exit = {},
                       std::function<MenuButtonCapture()> capture_source = {});
  ~OptionsMenu();
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
bool OptionsMenuBlocksInput();
void PrepareMetalHUD();
MenuButtonBinding LoadMenuButtonBinding(const std::filesystem::path& user_directory);
bool SaveMenuButtonBinding(const std::filesystem::path& user_directory, const MenuButtonBinding& binding);
bool LoadMetalHUDPreference(const std::filesystem::path& user_directory);
}
