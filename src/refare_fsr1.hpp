#pragma once
// AMD FidelityFX Super Resolution 1.0 (EASU) as a Dolphin post-processing
// shader, used on platforms without MetalFX. ReFare writes it to
// <user>/Shaders/FSR1.glsl; selecting post-processing shader "FSR1" upscales
// the game image from its render resolution to the window with EASU.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace refare {
inline constexpr const char* kFSR1Shader = "FSR1";

inline constexpr std::string_view kFSR1Source = R"GLSL(// FSR 1.0 EASU (Edge Adaptive Spatial Upsampling) for ReFare / Dolphin.
// Ported from AMD FidelityFX Super Resolution 1.0 (ffx_fsr1.h, FsrEasuF).
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

/*
[configuration]
[/configuration]
*/

float3 FsrTexel(float2 p)
{
  return SampleLocation((p + 0.5) * GetInvResolution()).rgb;
}

// FSR's cheap luma (B * 0.5 + R * 0.5 + G).
float FsrLuma(float3 c)
{
  return c.b * 0.5 + (c.r * 0.5 + c.g);
}

// Accumulates direction and edge length for one bilinear quadrant.
void FsrEasuSet(inout float2 dir, inout float len, float w,
                float lA, float lB, float lC, float lD, float lE)
{
  float dc = lD - lC;
  float cb = lC - lB;
  float lenX = max(abs(dc), abs(cb));
  lenX = lenX > 0.0 ? 1.0 / lenX : 0.0;
  float dirX = lD - lB;
  dir.x += dirX * w;
  lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
  lenX *= lenX;
  len += lenX * w;
  float ec = lE - lC;
  float ca = lC - lA;
  float lenY = max(abs(ec), abs(ca));
  lenY = lenY > 0.0 ? 1.0 / lenY : 0.0;
  float dirY = lE - lA;
  dir.y += dirY * w;
  lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
  lenY *= lenY;
  len += lenY * w;
}

// One tap of the approximate, direction-stretched Lanczos2 kernel.
void FsrEasuTap(inout float3 aC, inout float aW, float2 off, float2 dir, float2 len2,
                float lob, float clp, float3 c)
{
  float2 v;
  v.x = (off.x * dir.x) + (off.y * dir.y);
  v.y = (off.x * (-dir.y)) + (off.y * dir.x);
  v *= len2;
  float d2 = min(v.x * v.x + v.y * v.y, clp);
  float wB = (2.0 / 5.0) * d2 - 1.0;
  float wA = lob * d2 - 1.0;
  wB *= wB;
  wA *= wA;
  wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
  float w = wB * wA;
  aC += c * w;
  aW += w;
}

void main()
{
  // Position in source pixels, relative to the top-left of the 2x2 quad.
  float2 pp = GetCoordinates() * GetResolution() - 0.5;
  float2 fp = floor(pp);
  pp -= fp;

  //    b c
  //  e f g h
  //  i j k l
  //    n o
  float3 b = FsrTexel(fp + float2(0.0, -1.0));
  float3 c = FsrTexel(fp + float2(1.0, -1.0));
  float3 e = FsrTexel(fp + float2(-1.0, 0.0));
  float3 f = FsrTexel(fp);
  float3 g = FsrTexel(fp + float2(1.0, 0.0));
  float3 h = FsrTexel(fp + float2(2.0, 0.0));
  float3 i = FsrTexel(fp + float2(-1.0, 1.0));
  float3 j = FsrTexel(fp + float2(0.0, 1.0));
  float3 k = FsrTexel(fp + float2(1.0, 1.0));
  float3 l = FsrTexel(fp + float2(2.0, 1.0));
  float3 n = FsrTexel(fp + float2(0.0, 2.0));
  float3 o = FsrTexel(fp + float2(1.0, 2.0));

  float bL = FsrLuma(b), cL = FsrLuma(c), eL = FsrLuma(e), fL = FsrLuma(f);
  float gL = FsrLuma(g), hL = FsrLuma(h), iL = FsrLuma(i), jL = FsrLuma(j);
  float kL = FsrLuma(k), lL = FsrLuma(l), nL = FsrLuma(n), oL = FsrLuma(o);

  float2 dir = float2(0.0, 0.0);
  float len = 0.0;
  FsrEasuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
  FsrEasuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
  FsrEasuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
  FsrEasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);

  // Normalize the direction; flat areas fall back to (1, 0).
  float2 dir2 = dir * dir;
  float dirR = dir2.x + dir2.y;
  bool zro = dirR < (1.0 / 32768.0);
  dirR = zro ? 1.0 : inversesqrt(dirR);
  dir.x = zro ? 1.0 : dir.x;
  dir *= dirR;

  // Edge length -> kernel stretch and negative-lobe strength.
  len = len * 0.5;
  len *= len;
  float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
  float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
  float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
  float clp = 1.0 / lob;

  float3 aC = float3(0.0, 0.0, 0.0);
  float aW = 0.0;
  FsrEasuTap(aC, aW, float2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
  FsrEasuTap(aC, aW, float2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
  FsrEasuTap(aC, aW, float2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
  FsrEasuTap(aC, aW, float2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
  FsrEasuTap(aC, aW, float2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
  FsrEasuTap(aC, aW, float2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
  FsrEasuTap(aC, aW, float2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
  FsrEasuTap(aC, aW, float2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
  FsrEasuTap(aC, aW, float2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
  FsrEasuTap(aC, aW, float2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
  FsrEasuTap(aC, aW, float2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
  FsrEasuTap(aC, aW, float2(0.0, 2.0) - pp, dir, len2, lob, clp, n);

  // Deringing: clamp to the 2x2 neighbourhood.
  float3 min4 = min(min(f, g), min(j, k));
  float3 max4 = max(max(f, g), max(j, k));
  SetOutput(float4(min(max4, max(min4, aC / aW)), 1.0));
}
)GLSL";

// Writes <user_dir>/Shaders/FSR1.glsl when it is missing or out of date.
inline bool InstallFSR1Shader(const std::filesystem::path& user_dir) {
  const auto path = user_dir / "Shaders" / "FSR1.glsl";
  {
    std::ifstream in(path, std::ios::binary);
    const std::string current{std::istreambuf_iterator<char>(in), {}};
    if (current == kFSR1Source) return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(kFSR1Source.data(), static_cast<std::streamsize>(kFSR1Source.size()));
  return static_cast<bool>(out);
}
}  // namespace refare
