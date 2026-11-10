#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// All callers use the UI thread. The render view must outlive the worker.
void ReFareStart(void* view, void* layer, float scale, const char* game, const char* user);
void ReFareStop(void);
void ReFareToggleMenu(void);
int ReFareMenuOpen(void);
void ReFareMenuButtons(uint32_t mask);
void ReFareMenuPointer(double x, double y, int down, double scroll);
void ReFareDisplayRate(double rate);
void ReFareResize(void);
void ReFarePause(int paused);
void ReFareInput(int control, double value);
// 0 idle, 1 starting/running, 2 stopped, 3 failed. Copies the latest message.
int ReFareStatus(char* message, int capacity);
#ifdef __cplusplus
}
#endif
