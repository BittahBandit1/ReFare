#include "refare_button_prompts.hpp"
#include "Common/HookableEvent.h"
#include "Core/Host.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "VideoCommon/RefarePromptTextures.h"
#include "VideoCommon/VideoEvents.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>

namespace refare {
namespace {
// GX texture format id for RGBA8 (TextureFormat::RGBA8).
constexpr unsigned kGXRGBA8 = 6;

unsigned ReadBE32(const unsigned char* p) {
  return (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16) | (unsigned(p[2]) << 8) | p[3];
}
unsigned ReadLE32(const unsigned char* p) {
  return (unsigned(p[3]) << 24) | (unsigned(p[2]) << 16) | (unsigned(p[1]) << 8) | p[0];
}

// Glyphs missing from the game's prompt font, rendered from Roboto Condensed
// Black (SIL Open Font License 1.1), compressed to 64% width and quantized to
// the game's five gray levels. Row 0 is the cap top; '.'=0 '1'=68 '2'=136
// '3'=187 '#'=255.
struct Glyph { int width; std::array<const char*, 20> rows; };
constexpr Glyph kGlyphX{10, {"1##2.3##..", ".##3.##3..", ".3##1##1..", ".1##2##...", "..#####...",
                             "..####2...", "..2###1...", "..1###....", "..2###1...", "..####2...",
                             "..#####...", ".1##2##...", ".3##.##1..", ".##3.##3..", "1##2.3##..",
                             "..........", "..........", "..........", "..........", ".........."}};
constexpr Glyph kGlyphK{9, {"3#3.3##..", "##3.###..", "##21##1..", "##23##...", "##3##2...",
                            "#####1...", "#####....", "#####....", "#####1...", "#####3...",
                            "###3##...", "##21##1..", "##3.##2..", "##3.###..", "3#3.3##..",
                            ".........", ".........", ".........", ".........", "........."}};
constexpr Glyph kGlyphY{9, {".........", ".........", ".........", ".11..11..", "1##12##..",
                            ".##13##..", ".##2##3..", ".3#3##2..", ".2####1..", ".1####...",
                            "..####...", "..###3...", "..###2...", "..3##1...", "..1##....",
                            "..1##....", ".1##3....", ".2##2....", ".2##.....", "..2......"}};

unsigned char Level(char c) {
  switch (c) {
  case '1': return 68;
  case '2': return 136;
  case '3': return 187;
  case '#': return 255;
  default: return 0;
  }
}

// Upright (display-oriented) view over a stored RGBA image; the game draws
// these sprites with flipped V, so stored row 0 is the bottom of the text.
struct Canvas {
  std::vector<unsigned char>* rgba;
  unsigned w, h;
  unsigned char* At(unsigned x, unsigned y) const { return &(*rgba)[((h - 1 - y) * w + x) * 4]; }
};

bool IsText(const unsigned char* p) { return p[3] != 0 && p[0] != 0; }
}  // namespace

std::vector<ArchiveTexture> ReadSpriteArchive(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<unsigned char> d{std::istreambuf_iterator<char>(file), {}};
  std::vector<ArchiveTexture> out;
  if (d.size() < 16) return out;
  // Little-endian entry count, then 0x44-byte entries (name[64], size) from
  // 0x10; texture files follow at the next 32-byte boundary, each with a
  // 0x60-byte big-endian header (width, height, ?, GX format).
  const unsigned count = ReadLE32(d.data());
  size_t table = 0x10, offset = (0x10 + size_t(count) * 0x44 + 31) & ~size_t(31);
  if (count > 4096 || offset > d.size()) return out;
  for (unsigned i = 0; i < count; ++i, table += 0x44) {
    const char* name = reinterpret_cast<const char*>(&d[table]);
    const unsigned size = ReadLE32(&d[table + 0x40]);
    if (size < 0x60 || offset + size > d.size()) break;
    ArchiveTexture t;
    t.name.assign(name, strnlen(name, 0x40));
    t.width = ReadBE32(&d[offset]);
    t.height = ReadBE32(&d[offset + 4]);
    t.format = ReadBE32(&d[offset + 12]);
    t.texels.assign(d.begin() + offset + 0x60, d.begin() + offset + size);
    out.push_back(std::move(t));
    offset += size;
  }
  return out;
}

std::vector<unsigned char> DecodeRGBA8(const std::vector<unsigned char>& texels, unsigned w,
                                       unsigned h) {
  std::vector<unsigned char> rgba(size_t(w) * h * 4);
  // 4x4 tiles of 64 bytes: 16 AR pairs, then 16 GB pairs.
  size_t p = 0;
  for (unsigned by = 0; by < h; by += 4)
    for (unsigned bx = 0; bx < w; bx += 4, p += 64)
      for (unsigned i = 0; i < 16 && p + 64 <= texels.size(); ++i) {
        const unsigned x = bx + i % 4, y = by + i / 4;
        if (x >= w || y >= h) continue;
        unsigned char* o = &rgba[(size_t(y) * w + x) * 4];
        o[0] = texels[p + 2 * i + 1];
        o[1] = texels[p + 32 + 2 * i];
        o[2] = texels[p + 32 + 2 * i + 1];
        o[3] = texels[p + 2 * i];
      }
  return rgba;
}

std::vector<unsigned char> MakeKeyboardDecidePrompt(const std::vector<unsigned char>& rgba,
                                                     unsigned w, unsigned h) {
  // Layout of the original (upright): "Use" 13-36, "the" 40-60, "A" 65-73,
  // "Button" 79-119, then "t" from 121 continuing into the next texture.
  if (w != 128 || h != 32 || rgba.size() != size_t(w) * h * 4) return {};
  std::vector<unsigned char> src = rgba, out(rgba.size());
  const Canvas s{&src, w, h}, o{&out, w, h};
  // Bar background: the two-column dither from the bar's text-free left edge.
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x) std::memcpy(o.At(x, y), s.At(x % 2, y), 4);
  const auto copy_cols = [&](unsigned x0, unsigned x1, unsigned dst) {
    for (unsigned x = x0; x <= x1; ++x)
      for (unsigned y = 0; y < h; ++y)
        if (IsText(s.At(x, y))) std::memcpy(o.At(dst + x - x0, y), s.At(x, y), 4);
  };
  const auto draw = [&](const Glyph& g, unsigned dst) {
    for (unsigned row = 0; row < g.rows.size(); ++row)
      for (int col = 0; col < g.width; ++col) {
        const unsigned char v = Level(g.rows[row][col]);
        const unsigned x = dst + col, y = 6 + row;  // cap top on row 6, like the game
        if (!v || x >= w || y >= h) continue;
        unsigned char* p = o.At(x, y);
        p[0] = p[1] = p[2] = v;
        p[3] = 255;
      }
  };
  // Right to left from the kept "t" so the text still joins "o decide.".
  unsigned x = 124 - 4;                         // word space before "t"
  x -= kGlyphY.width; const unsigned xy = x;
  x -= 7; const unsigned xe = x;                // the game's own "e" (cols 54-60)
  x -= kGlyphK.width; const unsigned xk = x;
  x -= 4;
  x -= kGlyphX.width; const unsigned xx = x;
  x -= 4;
  x -= 60 - 13 + 1; const unsigned xuse = x;    // the game's own "Use the" (cols 13-60)
  copy_cols(13, 60, xuse);
  draw(kGlyphX, xx);
  draw(kGlyphK, xk);
  copy_cols(54, 60, xe);
  draw(kGlyphY, xy);
  copy_cols(121, 127, 121);
  return out;
}

