#include "refare_options.hpp"
#include "refare_metal_hud.hpp"
#include "Common/Config/Config.h"
#include "Common/IniFile.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Host.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoEvents.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <string>

namespace refare {
namespace {
std::atomic<bool> s_blocks_input{false};
struct Scale { int value; const char* label; const char* resolution; };
constexpr std::array<Scale, 7> scales = {{{1,"1x (native)","640x528"},
 {2,"2x","1280x720"},{3,"3x","1920x1080"},{4,"4x","2560x1440"},
 {6,"6x","3840x2160"},{8,"8x","5120x2880"},{12,"12x","7680x4320"}}};
struct Aspect { const char* label; AspectMode mode; int width; int height; };
constexpr std::array<Aspect, 6> aspects = {{{"Match window / display",AspectMode::Stretch,4,3},
 {"4:3 (original)",AspectMode::ForceStandard,4,3},
 {"16:9",AspectMode::CustomStretch,16,9},{"16:10",AspectMode::CustomStretch,16,10},
 {"21:9",AspectMode::CustomStretch,21,9},{"32:9",AspectMode::CustomStretch,32,9}}};
#ifdef __APPLE__
constexpr int kOptionCount = 3;
#else
constexpr int kOptionCount = 2;
#endif
using Buttons = OptionsInput;
Buttons ReadButtons() {
  Buttons result{};
  if (!Host_RendererHasFocus() || !g_controller_interface.IsInit()) return result;
  // Normal SI polling updates devices. Do not poll them again on the render
  // thread: that contends with game input and adds jitter to presentation.
  for (const auto& device : g_controller_interface.GetAllDevices()) {
    const auto held = [&](std::string_view name) {
      auto* input = device->FindInput(name);
      return input && input->GetState() > 0.5;
    };
    if (device->GetSource() == "SDL") {
      result[Toggle] |= held("Shoulder L");
      result[Up] |= held("Pad N"); result[Down] |= held("Pad S");
      result[Left] |= held("Pad W"); result[Right] |= held("Pad E");
      result[Back] |= held("Button E");
    } else {
      result[Toggle] |= held("F1");
      result[Up] |= held("Up Arrow") || held("Up");
      result[Down] |= held("Down Arrow") || held("Down");
      result[Left] |= held("Left Arrow") || held("Left");
      result[Right] |= held("Right Arrow") || held("Right");
      result[Back] |= held("Escape");
    }
  }
  return result;
}
}
struct OptionsMenu::Impl {
  std::filesystem::path directory;
  Common::EventHook draw_hook;
  std::function<OptionsInput()> input_source;
  Buttons previous{};
  bool open = false;
  bool drain = false;
  int selected = 0;
  int scale_index = 0;
  int aspect_index = 0;
  std::string save_error;
  void* render_view = nullptr;
  bool metal_hud = true;
  bool hud_applied = false;

