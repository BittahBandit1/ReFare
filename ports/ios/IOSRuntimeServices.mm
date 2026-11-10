#include "refare_metal_hud.hpp"
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <pthread/qos.h>
#include <atomic>
namespace refare {
static std::atomic<double> refresh{60};
bool MetalHUDSupported() { if (@available(iOS 16.0, *)) return true; return false; }
void SetMetalHUDVisible(void* surface, bool enabled) {
  if (!surface) return;
  UIView* view = (__bridge UIView*)surface;
  dispatch_async(dispatch_get_main_queue(), ^{
    if (@available(iOS 16.0, *)) {
      if ([view.layer isKindOfClass:CAMetalLayer.class]) {
        CAMetalLayer* layer = (CAMetalLayer*)view.layer;
        NSMutableDictionary* values = [layer.developerHUDProperties mutableCopy];
        if (!values) values = [NSMutableDictionary dictionary];
        values[@"mode"] = enabled ? @"default" : @"disabled";
        layer.developerHUDProperties = values;
      }
    }
  });
}
void KeepGameResponsive() {}
void PrioritizeCurrentThread() { pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); }
double SecondsSinceKeyDown() { return -1; }
double DisplayRefreshRate(void*) { return refresh.load(); }
}
extern "C" void ReFareDisplayRate(double rate) { refare::refresh.store(rate); }
