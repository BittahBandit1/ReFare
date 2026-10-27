#include "refare_runtime_services.hpp"
#include "Common/Config/Config.h"
#include "Common/IniFile.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/GCPad.h"
#include "VideoCommon/VideoConfig.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

namespace refare {
void ConfigureFramePacing() {
  // Apply defaults only when a user has not explicitly chosen a value.
  const auto set_default=[](const auto& key, auto value) {
    const auto base=Config::GetLayer(Config::LayerType::Base);
    if (base && !base->Exists(key.GetLocation())) Config::SetBase(key,value);
  };
  set_default(Config::GFX_CENTER_ORTHOGRAPHIC_PROJECTION,true);
  set_default(Config::GFX_VSYNC,true);
  set_default(Config::MAIN_PRECISION_FRAME_TIMING,true);
  set_default(Config::MAIN_SMOOTH_EARLY_PRESENTATION,true);
  set_default(Config::MAIN_RUSH_FRAME_PRESENTATION,false);
  set_default(Config::GFX_HACK_IMMEDIATE_XFB,false);
  // Auto uses command-buffer presentation with VSync in the Metal backend.
  set_default(Config::GFX_MTL_USE_PRESENT_DRAWABLE,TriState::Auto);
  set_default(Config::GFX_MTL_MINIMUM_PRESENT_DURATION,-1.0);
  std::fprintf(stderr,"[refare] frame pacing: vsync=%d precise_timer=%d smooth_presentation=%d\n",
    Config::Get(Config::GFX_VSYNC),Config::Get(Config::MAIN_PRECISION_FRAME_TIMING),
    Config::Get(Config::MAIN_SMOOTH_EARLY_PRESENTATION));
}

bool WriteGamepadProfile(const std::filesystem::path& directory,
                         std::string_view gamepad,std::string_view keyboard) {
  if (gamepad.empty()) return false;
  const auto path=directory/"Config/GCPadNew.ini";
  Common::IniFile profile;
  profile.Load(path.string());
  auto* pad=profile.GetOrCreateSection("GCPad1");
  auto* metadata=profile.GetOrCreateSection("ReFare");
  std::string existing,old_keyboard;
  bool managed=false;
  pad->Get("Device",&existing);
  metadata->Get("AutoConfigured",&managed,false);
  metadata->Get("Keyboard",&old_keyboard);
  // Never replace a hand-authored mapping.
  if (!existing.empty() && !managed) return false;
  if (existing==gamepad && old_keyboard==keyboard) return false;
  pad->Set("Device",std::string(gamepad));
  struct Binding {const char* setting; const char* button; const char* key;};
  static constexpr std::array<Binding,24> bindings={{{"Buttons/A","Button S","X"},
    {"Buttons/B","Button E","Z"},{"Buttons/X","Button W","C"},
    {"Buttons/Y","Button N","S"},{"Buttons/Z","Shoulder R","D"},
    {"Buttons/Start","Start","Return"},
#ifdef __APPLE__
    {"Main Stick/Up","Left Y+","Up Arrow"},{"Main Stick/Down","Left Y-","Down Arrow"},
    {"Main Stick/Left","Left X-","Left Arrow"},{"Main Stick/Right","Left X+","Right Arrow"},
#elif defined(_WIN32)
    {"Main Stick/Up","Left Y+","UP"},{"Main Stick/Down","Left Y-","DOWN"},
    {"Main Stick/Left","Left X-","LEFT"},{"Main Stick/Right","Left X+","RIGHT"},
#else
    {"Main Stick/Up","Left Y+","Up"},{"Main Stick/Down","Left Y-","Down"},
    {"Main Stick/Left","Left X-","Left"},{"Main Stick/Right","Left X+","Right"},
#endif
    {"C-Stick/Up","Right Y+","I"},{"C-Stick/Down","Right Y-","K"},
    {"C-Stick/Left","Right X-","J"},{"C-Stick/Right","Right X+","L"},
    {"Triggers/L","Trigger L","Q"},{"Triggers/R","Trigger R","W"},
    {"Triggers/L-Analog","Trigger L","Q"},{"Triggers/R-Analog","Trigger R","W"},
    {"D-Pad/Up","Pad N","T"},{"D-Pad/Down","Pad S","G"},
    {"D-Pad/Left","Pad W","F"},{"D-Pad/Right","Pad E","H"},
    {"Main Stick/Modifier","","Shift"},{"C-Stick/Modifier","","Ctrl"}}};
  for (const auto& binding:bindings) {
    std::string expression;
    if (*binding.button) expression="`"+std::string(binding.button)+"`";
    if (!keyboard.empty()) {
      if (!expression.empty()) expression+=" | ";
      expression+="`"+std::string(keyboard)+":"+binding.key+"`";
    }
    pad->Set(binding.setting,expression);
  }
  pad->Set("Rumble/Motor",std::string("`Motor L` | `Motor R`"));
  metadata->Set("AutoConfigured",true);
  metadata->Set("Keyboard",std::string(keyboard));
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(),ec);
  if (ec || !profile.Save(path.string())) return false;
  std::fprintf(stderr,"[refare] GameCube port 1 mapped to %s (keyboard retained)\n",std::string(gamepad).c_str());
  return true;
}

struct ControllerAutoSetup::Impl {
  std::filesystem::path directory;
  std::jthread worker;
  void Refresh() {
    if (!g_controller_interface.IsInit() || !Pad::IsInitialized()) return;
    std::string controller,keyboard;
    for (const auto& device:g_controller_interface.GetAllDevices()) {
      if (device->GetSource()=="SDL" && device->FindInput("Button S") && device->FindInput("Left X+")) {
        if (controller.empty()) controller=device->GetQualifiedName();
        const auto name=device->GetName();
        if (name.find("PS4")!=std::string::npos || name.find("DualShock")!=std::string::npos ||
            name.find("Wireless Controller")!=std::string::npos) controller=device->GetQualifiedName();
      } else if (device->FindInput("F1") && device->FindInput("X")) keyboard=device->GetQualifiedName();
    }
    if (WriteGamepadProfile(directory,controller,keyboard)) Pad::LoadConfig();
  }
  explicit Impl(std::filesystem::path path):directory(std::move(path)) {
    Refresh();
    worker=std::jthread([this](std::stop_token stop) {
      while (!stop.stop_requested()) {
        for (int i=0;i<10 && !stop.stop_requested();++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!stop.stop_requested()) Refresh();
      }
    });
  }
};
ControllerAutoSetup::ControllerAutoSetup(std::filesystem::path directory)
 :m_impl(std::make_unique<Impl>(std::move(directory))){}
ControllerAutoSetup::~ControllerAutoSetup()=default;
}
