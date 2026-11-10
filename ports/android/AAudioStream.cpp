// SPDX-License-Identifier: GPL-2.0-or-later
#include "AudioCommon/SoundStream.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
namespace AudioCommon {
namespace {
class AndroidSound final : public SoundStream {
  AAudioStream* stream = nullptr;
  std::atomic<float> volume{1};
  bool sound_reported=false;
  static aaudio_data_callback_result_t Render(AAudioStream*,void* user,void* output,int32_t count) {
    auto& self=*static_cast<AndroidSound*>(user);
    auto* samples=static_cast<short*>(output);
    self.m_mixer->Mix(samples,count);
    const float gain=self.volume.load(std::memory_order_relaxed);
    int peak=0;
    for(int i=0;i<count*2;++i){samples[i]=static_cast<short>(samples[i]*gain);peak=std::max(peak,std::abs(int(samples[i])));}
    if(peak&&!self.sound_reported){self.sound_reported=true;__android_log_print(ANDROID_LOG_INFO,"ReFare","AAudio is rendering non-silent game audio");}
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }
public:
  ~AndroidSound() override { if(stream){AAudioStream_requestStop(stream);AAudioStream_close(stream);} }
  bool Init() override {
    AAudioStreamBuilder* builder=nullptr;
    if(AAudio_createStreamBuilder(&builder)!=AAUDIO_OK)return false;
    AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder,AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder,48000);
    AAudioStreamBuilder_setChannelCount(builder,2);
    AAudioStreamBuilder_setDataCallback(builder,Render,this);
    auto result=AAudioStreamBuilder_openStream(builder,&stream);
    AAudioStreamBuilder_delete(builder);
    if(result!=AAUDIO_OK)return false;
    m_mixer->SetSampleRate(AAudioStream_getSampleRate(stream));
    __android_log_print(ANDROID_LOG_INFO,"ReFare","AAudio opened: %d Hz, %d channels",AAudioStream_getSampleRate(stream),AAudioStream_getChannelCount(stream));
    return AAudioStream_getFormat(stream)==AAUDIO_FORMAT_PCM_I16 && AAudioStream_getChannelCount(stream)==2;
  }
  bool SetRunning(bool running) override {
    return stream && (running?AAudioStream_requestStart(stream):AAudioStream_requestStop(stream))==AAUDIO_OK;
  }
  void SetVolume(int value) override { volume=std::clamp(value,0,100)/100.f; }
};
}
std::unique_ptr<SoundStream> CreateAAudioSoundStream(){return std::make_unique<AndroidSound>();}
}