  explicit Impl(std::filesystem::path path, std::function<OptionsInput()> source, void* view)
      : directory(std::move(path)), input_source(source ? std::move(source) : ReadButtons),
        render_view(view), metal_hud(LoadMetalHUDPreference(directory)) {
    draw_hook = GetVideoEvents().imgui_draw_event.Register([this] { Draw(); });
  }
  ~Impl() { draw_hook.reset(); s_blocks_input = false; }
  void ReadSettings() {
    metal_hud = LoadMetalHUDPreference(directory);
    const int scale = Config::Get(Config::GFX_EFB_SCALE);
    scale_index = 0;
    for (int i=0; i<static_cast<int>(scales.size()); ++i)
      if (scales[i].value == scale) scale_index = i;
    const auto mode = Config::Get(Config::GFX_ASPECT_RATIO);
    aspect_index = 0;
    for (int i=0; i<static_cast<int>(aspects.size()); ++i) {
      if (aspects[i].mode != mode) continue;
      if (mode == AspectMode::CustomStretch &&
          (Config::Get(Config::GFX_CUSTOM_ASPECT_RATIO_WIDTH) != aspects[i].width ||
           Config::Get(Config::GFX_CUSTOM_ASPECT_RATIO_HEIGHT) != aspects[i].height)) continue;
      aspect_index = i;
    }
  }
  void Apply(bool scale_changed) {
    if (scale_changed) {
      Config::SetBaseOrCurrent(Config::GFX_EFB_SCALE, scales[scale_index].value);
      // The frontend applies this setting at boot; keep it in sync with GFX.ini.
      Common::IniFile frontend;
      const auto file = (directory / "config.ini").string();
      if (!frontend.Load(file)) { save_error = "Could not read settings; change applies this session."; return; }
      frontend.GetOrCreateSection("Video")->Set("resolution", std::string(scales[scale_index].resolution));
      if (!frontend.Save(file)) { save_error = "Could not save resolution; change applies this session."; return; }
    } else {
      const auto& aspect = aspects[aspect_index];
      Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, aspect.mode);
      Config::SetBaseOrCurrent(Config::GFX_CUSTOM_ASPECT_RATIO_WIDTH, aspect.width);
      Config::SetBaseOrCurrent(Config::GFX_CUSTOM_ASPECT_RATIO_HEIGHT, aspect.height);
      Config::SetBaseOrCurrent(Config::GFX_WIDESCREEN_HACK, aspect_index != 1);
      Config::SetBaseOrCurrent(Config::GFX_SUGGESTED_ASPECT_RATIO, AspectMode::ForceStandard);
      Config::SetBaseOrCurrent(Config::GFX_CROP_TO_ASPECT_RATIO, false);
    }
    // Save just these video keys, retaining all unrelated user settings.
    Common::IniFile graphics;
    const auto file = (directory / "Config" / "GFX.ini").string();
    graphics.Load(file);
    auto* section = graphics.GetOrCreateSection("Settings");
    if (scale_changed) section->Set("InternalResolution", scales[scale_index].value);
    else {
      const auto& aspect = aspects[aspect_index];
      section->Set("AspectRatio", static_cast<int>(aspect.mode));
      section->Set("CustomAspectRatioWidth", aspect.width);
      section->Set("CustomAspectRatioHeight", aspect.height);
      section->Set("wideScreenHack", aspect_index != 1);
      section->Set("SuggestedAspectRatio", 2);
      section->Set("Crop", false);
    }
    std::error_code ec;
    std::filesystem::create_directories(directory / "Config", ec);
    save_error = (!ec && graphics.Save(file)) ? "" : "Could not save video settings; change applies this session.";
  }
  void Step(int delta) {
    if (selected == 2) {
      metal_hud = !metal_hud;
      SetMetalHUDVisible(render_view, metal_hud);
      Common::IniFile frontend;
      const auto file = (directory / "config.ini").string();
      if (!frontend.Load(file)) { save_error = "Could not read settings; HUD change applies this session."; return; }
      frontend.GetOrCreateSection("Video")->Set("metal_hud", metal_hud);
      save_error = frontend.Save(file) ? "" : "Could not save HUD setting; change applies this session.";
    } else if (selected == 0) {
      scale_index = std::clamp(scale_index + delta, 0, static_cast<int>(scales.size())-1);
      Apply(true);
    } else {
      const int count = static_cast<int>(aspects.size());
      aspect_index = (aspect_index + delta + count) % count;
      Apply(false);
    }
  }
  void Close() { open = false; drain = true; }
  void Draw() {
    if (!ImGui::GetCurrentContext()) return;
    if (!hud_applied) {
      // The first UI frame guarantees that the backend has attached its layer.
      SetMetalHUDVisible(render_view, metal_hud);
      hud_applied = true;
    }
    const Buttons down = input_source();
    Buttons pressed{};
    for (int i=0;i<Count;++i) pressed[i] = down[i] && !previous[i];
    previous = down;
    if (pressed[Toggle]) {
      if (open) Close();
      else { open = true; selected = 0; ReadSettings(); }
    } else if (open && pressed[Back]) Close();
    if (drain && std::none_of(down.begin(), down.end(), [](bool b){return b;})) drain = false;
    s_blocks_input = open || drain;
    if (!open) return;
    if (pressed[Up]) selected = (selected + kOptionCount - 1) % kOptionCount;
    if (pressed[Down]) selected = (selected + 1) % kOptionCount;
    if (pressed[Left]) Step(-1);
    if (pressed[Right]) Step(1);
    const auto& io = ImGui::GetIO();
    const float width = std::min(520.0f, io.DisplaySize.x - 24.0f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*0.5f,io.DisplaySize.y*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSize(ImVec2(width,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(24,22));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,12);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(0.055f,0.065f,0.08f,0.98f));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.23f,0.25f,0.29f,1));
    ImGui::Begin("ReFare options",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(1,0.75f,0.12f,1),"REFARE  /  OPTIONS");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    for (int row=0;row<kOptionCount;++row) {
      ImGui::PushID(row);
      if (ImGui::Selectable(row==0 ? "Resolution scale" : row==1 ? "Aspect ratio" : "Metal HUD",selected==row)) selected=row;
      if (ImGui::Button("<",ImVec2(42,32))) {selected=row; Step(-1);}
      ImGui::SameLine();
      ImGui::TextUnformatted(row==0 ? scales[scale_index].label : row==1 ? aspects[aspect_index].label : metal_hud ? "On" : "Off");
      ImGui::SameLine(ImGui::GetWindowWidth()-ImGui::GetStyle().WindowPadding.x-42);
      if (ImGui::Button(">",ImVec2(42,32))) {selected=row; Step(1);}
      ImGui::Spacing(); ImGui::PopID();
    }
    ImGui::Separator(); ImGui::Spacing();
    ImGui::TextWrapped("Changes apply immediately and are saved.");
    ImGui::TextDisabled("D-pad: select / change     L1 or Circle: close");
    ImGui::TextDisabled("Keyboard: F1, arrow keys, Escape");
    if (!save_error.empty()) ImGui::TextWrapped("%s",save_error.c_str());
    if (ImGui::Button("Done",ImVec2(-1,34))) Close();
    ImGui::End();
    ImGui::PopStyleColor(2); ImGui::PopStyleVar(2);
    s_blocks_input = open || drain;
  }
};
OptionsMenu::OptionsMenu(std::filesystem::path directory, std::function<OptionsInput()> source, void* render_view)
  : m_impl(std::make_unique<Impl>(std::move(directory),std::move(source),render_view)) {}
OptionsMenu::~OptionsMenu() = default;
bool OptionsMenuBlocksInput() {return s_blocks_input.load();}
void PrepareMetalHUD() {
#ifdef __APPLE__
  // Enable the HUD machinery before Metal device creation, even for a saved
  // Off preference, so the layer can be toggled back on without restarting.
  setenv("MTL_HUD_ENABLED", "1", 1);
#endif
}
bool LoadMetalHUDPreference(const std::filesystem::path& directory) {
  Common::IniFile frontend;
  frontend.Load((directory / "config.ini").string());
  bool enabled = true;
  frontend.GetOrCreateSection("Video")->Get("metal_hud", &enabled, true);
  return enabled;
}
}
