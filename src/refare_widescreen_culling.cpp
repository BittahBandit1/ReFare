#include "refare_widescreen_culling.hpp"
#include "VideoCommon/VideoConfig.h"
#include "refare_metal_hud.hpp"
#include <bit>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstdio>

// Crazy Taxi (GCTE51) camera notes, from the recompiled DOL:
// - 0x8000E704 builds the projection from a vertical FOV and aspect, and stores
//   culling data in the camera block (pointer at r13-26280):
//     +16 fov*aspect, +20 fov, +24/+32 sin/cos of the horizontal half-angle,
//     +28/+36 vertical, +40/+44 full angles, +48/+56 sin/cos(+40*k),
//     +52/+60 sin/cos(+44*k), where k = [0x800DCD50]*[0x800DCD54].
// - 0x8000D9BC rotates the side planes into world space every frame: planes
//   at +436/+448/+460/+472 from +24..+36, and +508..+544 from +48..+60.
//   0x80008278 and friends cull objects against them.
// Dolphin's widescreen hack widens only the GX projection, so objects outside
// the original 4:3 cone were culled. Before each plane build, rewrite the
// horizontal (and, for tall windows, vertical) values for the widened view.
// Values are derived from +16/+20 each time, so repeated calls never compound.
namespace refare {
namespace {
constexpr uint32_t kPlaneBuilder = 0x8000D9BC;
constexpr int32_t kCameraPointer = -26280;  // r13-relative
constexpr uint32_t kAngleScaleA = 0x800DCD50;
constexpr uint32_t kAngleScaleB = 0x800DCD54;
constexpr uint32_t kFullAngleScale = 0x800DCD58;  // double

float ReadF32(CPUState* state, uint32_t address) {
  return std::bit_cast<float>(static_cast<uint32_t>(moderngekko_mod_read(state, address, 4)));
}
double ReadF64(CPUState* state, uint32_t address) {
  return std::bit_cast<double>(moderngekko_mod_read(state, address, 8));
}
void WriteF32(CPUState* state, uint32_t address, double value) {
  moderngekko_mod_write(state, address, std::bit_cast<uint32_t>(static_cast<float>(value)), 4);
}

double HorizontalScale() {
  if (!g_ActiveConfig.bWidescreenHack) return 1.0;
  const float w = g_ActiveConfig.fAspectRatioHackW;
  return (w > 0.0f && w < 1.0f) ? 1.0 / w : 1.0;
}

struct Axis { uint32_t angle, sin_half, cos_half, sin_full, cos_full; };
constexpr Axis kHorizontal{16, 24, 32, 48, 56};
constexpr Axis kVertical{20, 28, 36, 52, 60};

// The game's own formulas, with the half-angle tangent scaled by `scale`.
void Write(CPUState* state, uint32_t camera, const Axis& axis, double k, double full_scale,
           double scale) {
  const double tangent = std::tan(ReadF32(state, camera + axis.angle) * k) * scale;
  const double half = std::atan(tangent);
  WriteF32(state, camera + axis.sin_half, std::sin(half));
  WriteF32(state, camera + axis.cos_half, std::cos(half));
  // Original: 2 * c * atan2(2 * near * tan(half), near), then sin/cos(.. * k).
  const double full = 2.0 * full_scale * std::atan(2.0 * tangent) * k;
  WriteF32(state, camera + axis.sin_full, std::sin(full));
  WriteF32(state, camera + axis.cos_full, std::cos(full));
}

void WidenCulling(CPUState* state) {
  static const bool prioritized = [] { PrioritizeCurrentThread(); return true; }();  // CPU thread
  (void)prioritized;
  // Runs on the CPU thread; these floats are refreshed by the video thread.
  if (!g_ActiveConfig.bWidescreenHack) return;
  const float h = g_ActiveConfig.fAspectRatioHackH;
  const double x_scale = HorizontalScale();
  const double y_scale = (h > 0.0f && h < 1.0f) ? 1.0 / h : 1.0;
  const uint32_t camera = static_cast<uint32_t>(
      moderngekko_mod_read(state, state->gpr[13] + static_cast<uint32_t>(kCameraPointer), 4));
  if (camera < 0x80000000u || camera >= 0x81800000u) return;
  const double k = static_cast<double>(ReadF32(state, kAngleScaleA)) * ReadF32(state, kAngleScaleB);
  const double full_scale = ReadF64(state, kFullAngleScale);
  static bool checked = false;
  if (!checked) {
    // One-time self-check: at scale 1 the formulas must reproduce the game's.
    checked = true;
    const double half = std::atan(std::tan(ReadF32(state, camera + 16) * k));
    std::fprintf(stderr,
                 "[refare] culling check: game sin/cos %.6f/%.6f, recomputed %.6f/%.6f; "
                 "game full sin %.6f, recomputed %.6f\n",
                 ReadF32(state, camera + 24), ReadF32(state, camera + 32), std::sin(half),
                 std::cos(half), ReadF32(state, camera + 48),
                 std::sin(2.0 * full_scale * std::atan(2.0 * std::tan(half)) * k));
  }
  if (x_scale > 1.0) Write(state, camera, kHorizontal, k, full_scale, x_scale);
  if (y_scale > 1.0) Write(state, camera, kVertical, k, full_scale, y_scale);
}

// A second, cheaper visibility test (0x80065ECC / 0x80065F28, used by the
// pedestrian, traffic and prop systems) checks |x| against depth with a fixed
// horizontal half-angle h = fov/2, independent of aspect. 0x80065E40 runs once
// per frame with the speed-dependent fov and stores 1/sin(h) at r13-31984 and
// 1/tan(h) at r13-31980 (fov * 0.5 * 65536/360 in 16-bit angle units, minus 1;
// 0x8008C35C is the sine table, 0x8008C2D4 is sin/cos). The test is therefore
// visible iff r/sin(h) + depth > |x|/tan(h), i.e. |x| < depth*tan(h) + r/cos(h):
// an exact sphere-vs-cone check. Replace the setter so h covers the wider view.
constexpr uint32_t kSetViewCone = 0x80065E40;
constexpr int32_t kConeInvSin = -31984;  // r13-relative
constexpr int32_t kConeInvTan = -31980;
constexpr double kPi = 3.14159265358979323846;

void SetViewCone(CPUState* state) {
  const double fov = state->fpr[1];
  const int32_t whole = static_cast<int32_t>(fov * 0.5 * 65536.0 / 360.0);  // fctiwz
  const uint32_t units = (static_cast<uint32_t>(whole) & 0xFFFFu) - 1u;
  double half = static_cast<double>(units & 0xFFFFu) * (2.0 * kPi / 65536.0);
  const double scale = HorizontalScale();
  if (scale > 1.0 && half > 0.0 && half < kPi / 2.0)
    half = std::atan(std::tan(half) * scale);
  WriteF32(state, state->gpr[13] + static_cast<uint32_t>(kConeInvSin), 1.0 / std::sin(half));
  WriteF32(state, state->gpr[13] + static_cast<uint32_t>(kConeInvTan), 1.0 / std::tan(half));
  state->pc = state->lr;  // void function: return to caller
}

// Diagnostic (REFARE_DL_TRACE=1 with REFARE_GFX_TRACE_DIR set): creating
// <dir>/dl-trace-request records every GXCallDisplayList for ~4 frames with the
// guest call stack, to locate which game systems submit which geometry.
constexpr uint32_t kGXCallDisplayList = 0x800C3858;
struct DisplayListTrace {
  std::string dir;
  std::FILE* file = nullptr;
  int remaining = 0;
  unsigned poll = 0;
  std::chrono::steady_clock::time_point end;
};
DisplayListTrace& Trace() {
  static DisplayListTrace trace = [] {
    DisplayListTrace t;
    if (const char* dir = std::getenv("REFARE_GFX_TRACE_DIR")) t.dir = dir;
    return t;
  }();
  return trace;
}
uint32_t ReadU32(CPUState* state, uint32_t address) {
  return static_cast<uint32_t>(moderngekko_mod_read(state, address, 4));
}
bool IsRam(uint32_t address) { return address >= 0x80000000u && address < 0x81800000u; }

void TraceDisplayList(CPUState* state) {
  DisplayListTrace& t = Trace();
  if (t.dir.empty()) return;
  namespace fs = std::filesystem;
  if (!t.file) {
    if ((t.poll++ & 255u) != 0u) return;
    std::error_code ec;
    const fs::path request = fs::path(t.dir) / "dl-trace-request";
    if (!fs::exists(request, ec)) return;
    fs::remove(request, ec);
    int index = 0;
    while (fs::exists(fs::path(t.dir) / ("dl-trace-" + std::to_string(index) + ".txt"), ec)) ++index;
    t.file = std::fopen((fs::path(t.dir) / ("dl-trace-" + std::to_string(index) + ".txt")).string().c_str(), "w");
    if (!t.file) return;
    t.remaining = 40000;
    t.end = std::chrono::steady_clock::now() + std::chrono::milliseconds(70);
    const uint32_t camera = ReadU32(state, state->gpr[13] + static_cast<uint32_t>(kCameraPointer));
    if (IsRam(camera)) {
      std::fprintf(t.file, "camera pos %g %g %g fwd %g %g %g aspectW %g\n",
                   ReadF32(state, camera + 88), ReadF32(state, camera + 92),
                   ReadF32(state, camera + 96), ReadF32(state, camera + 412),
                   ReadF32(state, camera + 416), ReadF32(state, camera + 420),
                   g_ActiveConfig.fAspectRatioHackW);
    }
  }
  std::fprintf(t.file, "dl %08x size %u lr %08x stack", state->gpr[3], state->gpr[4], state->lr);
  uint32_t frame = state->gpr[1];
  for (int depth = 0; depth < 14; ++depth) {
    const uint32_t next = ReadU32(state, frame);
    if (!IsRam(next) || next <= frame) break;
    const uint32_t ret = ReadU32(state, next + 4);
    if (!IsRam(ret)) break;
    std::fprintf(t.file, " %08x", ret);
    frame = next;
  }
  std::fputc('\n', t.file);
  if (--t.remaining <= 0 || std::chrono::steady_clock::now() > t.end) {
    std::fclose(t.file);
    t.file = nullptr;
  }
}

const ModernGekkoModPatch kPatches[] = {RECOMP_PATCH(kSetViewCone, SetViewCone)};

ModernGekkoModHook kHooks[] = {RECOMP_HOOK(kPlaneBuilder, WidenCulling),
                               RECOMP_HOOK(kGXCallDisplayList, TraceDisplayList)};

ModernGekkoModDesc kDescriptor = {
    MODERNGEKKO_MOD_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    "GCTE51",
    "refare_widescreen_culling",
    "1.0.0",
    "ReFare widescreen culling",
    nullptr, 0u,       // dependencies
    kPatches, 1u,      // patches
    kHooks, 1u,        // hooks
    nullptr, 0u,       // exports
    nullptr, 0u,       // imports
    nullptr, 0u,       // events
    nullptr, 0u,       // callbacks
    nullptr, nullptr,  // on_load, on_unload
};
}  // namespace

const ModernGekkoModDesc* WidescreenCullingMod() {
  // The display-list tracer intercepts every draw call; register it only on request.
  const char* trace = std::getenv("REFARE_DL_TRACE");
  kDescriptor.num_hooks = (trace && trace[0] == '1') ? 2u : 1u;
  return &kDescriptor;
}
}  // namespace refare
