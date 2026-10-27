#pragma once
#include <array>
#include <functional>
#include <filesystem>
#include <memory>
namespace refare {
enum OptionsButton { Toggle, Up, Down, Left, Right, Back, Count };
using OptionsInput = std::array<bool, Count>;
class OptionsMenu {
public:
  explicit OptionsMenu(std::filesystem::path user_directory,
                       std::function<OptionsInput()> input_source = {},
                       void* render_view = nullptr);
  ~OptionsMenu();
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
bool OptionsMenuBlocksInput();
void PrepareMetalHUD();
bool LoadMetalHUDPreference(const std::filesystem::path& user_directory);
}
