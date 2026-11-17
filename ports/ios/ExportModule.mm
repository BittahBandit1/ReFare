// Generic gameplay template. The personal framework is added by the builder,
// then signed together with the app by the user's installation tool.
#include "moderngekko/module.h"
#include <dlfcn.h>
#include <Foundation/Foundation.h>
#include <stdexcept>
#include <string>

extern "C" const ModernGekkoModuleDesc* staticrecomp_get_module() {
  static void* library = nullptr;
  if (!library) {
    NSString* path = [NSBundle.mainBundle.bundlePath
      stringByAppendingPathComponent:@"Frameworks/ReFareGame.framework/ReFareGame"];
    library = dlopen(path.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    if (!library) throw std::runtime_error(std::string("Could not load signed personal game framework: ") + dlerror());
  }
  using Entry = const ModernGekkoModuleDesc* (*)();
  auto entry = reinterpret_cast<Entry>(dlsym(library, "staticrecomp_get_module"));
  if (!entry) throw std::runtime_error("Personal game framework has no module entry point");
  return entry();
}
