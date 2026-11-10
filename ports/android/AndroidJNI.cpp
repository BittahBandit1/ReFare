#include <jni.h>
#include "jni/AndroidCommon/IDCache.h"
namespace {
JavaVM* vm=nullptr;
jclass bridge=nullptr;
jclass strings=nullptr;
}
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* java_vm,void*) {
 vm=java_vm; JNIEnv* env=nullptr;
 if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK)return JNI_ERR;
 auto local=env->FindClass("io/github/bittahbandit1/refare/MainActivity");
 if(!local)return JNI_ERR;
 bridge=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);
 local=env->FindClass("java/lang/String");strings=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);
 return JNI_VERSION_1_6;
}
namespace IDCache {
JNIEnv* GetEnvForThread() {
 struct Attached {
  JNIEnv* env=nullptr; bool owned=false;
  Attached(){if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK){owned=vm->AttachCurrentThread(&env,nullptr)==JNI_OK;}}
  ~Attached(){if(owned)vm->DetachCurrentThread();}
 };
 thread_local Attached attachment;
 return attachment.env;
}
jclass GetStringClass(){return strings;}
jclass GetContentHandlerClass(){return bridge;}
jclass GetNetworkHelperClass(){return bridge;}
#define METHOD(function,name,signature) jmethodID function(){static auto value=GetEnvForThread()->GetStaticMethodID(bridge,name,signature);return value;}
METHOD(GetContentHandlerOpenFd,"openContent","(Ljava/lang/String;Ljava/lang/String;)I")
METHOD(GetContentHandlerDelete,"deleteContent","(Ljava/lang/String;)Z")
METHOD(GetContentHandlerGetSizeAndIsDirectory,"contentSize","(Ljava/lang/String;)J")
METHOD(GetContentHandlerGetDisplayName,"contentName","(Ljava/lang/String;)Ljava/lang/String;")
METHOD(GetContentHandlerGetChildNames,"childNames","(Ljava/lang/String;Z)[Ljava/lang/String;")
METHOD(GetContentHandlerDoFileSearch,"searchContent","(Ljava/lang/String;[Ljava/lang/String;Z)[Ljava/lang/String;")
METHOD(GetNetworkHelperGetNetworkIpAddress,"networkAddress","()I")
METHOD(GetNetworkHelperGetNetworkPrefixLength,"networkPrefix","()I")
METHOD(GetNetworkHelperGetNetworkGateway,"networkGateway","()I")
}
