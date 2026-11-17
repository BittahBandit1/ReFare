#include "../ports/ios/BuilderBridge.h"
// A cache hit must not invoke any compiler. These return failure so a broken
// integrity check cannot silently regenerate or export the fixture.
int ReFareGenerate(const char* dol, const char* directory) { (void)dol; (void)directory; return 1; }
int ReFareCompile(int argc, const char* const* argv) { (void)argc; (void)argv; return 1; }
int ReFareLink(int argc, const char* const* argv) { (void)argc; (void)argv; return 1; }
int ReFarePrepareDOL(const char* dol, const char* manifest) { (void)dol; (void)manifest; return 0; }
void ReFareBuilderLog(const char* path) { (void)path; }
