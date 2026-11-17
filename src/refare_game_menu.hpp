#pragma once
#include "moderngekko/mod_abi.h"

namespace refare {
// Adds "REFARE SETTINGS" to Crazy Taxi's own OPTIONS screen (GCTE51 rev 0).
// Hook this at the game's per-frame menu engine (kGameMenuEngine).
constexpr unsigned kGameMenuEngine = 0x800232A4u;
void GameMenuTick(CPUState* state);
}  // namespace refare
