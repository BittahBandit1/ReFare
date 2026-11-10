#include "moderngekko/runtime.hpp"
#include "moderngekko/module.h"
#include "dol_patch.hpp"
#include "refare_options.hpp"
#include "refare_runtime_services.hpp"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Core/Core.h"
#include "Core/DolphinAnalytics.h"
#include "Core/System.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoEvents.h"
#include "imgui.h"
#include <android/native_window_jni.h>
#include <android/log.h>
#include <jni.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>
extern "C" const ModernGekkoModuleDesc* staticrecomp_get_module();
namespace {
std::mutex lock;
std::jthread worker;
moderngekko::Runtime* active=nullptr;
int state=0;
bool stopping=false;
std::string message;
std::atomic<long long> frames{0};
std::atomic<bool> toggle{false};
std::atomic<unsigned> buttons{0};
struct Pointer{float x,y;bool down;};
std::mutex pointer_lock;
std::vector<Pointer> pointers;
void Log(const char* text){__android_log_print(ANDROID_LOG_INFO,"ReFare","%s",text);}
void Stop(){std::lock_guard guard(lock);stopping=true;if(active)active->RequestStop();}
refare::OptionsInput MenuInput(){
 std::vector<Pointer> events;
 {std::lock_guard guard(pointer_lock);events.swap(pointers);}
 auto& io=ImGui::GetIO();
 for(auto p:events){io.AddMousePosEvent(p.x*io.DisplaySize.x,p.y*io.DisplaySize.y);io.AddMouseButtonEvent(0,p.down);}
 refare::OptionsInput input{};auto mask=buttons.load();
 for(int i=0;i<refare::Count;++i)input[i]=(mask&(1u<<i))!=0;
 input[refare::Toggle]=input[refare::Toggle]||toggle.exchange(false);
 return input;
}
std::string Text(JNIEnv* env,jstring s){const char* p=env->GetStringUTFChars(s,nullptr);std::string text(p);env->ReleaseStringUTFChars(s,p);return text;}
}
#define JNI_METHOD(name) Java_io_github_bittahbandit1_refare_MainActivity_##name
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nativeStart)(JNIEnv* env,jclass,jobject surface,jstring game,jstring user,jstring sys,jfloat scale){
 {std::lock_guard guard(lock);if(state==1)return;}
 if(worker.joinable())worker.join();
 auto* window=ANativeWindow_fromSurface(env,surface);
 if(!window)return;
 std::filesystem::path root=Text(env,game),profile=Text(env,user),system=Text(env,sys);
 {std::lock_guard guard(lock);state=1;stopping=false;message="Starting game";}
 frames=0;toggle=false;buttons=0;
 worker=std::jthread([=]{
  try{
   std::filesystem::create_directories(profile/"Config");
   std::freopen((profile/"gameplay.log").c_str(),"a",stderr);
   std::setvbuf(stderr,nullptr,_IONBF,0);
   Log("Starting native ARM64 Crazy Taxi runtime");
   DolphinAnalytics::AndroidSetGetValFunc([](std::string){return std::string("ReFare Android");});
   static bool sys_set=false;if(!sys_set){File::SetSysDirectory(system.string());sys_set=true;}
   if(!std::filesystem::exists(profile/"Config/GFX.ini"))std::filesystem::copy_file(system.parent_path()/"GFX.ini",profile/"Config/GFX.ini");
   bool changed=false;std::string error;
   if(!moderngekko::frontend::ApplyDolPatchManifest(root/"sys/main.dol",system.parent_path()/"startup-patches.csv",&changed,&error))throw std::runtime_error(error);
   moderngekko::RuntimeConfig config;
   config.game_root=root;config.user_directory=profile;
   config.module=moderngekko::ModuleSource::AttachedDescriptor(staticrecomp_get_module());
   config.native_window=window;config.native_metal_layer=window;config.native_surface_scale=scale;
   config.graphics.backend="Vulkan";config.audio.backend="AAudio";config.show_fps_in_title=false;
   config.input.blocks_input=[] {return refare::OptionsMenuBlocksInput();};
   auto created=moderngekko::Runtime::Create(std::move(config));
   if(!created)throw std::runtime_error(created.error->message);
   Log("Runtime created; launching compiled game");
   {auto guard=ControllerEmu::EmulatedController::GetStateLock();ciface::Touch::RegisterGameCubeInputOverrider(0);}
   {std::lock_guard guard(lock);active=created.runtime.get();if(stopping)active->RequestStop();message="Running";}
   struct Clear{~Clear(){std::lock_guard guard(lock);active=nullptr;}}clear;
   auto present=GetVideoEvents().after_present_event.Register([](const PresentInfo& info){
    frames=info.frame_count;
    if(info.frame_count && info.frame_count%300==0)__android_log_print(ANDROID_LOG_INFO,"ReFare","Presented frame %llu",static_cast<unsigned long long>(info.frame_count));
   });
   refare::ConfigureFramePacing();
   auto menu=std::make_unique<refare::OptionsMenu>(profile,MenuInput,nullptr,Stop,[]{return refare::MenuButtonCapture{};});
   auto result=created.runtime->Run();
   menu.reset();present.reset();
   {std::lock_guard guard(lock);active=nullptr;}
   {auto guard=ControllerEmu::EmulatedController::GetStateLock();ciface::Touch::UnregisterGameCubeInputOverrider(0);}
   created.runtime.reset();
   {std::lock_guard guard(lock);state=result.error?3:2;message=result.error?result.error->message:"Game stopped";Log(message.c_str());}
  }catch(const std::exception& error){
   std::lock_guard guard(lock);active=nullptr;state=3;message=error.what();Log(message.c_str());std::fprintf(stderr,"Android startup error: %s\n",error.what());
  }
  std::fflush(stderr);ANativeWindow_release(window);
 });
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nativeStop)(JNIEnv*,jclass){Stop();}
extern "C" JNIEXPORT jint JNICALL JNI_METHOD(nativeState)(JNIEnv*,jclass){std::lock_guard guard(lock);return state;}
extern "C" JNIEXPORT jstring JNICALL JNI_METHOD(nativeMessage)(JNIEnv* env,jclass){std::lock_guard guard(lock);return env->NewStringUTF(message.c_str());}
extern "C" JNIEXPORT jlong JNICALL JNI_METHOD(nativeFrames)(JNIEnv*,jclass){return frames.load();}
extern "C" JNIEXPORT jboolean JNICALL JNI_METHOD(nativeMenuOpen)(JNIEnv*,jclass){return refare::OptionsMenuBlocksInput();}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nativeToggleMenu)(JNIEnv*,jclass){toggle=true;}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nativeMenuButtons)(JNIEnv*,jclass,jint mask){buttons=mask;}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nativePointer)(JNIEnv*,jclass,jfloat x,jfloat y,jboolean down){std::lock_guard guard(pointer_lock);if(pointers.size()<256)pointers.push_back({x,y,down!=0});}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nativeInput)(JNIEnv*,jclass,jint id,jdouble value){
 if(id<0||id>17||!std::isfinite(value))return;
 auto guard=ControllerEmu::EmulatedController::GetStateLock();
 ciface::Touch::SetControlState(0,static_cast<ciface::Touch::ControlID>(id),refare::OptionsMenuBlocksInput()?0:std::clamp(value,-1.0,1.0));
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nativePause)(JNIEnv*,jclass,jboolean paused){
 std::lock_guard guard(lock);
 if(active&&Core::IsRunning(Core::System::GetInstance())){if(paused)active->Pause();else active->Resume();}
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nativeResize)(JNIEnv*,jclass){Core::QueueHostJob([](Core::System&){if(g_presenter)g_presenter->ResizeSurface();});}
