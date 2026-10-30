#include "refare_options.hpp"
#include "refare_metal_hud.hpp"
#include "refare_runtime_services.hpp"
#include "Common/Config/Config.h"
#include "Common/IniFile.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Host.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "VideoCommon/RefareFrameInterpolation.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoEvents.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <cstdlib>
#include <string>
#include <vector>

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
// Dolphin post-processing shader shipped in Sys/Shaders/FXAA.glsl.
constexpr const char* kFXAAShader = "FXAA";

// Render-only frame interpolation: Off, follow the display's refresh rate,
// or a fixed output rate (Hz). The game itself always runs at 59.94 Hz.
struct InterpChoice { const char* label; bool on; int target; };
constexpr std::array<InterpChoice,6> kInterpChoices = {{
  {"Off",false,0},{"Match display",true,0},{"90 FPS",true,90},
  {"120 FPS",true,120},{"144 FPS",true,144},{"180 FPS",true,180}}};

enum class Row { Scale, Aspect, HudLayout, FXAA, Interpolation, VSync, MetalHUD, MenuButton, Exit };
const std::vector<Row>& Rows() {
  static const std::vector<Row> rows = {Row::Scale, Row::Aspect, Row::HudLayout, Row::FXAA,
    Row::Interpolation, Row::VSync,
#ifdef __APPLE__
    Row::MetalHUD,
#endif
    Row::MenuButton, Row::Exit};
  return rows;
}
const char* RowLabel(Row row) {
  switch (row) {
  case Row::Scale: return "Resolution scale";
  case Row::Aspect: return "Aspect ratio";
  case Row::HudLayout: return "HUD and menus";
  case Row::FXAA: return "FXAA";
  case Row::Interpolation: return "Interpolation (experimental)";
  case Row::VSync: return "V-Sync";
  case Row::MetalHUD: return "Metal HUD";
  case Row::MenuButton: return "Options menu button";
  case Row::Exit: return "Exit The Game";
  }
  return "";
}

// Controller button and keyboard key that open/close this menu, by Dolphin
// input name. Saved in Config/ReFare.ini; render thread only.
MenuButtonBinding s_menu_binding;

// Display name for an SDL controller input (PlayStation labels, like the rest
// of the menu); anything unknown is shown as Dolphin names it.
std::string PadLabel(const std::string& name) {
  static constexpr std::pair<const char*, const char*> labels[] = {
    {"Shoulder L","L1"},{"Shoulder R","R1"},{"Trigger L","L2"},{"Trigger R","R2"},
    {"Thumb L","L3"},{"Thumb R","R3"},{"Button W","Square"},{"Button N","Triangle"},
    {"Button S","Cross"},{"Button E","Circle"},{"Back","Select / Share"},
    {"Start","Start / Options"},{"Guide","Home / PS"},{"Touchpad","Touchpad"}};
  for (const auto& [from, to] : labels) if (name == from) return to;
  return name;
}

// Inputs that can't become the menu button: the menu's own navigation and
// cancel, sticks, motion sensors and the mouse.
bool BindablePad(const std::string& n) {
  for (const char* p : {"Pad ","Left ","Right ","Accel","Gyro","Motion","Battery"})
    if (n.starts_with(p)) return false;
  return n != "Button S" && n != "Button E";
}
bool BindableKey(const std::string& n) {
  for (const char* p : {"Click","Cursor","Axis","Scroll","Up","Down","Left","Right"})
    if (n.starts_with(p)) return false;
  return n != "Escape" && n != "Return" && n != "Keypad Enter";
}

