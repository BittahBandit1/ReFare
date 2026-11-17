#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Called serially on a worker queue. Outputs are files, never loaded or executed.
int ReFareGenerate(const char* dol, const char* directory);
int ReFareCompile(int argc, const char* const* argv);
int ReFareLink(int argc, const char* const* argv);
int ReFarePrepareDOL(const char* dol, const char* manifest);
void ReFareBuilderLog(const char* path);
#ifdef __cplusplus
}
#endif
