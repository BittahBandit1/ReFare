#include "dol_patch.hpp"
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  bool changed = false;
  std::string error;
  if (!moderngekko::frontend::ApplyDolPatchManifest(argv[1], argv[2], &changed, &error)) {
    std::cerr << error;
    return 1;
  }
  std::cout << (changed ? "changed" : "unchanged");
}
