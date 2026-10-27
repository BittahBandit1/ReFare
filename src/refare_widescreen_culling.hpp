#pragma once
#include "moderngekko/mod_abi.h"
namespace refare {
// Built-in code mod: widens Crazy Taxi's view-frustum culling to match the
// widescreen projection, so geometry at the sides of wide windows is drawn.
const ModernGekkoModDesc* WidescreenCullingMod();
}
