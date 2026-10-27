#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
namespace refare {
void ConfigureFramePacing();
bool WriteGamepadProfile(const std::filesystem::path& directory,
                         std::string_view gamepad, std::string_view keyboard);
// Diagnostic: with REFARE_GFX_TRACE_DIR set, creating <dir>/pacing-request
// records one CSV row per presented frame (10 s) to <dir>/pacing-N.csv.
class FramePacingRecorder {
public:
  FramePacingRecorder();
  ~FramePacingRecorder();
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
class ControllerAutoSetup {
public:
  explicit ControllerAutoSetup(std::filesystem::path directory);
  ~ControllerAutoSetup();
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
}
