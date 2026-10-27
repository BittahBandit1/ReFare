#include "refare_metal_hud.hpp"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <dispatch/dispatch.h>
#import <Foundation/Foundation.h>
#include <pthread/qos.h>

namespace refare {
void SetMetalHUDVisible(void* render_view, bool enabled) {
  if (!render_view) return;
  // The runtime's macOS render window is an NSView. Access its layer on the
  // main thread; the options overlay itself runs on the rendering thread.
  NSView* view = (__bridge NSView*)render_view;
  const auto update = ^{
    if (@available(macOS 13.0, *)) {
      CALayer* layer = view.layer;
      if ([layer isKindOfClass:[CAMetalLayer class]]) {
        CAMetalLayer* metal_layer = (CAMetalLayer*)layer;
        NSMutableDictionary* properties = [metal_layer.developerHUDProperties mutableCopy];
        if (!properties) properties = [NSMutableDictionary dictionary];
        properties[@"mode"] = enabled ? @"default" : @"disabled";
        metal_layer.developerHUDProperties = properties;
      }
    }
  };
  if ([NSThread isMainThread]) update();
  else dispatch_async(dispatch_get_main_queue(), update);
}

void KeepGameResponsive() {
  static id<NSObject> activity = nil;
  if (activity) return;
  // Latency-critical: macOS must not nap or coalesce timers for the emulator
  // when the window is in the background or covered by Spotlight.
  activity = [[NSProcessInfo processInfo]
      beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep |
                                NSActivityLatencyCritical)
                        reason:@"ReFare game emulation and frame presentation"];
}

void PrioritizeCurrentThread() {
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}
}
