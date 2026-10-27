#pragma once
namespace refare {
#ifdef __APPLE__
void SetMetalHUDVisible(void* render_view, bool enabled);
// Keep App Nap and timer throttling off while the game runs, including when
// another app (Spotlight, a notification) takes focus.
void KeepGameResponsive();
// Run the calling thread at user-interactive QoS (CPU and video threads).
void PrioritizeCurrentThread();
#else
inline void SetMetalHUDVisible(void*, bool) {}
inline void KeepGameResponsive() {}
inline void PrioritizeCurrentThread() {}
#endif
}
