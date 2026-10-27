#pragma once
namespace refare {
#ifdef __APPLE__
void SetMetalHUDVisible(void* render_view, bool enabled);
#else
inline void SetMetalHUDVisible(void*, bool) {}
#endif
}