struct ButtonPrompts::Impl {
  Common::EventHook hook;
  int mode = -1;  // -1 unknown, 0 controller, 1 keyboard

  void Poll() {
    if (!g_controller_interface.IsInit()) return;
    bool keyboard = false, controller = false, have_controller = false;
    const bool focused = Host_RendererHasFocus();
    for (const auto& device : g_controller_interface.GetAllDevices()) {
      const bool is_pad = device->GetSource() == "SDL";
      const bool is_keyboard = !is_pad && device->GetName().find("Keyboard") != std::string::npos;
      have_controller |= is_pad;
      if (!is_pad && !(is_keyboard && focused)) continue;
      for (const auto* input : device->Inputs()) {
        if (!input->IsDetectable() || input->GetState() <= 0.5) continue;
        if (is_keyboard) {
          const std::string name = input->GetName();
          // Mouse buttons/axes share the keyboard device; they don't count.
          if (name.starts_with("Click") || name.starts_with("Cursor") ||
              name.starts_with("Axis") || name.starts_with("Scroll"))
            continue;
          keyboard = true;
        } else {
          controller = true;
        }
        break;
      }
    }
    int next = mode;
    if (keyboard) next = 1;
    else if (controller) next = 0;
    else if (mode < 0) next = have_controller ? 0 : 1;
    if (next != mode) {
      mode = next;
      RefarePromptTextures::SetKeyboard(mode == 1);
      std::fprintf(stderr, "[refare] button prompts: %s\n", mode == 1 ? "keyboard" : "controller");
    }
  }
};

ButtonPrompts::ButtonPrompts(const std::filesystem::path& game_root)
    : m_impl(std::make_unique<Impl>()) {
  const auto archive = game_root / "files" / "ct" / "sprCENG.all";
  int registered = 0;
  for (const auto& t : ReadSpriteArchive(archive)) {
    if (t.name != "ST_mode_s_eng02.tex" || t.format != kGXRGBA8) continue;
    auto keyboard = MakeKeyboardDecidePrompt(DecodeRGBA8(t.texels, t.width, t.height), t.width,
                                             t.height);
    if (keyboard.empty()) continue;
    // Diagnostic: REFARE_PROMPT_DUMP=<dir> writes the generated image (PAM).
    if (const char* dump = std::getenv("REFARE_PROMPT_DUMP")) {
      std::ofstream pam(std::filesystem::path(dump) / "ST_mode_s_eng02_keyboard.pam", std::ios::binary);
      pam << "P7\nWIDTH " << t.width << "\nHEIGHT " << t.height
          << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
      pam.write(reinterpret_cast<const char*>(keyboard.data()), std::streamsize(keyboard.size()));
    }
    RefarePromptTextures::Register(t.texels, t.width, t.height, kGXRGBA8, std::move(keyboard));
    ++registered;
  }
  if (!registered)
    std::fprintf(stderr, "[refare] button prompts: %s not found or unexpected; keyboard prompts off\n",
                 archive.string().c_str());
  // Any thread that presents; runs once per displayed frame.
  m_impl->hook = GetVideoEvents().before_present_event.Register(
      [impl = m_impl.get()](PresentInfo&) { impl->Poll(); });
}

ButtonPrompts::~ButtonPrompts() {
  m_impl->hook.reset();
  RefarePromptTextures::SetKeyboard(false);
}
}  // namespace refare
