#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
namespace refare {
void ConfigureFramePacing();
bool WriteGamepadProfile(const std::filesystem::path& directory,
                         std::string_view gamepad, std::string_view keyboard);
class ControllerAutoSetup {
public:
  explicit ControllerAutoSetup(std::filesystem::path directory);
  ~ControllerAutoSetup();
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
}
