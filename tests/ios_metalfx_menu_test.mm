#include "refare_options.hpp"
#include "Common/Config/Config.h"
#include "Common/IniFile.h"
#include "Core/Config/GraphicsSettings.h"
#include "VideoCommon/VideoEvents.h"
#include "VideoCommon/RefareMetalFX.h"
#include "imgui.h"
#include "VideoBackends/Metal/MTLObjectCache.h"
#import <Metal/Metal.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
struct Base : Config::ConfigLayerLoader {
 Base() : ConfigLayerLoader(Config::LayerType::Base) {}
 void Load(Config::Layer*) override {} void Save(Config::Layer*) override {}
};
static void Check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
int main(int argc,char** argv) { try {
 if(argc!=2)return 2;
 const std::filesystem::path directory=argv[1];
 std::filesystem::create_directories(directory/"Config");
 Metal::g_device = MRCTransfer(MTLCreateSystemDefaultDevice());
 RefareMetalFX::Initialize();
 Check(RefareMetalFX::GetSupport()==RefareMetalFX::Support::Available,"Test GPU lacks MetalFX");
 Config::Init(); Config::AddLayer(std::make_unique<Base>());
 Check(!Config::Get(Config::GFX_METALFX_SPATIAL),"MetalFX should default off");
 ImGui::CreateContext(); auto& io=ImGui::GetIO();
 io.DisplaySize={2340,1080};io.DisplayFramebufferScale={3,3};io.DeltaTime=1.f/60;io.IniFilename=nullptr;
 unsigned char* data;int width,height;io.Fonts->GetTexDataAsRGBA32(&data,&width,&height);
 refare::OptionsInput input{};
 {
  refare::OptionsMenu menu(directory,[&]{return input;});
  auto frame=[&]{ImGui::NewFrame();GetVideoEvents().imgui_draw_event.Trigger();ImGui::Render();};
  auto tap=[&](refare::OptionsButton button){input[button]=true;frame();input[button]=false;frame();};
  frame();tap(refare::Toggle);Check(refare::OptionsMenuBlocksInput(),"Options did not open");
  for(int i=0;i<4;++i)tap(refare::Down);
  tap(refare::Right);Check(Config::Get(Config::GFX_METALFX_SPATIAL),"Menu did not enable MetalFX");
  Common::IniFile settings;settings.Load((directory/"Config/GFX.ini").string());bool saved=false;
  settings.GetOrCreateSection("Enhancements")->Get("MetalFXSpatial",&saved,false);
  Check(saved,"MetalFX preference not saved");
  tap(refare::Right);Check(!Config::Get(Config::GFX_METALFX_SPATIAL),"Menu did not disable MetalFX");
  settings.Load((directory/"Config/GFX.ini").string());
  settings.GetOrCreateSection("Enhancements")->Get("MetalFXSpatial",&saved,true);
  Check(!saved,"Off preference not saved");
  RefareMetalFX::Shutdown(); Metal::g_device=nullptr; RefareMetalFX::Initialize();
  tap(refare::Right);Check(!Config::Get(Config::GFX_METALFX_SPATIAL),"Unsupported GPU enabled MetalFX");
  tap(refare::Down);tap(refare::Down); // Interpolation, then iOS Metal HUD.
  Check(refare::LoadMetalHUDPreference(directory),"HUD should default on");
  tap(refare::Right);Check(!refare::LoadMetalHUDPreference(directory),"iOS HUD Off did not save without a frontend config");
  tap(refare::Left);Check(refare::LoadMetalHUDPreference(directory),"iOS HUD On did not save");
  tap(refare::Toggle);frame();Check(!refare::OptionsMenuBlocksInput(),"Closing menu retained input block");
 }
 ImGui::DestroyContext();Config::Shutdown();
 std::cout<<"PASS: iOS menu opens, toggles MetalFX live, saves On/Off, rejects unsupported hardware and releases input\n";
 return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