MenuButtonCapture ReadCapture() {
  MenuButtonCapture result;
  if (!Host_RendererHasFocus() || !g_controller_interface.IsInit()) return result;
  for (const auto& device : g_controller_interface.GetAllDevices()) {
    const bool pad = device->GetSource() == "SDL";
    if (!pad && device->GetName().find("Keyboard") == std::string::npos) continue;
    for (const auto* input : device->Inputs()) {
      if (!input->IsDetectable() || input->GetState() <= 0.5) continue;
      const std::string name = input->GetName();
      if (pad ? name == "Button E" : name == "Escape") { result.cancel = true; continue; }
      if (pad ? !BindablePad(name) : !BindableKey(name)) {
        // Navigation still down from choosing the row (e.g. Cross, Enter) must be
        // released first; sticks, motion sensors and the mouse never count.
        if (pad ? (name == "Button S" || name.starts_with("Pad "))
                : !(name.starts_with("Click") || name.starts_with("Cursor") ||
                    name.starts_with("Axis") || name.starts_with("Scroll")))
          result.held = true;
        continue;
      }
      result.held = true;
      if (pad && result.pad.empty()) result.pad = name;
      if (!pad && result.key.empty()) result.key = name;
    }
  }
  return result;
}

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
      result[Toggle] |= !s_menu_binding.pad.empty() && held(s_menu_binding.pad);
      result[Up] |= held("Pad N"); result[Down] |= held("Pad S");
      result[Left] |= held("Pad W"); result[Right] |= held("Pad E");
      result[Back] |= held("Button E");
      result[Confirm] |= held("Button S");
    } else {
      result[Toggle] |= !s_menu_binding.key.empty() && held(s_menu_binding.key);
      result[Up] |= held("Up Arrow") || held("Up");
      result[Down] |= held("Down Arrow") || held("Down");
      result[Left] |= held("Left Arrow") || held("Left");
      result[Right] |= held("Right Arrow") || held("Right");
      result[Back] |= held("Escape");
      result[Confirm] |= held("Return") || held("Keypad Enter");
    }
  }
  return result;
}
}
struct OptionsMenu::Impl {
  std::filesystem::path directory;
  Common::EventHook draw_hook;
  FramePacingRecorder pacing_recorder;
  std::function<OptionsInput()> input_source;
  std::function<void()> request_exit;
  std::function<MenuButtonCapture()> capture_source;
  Buttons previous{};
  bool capturing = false;
  bool capture_armed = false;  // nothing held since capture started
  mutable std::string binding_text;
  bool open = false;
  bool drain = false;
  int selected = 0;
  int scale_index = 0;
  int aspect_index = 0;
  bool center_hud = true;
  bool fxaa = false;
  int interp_index = 0;
  mutable std::string value_text;
  bool vsync = true;
  bool confirm_exit = false;
  int exit_choice = 0;  // 0 = No (left, default), 1 = Yes (right)
  bool exiting = false;
  std::string save_error;
  void* render_view = nullptr;
  bool metal_hud = true;
  bool hud_applied = false;

