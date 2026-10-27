#include "refare_runtime_services.hpp"
#include "Common/Config/Config.h"
#include "Common/IniFile.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/GCPad.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoEvents.h"
#include "VideoCommon/RefarePresentTimes.h"
#include <cstdlib>
#include <filesystem>
#include <string>
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
  // Crazy Taxi tiles menu art from separate textures drawn with wrap
  // addressing. Upscaled, filtering bleeds the opposite tile edge into seams.
  set_default(Config::GFX_HACK_CLAMP_SPRITE_TEXTURE_EDGES,true);
  // Manual sampling wraps in the shader and bypasses the edge clamp above. An
  // earlier ReFare build defaulted it on and Dolphin saved that to GFX.ini, so
  // force the (faster) hardware sampler path rather than only defaulting it.
  Config::SetBase(Config::GFX_HACK_FAST_TEXTURE_SAMPLING,true);
  // Auto uses command-buffer presentation with VSync in the Metal backend.
  set_default(Config::GFX_MTL_USE_PRESENT_DRAWABLE,TriState::Auto);
  set_default(Config::GFX_MTL_MINIMUM_PRESENT_DURATION,-1.0);
  std::fprintf(stderr,"[refare] frame pacing: vsync=%d precise_timer=%d smooth_presentation=%d\n",
    Config::Get(Config::GFX_VSYNC),Config::Get(Config::MAIN_PRECISION_FRAME_TIMING),
    Config::Get(Config::MAIN_SMOOTH_EARLY_PRESENTATION));
}

struct FramePacingRecorder::Impl {
  std::filesystem::path directory;
  Common::EventHook hook;
  std::FILE* file = nullptr;
  int remaining = 0;
  unsigned poll = 0;
  Clock::time_point start{};
  uint64_t glass_tail = 0;
  void OnPresent(const PresentInfo& info) {
    namespace fs = std::filesystem;
    if (!file) {
      if ((poll++ % 30u) != 0u) return;
      std::error_code ec;
      if (!fs::exists(directory / "pacing-request", ec)) return;
      fs::remove(directory / "pacing-request", ec);
      int index = 0;
      while (fs::exists(directory / ("pacing-" + std::to_string(index) + ".csv"), ec)) ++index;
      file = std::fopen((directory / ("pacing-" + std::to_string(index) + ".csv")).string().c_str(), "w");
      if (!file) return;
      remaining = 600;
      start = Clock::now();
      std::fprintf(file, "present,frame,reason,emulated_us,intended_ms,actual_ms,host_ms,accuracy\n");
      glass_tail = RefarePresentTimes::Get().head.load(std::memory_order_acquire);
    }
    // On-glass times (steady_clock and CACurrentMediaTime share mach time).
    {
      auto& ring = RefarePresentTimes::Get();
      const uint64_t head = ring.head.load(std::memory_order_acquire);
      const double start_s = std::chrono::duration<double>(start.time_since_epoch()).count();
      for (; glass_tail < head; ++glass_tail) {
        const double t = ring.seconds[glass_tail % ring.seconds.size()].load(std::memory_order_relaxed);
        std::fprintf(file, "glass,%.3f\n", t > 0.0 ? (t - start_s) * 1000.0 : -1.0);
      }
    }
    const auto ms = [this](Clock::time_point t) {
      return t.time_since_epoch().count() == 0
                 ? -1.0
                 : std::chrono::duration<double, std::milli>(t - start).count();
    };
    std::fprintf(file, "%llu,%llu,%d,%llu,%.3f,%.3f,%.3f,%d\n",
                 static_cast<unsigned long long>(info.present_count),
                 static_cast<unsigned long long>(info.frame_count),
                 static_cast<int>(info.reason),
                 static_cast<unsigned long long>(info.emulated_timestamp),
                 ms(info.intended_present_time), ms(info.actual_present_time),
                 ms(Clock::now()), static_cast<int>(info.present_time_accuracy));
    if (--remaining <= 0) { std::fclose(file); file = nullptr; }
  }
};
FramePacingRecorder::FramePacingRecorder() : m_impl(std::make_unique<Impl>()) {
  const char* dir = std::getenv("REFARE_GFX_TRACE_DIR");
  if (!dir) return;
  m_impl->directory = dir;
  m_impl->hook = GetVideoEvents().after_present_event.Register(
      [impl = m_impl.get()](const PresentInfo& info) { impl->OnPresent(info); });
}
FramePacingRecorder::~FramePacingRecorder() {
  m_impl->hook.reset();
  if (m_impl->file) std::fclose(m_impl->file);
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
