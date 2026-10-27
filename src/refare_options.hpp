#pragma once
#include <array>
#include <functional>
#include <filesystem>
#include <memory>
namespace refare {
enum OptionsButton { Toggle, Up, Down, Left, Right, Back, Confirm, Count };
using OptionsInput = std::array<bool, Count>;
class OptionsMenu {
public:
  // request_exit is called (from the render thread) after the player confirms
  // "Exit The Game". It must only request a shutdown, never block.
  explicit OptionsMenu(std::filesystem::path user_directory,
                       std::function<OptionsInput()> input_source = {},
                       void* render_view = nullptr,
                       std::function<void()> request_exit = {});
  ~OptionsMenu();
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
bool OptionsMenuBlocksInput();
void PrepareMetalHUD();
bool LoadMetalHUDPreference(const std::filesystem::path& user_directory);
}
