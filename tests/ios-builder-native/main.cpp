#include "BuilderBridge.h"
#include <cstring>

int main(int argc, char** argv) {
  if (argc < 2) return 64;
  if (!std::strcmp(argv[1], "generate") && argc == 4) return ReFareGenerate(argv[2], argv[3]);
  if (!std::strcmp(argv[1], "patch") && argc == 4) return ReFarePrepareDOL(argv[2], argv[3]);
  if (!std::strcmp(argv[1], "compile")) return ReFareCompile(argc - 2, argv + 2);
  if (!std::strcmp(argv[1], "link")) return ReFareLink(argc - 2, argv + 2);
  // Exercise linker re-entry in one process, as repeated IPA exports do.
  if (!std::strcmp(argv[1], "link-twice")) {
    int first = ReFareLink(argc - 2, argv + 2);
    return first ? first : ReFareLink(argc - 2, argv + 2);
  }
  return 64;
}
