#include "refare_options.hpp"
#include "refare_runtime_services.hpp"
#include "frontend_config.hpp"
#include "Common/Config/Config.h"
#include "Common/IniFile.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoEvents.h"
#include "imgui.h"
#include <filesystem>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
class BaseLayer : public Config::ConfigLayerLoader {
public:
  BaseLayer() : ConfigLayerLoader(Config::LayerType::Base) {}
  void Load(Config::Layer*) override {}
  void Save(Config::Layer*) override {}
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::filesystem::path directory = argv[1];
  std::filesystem::create_directories(directory / "Config");
  std::ofstream(directory/"config.ini") << "[Video]\nresolution=1920x1080\n[Input]\ncontroller1=Preserved\n";
  std::ofstream(directory/"Config/GFX.ini") << "[Settings]\nVSync = True\n";
  Check(refare::LoadMetalHUDPreference(directory),"Metal HUD is not enabled by default");
  refare::PrepareMetalHUD();
#ifdef __APPLE__
  Check(std::string(std::getenv("MTL_HUD_ENABLED"))=="1","HUD machinery not enabled at startup");
#endif
  Config::Init();
  Config::AddLayer(std::make_unique<BaseLayer>());
  refare::ConfigureFramePacing();
  Check(Config::Get(Config::GFX_VSYNC),"Pacing defaults did not enable VSync");
  Check(Config::Get(Config::MAIN_SMOOTH_EARLY_PRESENTATION),"Presentation smoothing default missing");
  Config::SetBase(Config::GFX_VSYNC,false);
  refare::ConfigureFramePacing();
  Check(!Config::Get(Config::GFX_VSYNC),"Explicit user VSync preference overwritten");
  const auto pad_directory=directory/"controller-profile-test";
  std::filesystem::remove_all(pad_directory);
  Check(refare::WriteGamepadProfile(pad_directory,"SDL/0/PS4 Controller","Quartz/0/Keyboard & Mouse"),"Empty controller profile not configured");
  Common::IniFile pad_profile;
  const auto pad_file=(pad_directory/"Config/GCPadNew.ini").string();
  pad_profile.Load(pad_file);
  std::string cross,start,steering;
  pad_profile.GetOrCreateSection("GCPad1")->Get("Buttons/A",&cross);
  pad_profile.GetOrCreateSection("GCPad1")->Get("Buttons/Start",&start);
  pad_profile.GetOrCreateSection("GCPad1")->Get("Main Stick/Left",&steering);
  Check(cross.find("Button S")!=std::string::npos && cross.find("Keyboard & Mouse:X")!=std::string::npos,"Cross/keyboard mapping missing");
  Check(start.find("Start")!=std::string::npos && steering.find("Left X-")!=std::string::npos,"Start/steering mapping missing");
  Check(!refare::WriteGamepadProfile(pad_directory,"SDL/0/PS4 Controller","Quartz/0/Keyboard & Mouse"),"Unchanged profile unnecessarily rewritten");
  Check(refare::WriteGamepadProfile(pad_directory,"SDL/1/PS4 Controller","Quartz/0/Keyboard & Mouse"),"Reconnected controller ID not updated");
  pad_profile.Load(pad_file);
  pad_profile.GetOrCreateSection("ReFare")->Set("AutoConfigured",false);
  pad_profile.Save(pad_file);
  Check(!refare::WriteGamepadProfile(pad_directory,"SDL/0/Other Pad",""),"Custom controller profile overwritten");
  Check(Config::Get(Config::GFX_CENTER_ORTHOGRAPHIC_PROJECTION),"Centered HUD default missing");
  for (const auto& [width,height] : {std::pair{1920,1080},std::pair{3440,1440},std::pair{1080,1920},std::pair{1440,1080}}) {
    const float aspect=float(width)/height;
    const float x=std::min(1.0f,(4.0f/3.0f)/aspect);
    const float y=std::min(1.0f,aspect/(4.0f/3.0f));
    std::array<float,16> projection={2.0f/640,0,0,-1, 0,-2.0f/480,0,1, 0,0,-0.0001f,-0.5f, 0,0,0,1};
    const auto original=projection;
    VertexShaderManager::CenterOrthographicProjection(projection,x,y);
    Check(std::abs(projection[0]*320+projection[3])<0.00001f && std::abs(projection[5]*240+projection[7])<0.00001f,"HUD center shifted");
    const float pixel_width=64*projection[0]*width/2;
    const float pixel_height=-64*projection[5]*height/2;
    Check(std::abs(pixel_width-pixel_height)<0.001f,"Square HUD element distorted");
    Check(std::equal(projection.begin()+8,projection.end(),original.begin()+8),"HUD correction changed depth/W");
    if (width==1440) Check(projection==original,"Native 4:3 projection changed");
  }
  Config::SetBase(Config::GFX_EFB_SCALE,3);
  Config::SetBase(Config::GFX_ASPECT_RATIO,AspectMode::Stretch);
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.DisplaySize=ImVec2(1280,720); io.DeltaTime=1.0f/60;
  io.IniFilename=nullptr;
  unsigned char* pixels; int w,h;
  io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
  refare::OptionsInput input{};
  {
    refare::OptionsMenu menu(directory,[&]{return input;});
    const auto frame=[&] {
      ImGui::NewFrame(); GetVideoEvents().imgui_draw_event.Trigger(); ImGui::Render();
    };
    const auto tap=[&](refare::OptionsButton button) {
      input[button]=true; frame(); input[button]=false; frame();
    };
    frame(); Check(!refare::OptionsMenuBlocksInput(),"Closed menu blocks input");
    tap(refare::Toggle);
    Check(refare::OptionsMenuBlocksInput(),"L1/F1 did not open menu");
    Check(ImGui::GetDrawData()->TotalVtxCount>0,"Options overlay did not render");
    input[refare::Right]=true; frame(); frame(); frame();
    Check(Config::Get(Config::GFX_EFB_SCALE)==4,"Held button repeats unexpectedly or scale did not change");
    input={}; frame();
    tap(refare::Down); tap(refare::Right);
    Check(Config::Get(Config::GFX_ASPECT_RATIO)==AspectMode::ForceStandard,"4:3 was not applied");
    Check(!Config::Get(Config::GFX_WIDESCREEN_HACK),"Original aspect still changes projection");
    tap(refare::Right);
    Check(Config::Get(Config::GFX_ASPECT_RATIO)==AspectMode::CustomStretch,"16:9 was not applied");
    Check(Config::Get(Config::GFX_CUSTOM_ASPECT_RATIO_WIDTH)==16 && Config::Get(Config::GFX_CUSTOM_ASPECT_RATIO_HEIGHT)==9,"Custom aspect values incorrect");
    tap(refare::Left); tap(refare::Left);
    Check(Config::Get(Config::GFX_ASPECT_RATIO)==AspectMode::Stretch && Config::Get(Config::GFX_WIDESCREEN_HACK),"Window-following projection was not restored");
#ifdef __APPLE__
    tap(refare::Down); tap(refare::Right);
    Check(!refare::LoadMetalHUDPreference(directory),"Metal HUD Off was not saved");
    tap(refare::Back); tap(refare::Toggle);
    tap(refare::Up); // Wrap from scale to Metal HUD.
    tap(refare::Left);
    Check(refare::LoadMetalHUDPreference(directory),"Metal HUD On was not saved after reopening");
    tap(refare::Right);
    Check(!refare::LoadMetalHUDPreference(directory),"Metal HUD failed to toggle back Off");
#endif
    input[refare::Back]=true; frame();
    Check(refare::OptionsMenuBlocksInput(),"Closing button leaks to game before release");
    input={}; frame(); Check(!refare::OptionsMenuBlocksInput(),"Input remains blocked after closing");
    tap(refare::Toggle); tap(refare::Toggle);
    Check(!refare::OptionsMenuBlocksInput(),"L1/F1 close failed");
    Common::IniFile frontend,graphics;
    frontend.Load((directory/"config.ini").string());
    graphics.Load((directory/"Config/GFX.ini").string());
    std::string resolution,controller; int aspect=0,scale=0; bool vsync=false;
    frontend.GetOrCreateSection("Video")->Get("resolution",&resolution);
    frontend.GetOrCreateSection("Input")->Get("controller1",&controller);
    graphics.GetOrCreateSection("Settings")->Get("AspectRatio",&aspect);
    graphics.GetOrCreateSection("Settings")->Get("InternalResolution",&scale);
    graphics.GetOrCreateSection("Settings")->Get("VSync",&vsync);
    Check(resolution=="2560x1440" && scale==4,"Scale was not saved for next launch");
    Check(aspect==3,"Aspect was not saved for next launch");
    Check(vsync && controller=="Preserved","Unrelated settings were overwritten");
    // The regular frontend writer must also preserve the saved HUD preference.
    auto config = moderngekko::frontend::LoadConfig(directory, false);
    Check(static_cast<bool>(config), "Frontend could not reload options");
#ifdef __APPLE__
    Check(!config.metal_hud,"Frontend lost saved Metal HUD Off");
#endif
    std::string error;
    Check(moderngekko::frontend::SaveConfig(directory,config,&error),"Frontend save failed");
#ifdef __APPLE__
    Check(!refare::LoadMetalHUDPreference(directory),"Frontend save reset Metal HUD Off");
#endif
  }
  Check(!refare::OptionsMenuBlocksInput(),"Destroyed menu blocks input");
  ImGui::DestroyContext(); Config::Shutdown();
  std::cout << "PASS: overlay rendering, controls, live settings, HUD default/toggle/persistence, and input release\n";
}