  Impl(std::filesystem::path path, std::function<OptionsInput()> source, void* view,
       std::function<void()> exit, std::function<MenuButtonCapture()> capture)
      : directory(std::move(path)), input_source(source ? std::move(source) : ReadButtons),
        request_exit(std::move(exit)),
        capture_source(capture ? std::move(capture) : ReadCapture), render_view(view),
        metal_hud(LoadMetalHUDPreference(directory)) {
    s_menu_binding = LoadMenuButtonBinding(directory);
    binding = s_menu_binding;
    draw_hook = GetVideoEvents().imgui_draw_event.Register([this] { Draw(); });
  }
  ~Impl() { draw_hook.reset(); s_blocks_input = false; }
  Row CurrentRow() const { return Rows()[selected]; }
  void ReadSettings() {
    metal_hud = LoadMetalHUDPreference(directory);
    center_hud = Config::Get(Config::GFX_CENTER_ORTHOGRAPHIC_PROJECTION);
    fxaa = Config::Get(Config::GFX_ENHANCE_POST_SHADER) == kFXAAShader;
    {
      const bool on = Config::Get(Config::GFX_REFARE_FRAME_INTERPOLATION);
      const int target = Config::Get(Config::GFX_REFARE_INTERPOLATION_TARGET);
      interp_index = on ? 1 : 0;
      for (int i=2; on && i<static_cast<int>(kInterpChoices.size()); ++i)
        if (kInterpChoices[i].target == target) interp_index = i;
    }
    vsync = Config::Get(Config::GFX_VSYNC);
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
  // Save only the given GFX.ini keys, retaining all unrelated user settings.
  template <typename Fn> void SaveGraphics(Fn&& write) {
    Common::IniFile graphics;
    const auto file = (directory / "Config" / "GFX.ini").string();
    graphics.Load(file);
    write(graphics);
    std::error_code ec;
    std::filesystem::create_directories(directory / "Config", ec);
    save_error = (!ec && graphics.Save(file)) ? "" : "Could not save video settings; change applies this session.";
  }
  void ApplyScale() {
    Config::SetBaseOrCurrent(Config::GFX_EFB_SCALE, scales[scale_index].value);
    // The frontend applies this setting at boot; keep it in sync with GFX.ini.
    Common::IniFile frontend;
    const auto file = (directory / "config.ini").string();
    if (!frontend.Load(file)) { save_error = "Could not read settings; change applies this session."; return; }
    frontend.GetOrCreateSection("Video")->Set("resolution", std::string(scales[scale_index].resolution));
    if (!frontend.Save(file)) { save_error = "Could not save resolution; change applies this session."; return; }
    SaveGraphics([&](Common::IniFile& ini) {
      ini.GetOrCreateSection("Settings")->Set("InternalResolution", scales[scale_index].value);
    });
  }
  void ApplyAspect() {
    const auto& aspect = aspects[aspect_index];
    Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, aspect.mode);
    Config::SetBaseOrCurrent(Config::GFX_CUSTOM_ASPECT_RATIO_WIDTH, aspect.width);
    Config::SetBaseOrCurrent(Config::GFX_CUSTOM_ASPECT_RATIO_HEIGHT, aspect.height);
    Config::SetBaseOrCurrent(Config::GFX_WIDESCREEN_HACK, aspect_index != 1);
    Config::SetBaseOrCurrent(Config::GFX_SUGGESTED_ASPECT_RATIO, AspectMode::ForceStandard);
    Config::SetBaseOrCurrent(Config::GFX_CROP_TO_ASPECT_RATIO, false);
    SaveGraphics([&](Common::IniFile& ini) {
      auto* section = ini.GetOrCreateSection("Settings");
      section->Set("AspectRatio", static_cast<int>(aspect.mode));
      section->Set("CustomAspectRatioWidth", aspect.width);
      section->Set("CustomAspectRatioHeight", aspect.height);
      section->Set("wideScreenHack", aspect_index != 1);
      section->Set("SuggestedAspectRatio", 2);
      section->Set("Crop", false);
    });
  }
  void ApplyHudLayout() {
    // Takes effect on the next projection load; VertexShaderManager watches it.
    Config::SetBaseOrCurrent(Config::GFX_CENTER_ORTHOGRAPHIC_PROJECTION, center_hud);
    SaveGraphics([&](Common::IniFile& ini) {
      ini.GetOrCreateSection("Settings")->Set("CenterOrthographicProjection", center_hud);
    });
  }
  void ApplyFXAA() {
    // The presenter recompiles its post-processing pipeline on the next frame.
    Config::SetBaseOrCurrent(Config::GFX_ENHANCE_POST_SHADER, std::string(fxaa ? kFXAAShader : ""));
    SaveGraphics([&](Common::IniFile& ini) {
      ini.GetOrCreateSection("Enhancements")->Set("PostProcessingShader", std::string(fxaa ? kFXAAShader : ""));
    });
  }
  void ApplyInterpolation() {
    // Render-only: the game still runs at 59.94 Hz; the video thread draws
    // in-between frames for the extra display refreshes. Takes effect next frame.
    const InterpChoice& choice = kInterpChoices[interp_index];
    Config::SetBaseOrCurrent(Config::GFX_REFARE_INTERPOLATION_TARGET, choice.target);
    Config::SetBaseOrCurrent(Config::GFX_REFARE_FRAME_INTERPOLATION, choice.on);
    SaveGraphics([&](Common::IniFile& ini) {
      auto* section = ini.GetOrCreateSection("Enhancements");
      section->Set("RefareFrameInterpolation", choice.on);
      section->Set("RefareInterpolationTarget", choice.target);
    });
  }
  void ApplyVSync() {
    // Off lets presents run unthrottled (tearing possible); applies live.
    Config::SetBaseOrCurrent(Config::GFX_VSYNC, vsync);
    SaveGraphics([&](Common::IniFile& ini) {
      ini.GetOrCreateSection("Hardware")->Set("VSync", vsync);
    });
  }
  void ApplyMetalHUD() {
    SetMetalHUDVisible(render_view, metal_hud);
    Common::IniFile frontend;
    const auto file = (directory / "config.ini").string();
    if (!frontend.Load(file)) { save_error = "Could not read settings; HUD change applies this session."; return; }
    frontend.GetOrCreateSection("Video")->Set("metal_hud", metal_hud);
    save_error = frontend.Save(file) ? "" : "Could not save HUD setting; change applies this session.";
  }
  void SaveMenuButton() {
    s_menu_binding = binding;
    save_error = SaveMenuButtonBinding(directory, binding) ? "" : "Could not save the menu button; change applies this session.";
  }
  MenuButtonBinding binding = s_menu_binding;
  void StartCapture() { capturing = true; capture_armed = false; }
  // Waits for every input to be released (the press that chose the row),
  // then binds the next controller button or key. Circle / Escape cancel.
  void UpdateCapture() {
    const MenuButtonCapture c = capture_source();
    if (c.cancel) { capturing = false; previous.fill(true); return; }
    if (!capture_armed) { capture_armed = !c.held; return; }
    if (c.pad.empty() && c.key.empty()) return;
    // A controller press changes only the controller button, a key only the key.
    if (!c.pad.empty()) binding.pad = c.pad;
    if (!c.key.empty()) binding.key = c.key;
    SaveMenuButton();
    capturing = false;
    previous.fill(true);  // the new button is held now: don't let it close the menu
  }
  void Step(int delta) {
    switch (CurrentRow()) {
    case Row::Scale:
      scale_index = std::clamp(scale_index + delta, 0, static_cast<int>(scales.size())-1);
      ApplyScale(); break;
    case Row::Aspect: {
      const int count = static_cast<int>(aspects.size());
      aspect_index = (aspect_index + delta + count) % count;
      ApplyAspect(); break;
    }
    case Row::HudLayout: center_hud = !center_hud; ApplyHudLayout(); break;
    case Row::FXAA: fxaa = !fxaa; ApplyFXAA(); break;
    case Row::Interpolation: {
      const int count = static_cast<int>(kInterpChoices.size());
      interp_index = (interp_index + delta + count) % count;
      ApplyInterpolation(); break;
    }
    case Row::VSync: vsync = !vsync; ApplyVSync(); break;
    case Row::MetalHUD: metal_hud = !metal_hud; ApplyMetalHUD(); break;
    case Row::MenuButton:  // Left/Right restore the default L1 / Tab.
      if (binding.pad != MenuButtonBinding{}.pad || binding.key != MenuButtonBinding{}.key) {
        binding = MenuButtonBinding{}; SaveMenuButton();
      }
      break;
    case Row::Exit: break;  // Needs an explicit confirm, never a D-pad nudge.
    }
  }
  void Activate() {
    switch (CurrentRow()) {
    case Row::Exit: confirm_exit = true; exit_choice = 0; break;
    case Row::MenuButton: StartCapture(); break;
    case Row::HudLayout: case Row::FXAA: case Row::Interpolation: case Row::VSync: case Row::MetalHUD: Step(1); break;
    default: break;
    }
  }
  void Close() { open = false; confirm_exit = false; capturing = false; drain = true; }
  void FinishExit() {
    exiting = true;
    Close();
    if (request_exit) request_exit();
  }
  const char* RowValue(Row row) const {
    switch (row) {
    case Row::Scale: return scales[scale_index].label;
    case Row::Aspect: return aspects[aspect_index].label;
    case Row::HudLayout: return center_hud ? "Centered (4:3)" : "Stretched";
    case Row::FXAA: return fxaa ? "On" : "Off";
    case Row::Interpolation: {
      if (interp_index != 1) return kInterpChoices[interp_index].label;
      const double rate = RefareFrameInterpolation::OutputRate();
      if (rate <= 0.0) return "Match display";
      value_text = "Match display (" + std::to_string(static_cast<int>(rate + 0.5)) + " FPS)";
      return value_text.c_str();
    }
    case Row::VSync: return vsync ? "On" : "Off";
    case Row::MetalHUD: return metal_hud ? "On" : "Off";
    case Row::MenuButton:
      if (capturing) return "Press a button or key...";
      binding_text = PadLabel(binding.pad) + " / " + binding.key;
      return binding_text.c_str();
    case Row::Exit: return "";
    }
    return "";
  }
  void HandleInput(const Buttons& pressed) {
    if (confirm_exit) {
      if (pressed[Left]) exit_choice = 0;
      if (pressed[Right]) exit_choice = 1;
      if (pressed[Back]) confirm_exit = false;
      else if (pressed[Confirm]) {
        if (exit_choice == 1) FinishExit();
        else confirm_exit = false;
      }
      return;
    }
    const int count = static_cast<int>(Rows().size());
    if (pressed[Up]) selected = (selected + count - 1) % count;
    if (pressed[Down]) selected = (selected + 1) % count;
    if (pressed[Left]) Step(-1);
    if (pressed[Right]) Step(1);
    if (pressed[Confirm]) Activate();
  }
  void DrawConfirmExit() {
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*0.5f,io.DisplaySize.y*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(360.0f, io.DisplaySize.x - 24.0f),0));
    ImGui::SetNextWindowFocus();
    ImGui::Begin("ReFare exit",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
    const char* question = "Are you sure?";
    ImGui::SetCursorPosX((ImGui::GetWindowWidth()-ImGui::CalcTextSize(question).x)*0.5f);
    ImGui::TextUnformatted(question);
    ImGui::Spacing(); ImGui::Spacing();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float button = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    for (int choice = 0; choice < 2; ++choice) {
      if (choice) ImGui::SameLine();
      const bool highlighted = exit_choice == choice;
      ImGui::PushStyleColor(ImGuiCol_Button, highlighted ? ImVec4(1,0.75f,0.12f,1) : ImVec4(0.23f,0.25f,0.29f,1));
      ImGui::PushStyleColor(ImGuiCol_Text, highlighted ? ImVec4(0.05f,0.05f,0.05f,1) : ImVec4(1,1,1,1));
      if (ImGui::Button(choice ? "Yes" : "No", ImVec2(button, 38))) {
        exit_choice = choice;
        if (choice) FinishExit(); else confirm_exit = false;
      }
      ImGui::PopStyleColor(2);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("D-pad: choose     Cross / Enter: confirm");
    ImGui::End();
  }
  void Draw() {
    if (!ImGui::GetCurrentContext()) return;
    // Render thread, every present: keep "Match display" on the right rate.
    RefareFrameInterpolation::SetDisplayRate(DisplayRefreshRate(render_view));
    if (!hud_applied) {
      PrioritizeCurrentThread();  // Render thread.
      // With MTL_HUD_ENABLED set at launch the HUD is already visible. Only
      // touch the layer for a saved Off; re-enabling it here would register a
      // second, layer-only HUD that cannot measure GPU time.
      if (!metal_hud) SetMetalHUDVisible(render_view, false);
      hud_applied = true;
    }
    const Buttons down = input_source();
    Buttons pressed{};
    for (int i=0;i<Count;++i) pressed[i] = down[i] && !previous[i];
    previous = down;
    if (exiting) { s_blocks_input = true; return; }
    if (capturing) UpdateCapture();
    else if (pressed[Toggle]) {
      if (open) Close();
      else { open = true; selected = 0; confirm_exit = false; ReadSettings(); }
    } else if (open && pressed[Back] && !confirm_exit) Close();
    else if (open) HandleInput(pressed);
    if (drain && std::none_of(down.begin(), down.end(), [](bool b){return b;})) drain = false;
    s_blocks_input = open || drain;
    if (!open) return;
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
    const auto& rows = Rows();
    for (int index=0; index<static_cast<int>(rows.size()); ++index) {
      const Row row = rows[index];
      ImGui::PushID(index);
      if (row == Row::MenuButton) {
        if (ImGui::Selectable(RowLabel(row),selected==index)) selected=index;
        ImGui::TextUnformatted(RowValue(row));
        ImGui::SameLine(ImGui::GetWindowWidth()-ImGui::GetStyle().WindowPadding.x-170);
        if (ImGui::Button(capturing ? "Cancel" : "Change",ImVec2(84,32))) {
          selected=index; if (capturing) capturing=false; else StartCapture();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset",ImVec2(78,32))) {selected=index; capturing=false; Step(1);}
      } else if (row == Row::Exit) {
        ImGui::Separator(); ImGui::Spacing();
        const bool highlighted = selected == index;
        ImGui::PushStyleColor(ImGuiCol_Button, highlighted ? ImVec4(0.72f,0.16f,0.14f,1) : ImVec4(0.23f,0.25f,0.29f,1));
        if (ImGui::Button(RowLabel(row), ImVec2(-1,36))) { selected = index; Activate(); }
        ImGui::PopStyleColor();
      } else {
        if (ImGui::Selectable(RowLabel(row),selected==index)) selected=index;
        if (ImGui::Button("<",ImVec2(42,32))) {selected=index; Step(-1);}
        ImGui::SameLine();
        ImGui::TextUnformatted(RowValue(row));
        ImGui::SameLine(ImGui::GetWindowWidth()-ImGui::GetStyle().WindowPadding.x-42);
        if (ImGui::Button(">",ImVec2(42,32))) {selected=index; Step(1);}
      }
      ImGui::Spacing(); ImGui::PopID();
    }
    ImGui::Separator(); ImGui::Spacing();
    ImGui::TextWrapped("Changes apply immediately and are saved.");
    if (capturing) {
      ImGui::TextWrapped("Press the controller button or key that should open this menu. Circle / Escape: cancel.");
    } else {
      const std::string pad = "D-pad: select / change     Cross: select     " + PadLabel(s_menu_binding.pad) + " or Circle: close";
      const std::string keys = "Keyboard: " + s_menu_binding.key + ", arrow keys, Enter, Escape";
      ImGui::TextDisabled("%s", pad.c_str());
      ImGui::TextDisabled("%s", keys.c_str());
    }
    if (!save_error.empty()) ImGui::TextWrapped("%s",save_error.c_str());
    if (ImGui::Button("Done",ImVec2(-1,34))) Close();
    ImGui::End();
    ImGui::PopStyleColor(2); ImGui::PopStyleVar(2);
    if (confirm_exit) DrawConfirmExit();
    s_blocks_input = open || drain || exiting;
  }
};
OptionsMenu::OptionsMenu(std::filesystem::path directory, std::function<OptionsInput()> source,
                         void* render_view, std::function<void()> request_exit,
                         std::function<MenuButtonCapture()> capture_source)
  : m_impl(std::make_unique<Impl>(std::move(directory),std::move(source),render_view,
                                  std::move(request_exit),std::move(capture_source))) {}
OptionsMenu::~OptionsMenu() = default;
bool OptionsMenuBlocksInput() {return s_blocks_input.load();}
void PrepareMetalHUD() {
#ifdef __APPLE__
  // Enable the HUD machinery before Metal device creation, even for a saved
  // Off preference, so the layer can be toggled back on without restarting.
  setenv("MTL_HUD_ENABLED", "1", 0);
  KeepGameResponsive();
#endif
}
// Config/ReFare.ini, which only ReFare writes ([Controls] MenuPad, MenuKey).
MenuButtonBinding LoadMenuButtonBinding(const std::filesystem::path& directory) {
  Common::IniFile ini;
  ini.Load((directory / "Config" / "ReFare.ini").string());
  MenuButtonBinding binding;
  auto* section = ini.GetOrCreateSection("Controls");
  section->Get("MenuPad", &binding.pad, binding.pad);
  section->Get("MenuKey", &binding.key, binding.key);
  if (binding.pad.empty() || !BindablePad(binding.pad)) binding.pad = MenuButtonBinding{}.pad;
  if (binding.key.empty() || !BindableKey(binding.key)) binding.key = MenuButtonBinding{}.key;
  return binding;
}
bool SaveMenuButtonBinding(const std::filesystem::path& directory, const MenuButtonBinding& binding) {
  Common::IniFile ini;
  const auto file = (directory / "Config" / "ReFare.ini").string();
  ini.Load(file);
  auto* section = ini.GetOrCreateSection("Controls");
  section->Set("MenuPad", binding.pad);
  section->Set("MenuKey", binding.key);
  std::error_code ec;
  std::filesystem::create_directories(directory / "Config", ec);
  return !ec && ini.Save(file);
}
bool LoadMetalHUDPreference(const std::filesystem::path& directory) {
  Common::IniFile frontend;
  frontend.Load((directory / "config.ini").string());
  bool enabled = true;
  frontend.GetOrCreateSection("Video")->Get("metal_hud", &enabled, true);
  return enabled;
}
}
