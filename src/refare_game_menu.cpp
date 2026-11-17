#include "refare_game_menu.hpp"
#include "refare_options.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// ReFare's settings inside Crazy Taxi's own OPTIONS screen (GCTE51 rev 0).
//
// The game's menus are data. 0x800232A4 runs every frame while a menu is up:
// starting at the root menu 0x800DDFB0 it follows the cursor stack at
// 0x80229590 (depth, then one cursor per depth) to the current menu, calls
// every item's handler(selected, index), then the menu's draw function.
//   menu  = { void (*draw)(); u32 count; { menu* submenu; void (*handler)(int, int); } items[count]; }
//   label = { float x, y, scale; char text[32]; }   (44 bytes)
// OPTIONS is the menu at 0x800DDEC8 (9 items, referenced from root item
// 0x800DDFD0). Its draw function 0x8002274C draws the label table 0x80156604
// with the generic list renderer 0x8001B3DC and sets up up/down navigation
// for 9 items (0x8001A798). Item handlers draw their value with 0x8001B98C
// (one record of a choice table) and change it with 0x8001AC54
// (left/right, clamped to [0, count)). Choosing an item with a submenu opens
// it; the action menu 0x800DDDC8 goes back up one level.
//
// ReFare builds, in guest RAM that the game never uses (0x80001820-0x80002FFF,
// below the DOL's first section; Dolphin's HLE stub sits just before it), a 10-item copy of OPTIONS with a
// "REFARE SETTINGS" item to the right of EXIT, and a REFARE submenu whose items all use
// one small handler. The two draw functions are copies of 0x8002274C with the
// table, item count and help text adjusted. Everything the game executes is
// its own code or a copy of it; ReFare only reads and writes the data each
// frame, from this hook, to stay in step with the settings.
namespace refare {
namespace {
// Dolphin's HLE uses only 0x80001800-0x8000180B (HBReload stub, "STUBHAXX").
constexpr uint32_t kBase = 0x80001820u;
constexpr uint32_t kEnd = 0x80003000u;
constexpr uint32_t kMagic = 0x52464D31u;  // "RFM1"
constexpr uint32_t kMaskAddr = kBase + 0x010;
constexpr uint32_t kValues = kBase + 0x020;    // u32[16]
constexpr uint32_t kCounts = kBase + 0x060;    // u32[16]
constexpr uint32_t kOptMenu = kBase + 0x0A0;   // 8 + 10*8
constexpr uint32_t kRfMenu = kBase + 0x100;    // 8 + 16*8
constexpr uint32_t kOptLabels = kBase + 0x190; // 10 * 44
constexpr uint32_t kRfLabels = kBase + 0x350;  // 12 * 44
constexpr uint32_t kOptDraw = kBase + 0x560;
constexpr uint32_t kRfDraw = kBase + 0x6B0;
constexpr uint32_t kHandler = kBase + 0x800;
constexpr uint32_t kChoices = kBase + 0x900;
constexpr int kMaxRows = 9;       // value rows (9 + EXIT fill the screen)
constexpr int kMaxChoices = 7;
constexpr uint32_t kRecord = 44;
constexpr uint32_t kChoiceStride = kMaxChoices * kRecord;
static_assert(kChoices + kMaxRows * kChoiceStride <= kEnd);

// Game addresses (GCTE51 rev 0).
constexpr uint32_t kRootOptionsItem = 0x800DDFD0u;  // root item -> OPTIONS menu
constexpr uint32_t kOptionsMenu = 0x800DDEC8u;
constexpr uint32_t kBackAction = 0x800DDDC8u;
constexpr uint32_t kOptionsLabels = 0x80156604u;
constexpr uint32_t kOptionsDraw = 0x8002274Cu;
constexpr uint32_t kOptionsDrawEnd = 0x8002288Cu;  // blr
constexpr uint32_t kAdjust = 0x8001AC54u;
constexpr uint32_t kDrawChoice = 0x8001B98Cu;
constexpr float kRowY0 = 112.0f, kLabelX = 184.0f, kValueX = 478.0f;
// OPTIONS keeps the game's layout: EXIT stays on its row (y 384) and
// REFARE SETTINGS goes on the same row, to its right. The REFARE page has
// 9 settings + EXIT, so its rows are 31 apart instead of the game's 34; EXIT
// then ends above the help bar.
constexpr float kExitY = 384.0f, kRefareX = 377.0f, kSubmenuStep = 31.0f;
float RowY(int row) { return kRowY0 + kSubmenuStep * static_cast<float>(row); }

uint32_t Read32(CPUState* s, uint32_t a) { return static_cast<uint32_t>(moderngekko_mod_read(s, a, 4)); }
void Write32(CPUState* s, uint32_t a, uint32_t v) { moderngekko_mod_write(s, a, v, 4); }
void WriteF(CPUState* s, uint32_t a, float v) { Write32(s, a, std::bit_cast<uint32_t>(v)); }

void WriteLabel(CPUState* s, uint32_t at, float x, float y, const std::string& text) {
  WriteF(s, at, x); WriteF(s, at + 4, y); WriteF(s, at + 8, 1.0f);
  char buffer[32] = {};
  for (size_t i = 0; i < text.size() && i < 31; ++i)
    buffer[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
  for (uint32_t i = 0; i < 32; i += 4)
    Write32(s, at + 12 + i, (uint32_t(uint8_t(buffer[i])) << 24) | (uint32_t(uint8_t(buffer[i+1])) << 16) |
                            (uint32_t(uint8_t(buffer[i+2])) << 8) | uint8_t(buffer[i+3]));
}

// PowerPC encoders.
constexpr uint32_t Ha(uint32_t a) { return (a + 0x8000u) >> 16; }
constexpr uint32_t Lo(uint32_t a) { return a & 0xFFFFu; }
constexpr uint32_t Addi(int d, int a, uint32_t imm) { return 0x38000000u | d << 21 | a << 16 | (imm & 0xFFFFu); }
constexpr uint32_t Li(int d, int imm) { return Addi(d, 0, static_cast<uint32_t>(imm)); }
constexpr uint32_t Lis(int d, uint32_t imm) { return 0x3C000000u | d << 21 | (imm & 0xFFFFu); }
constexpr uint32_t Ori(int a, int s, uint32_t imm) { return 0x60000000u | s << 21 | a << 16 | (imm & 0xFFFFu); }
constexpr uint32_t Lwz(int d, int a, int off) { return 0x80000000u | d << 21 | a << 16 | (off & 0xFFFF); }
constexpr uint32_t Stw(int s, int a, int off) { return 0x90000000u | s << 21 | a << 16 | (off & 0xFFFF); }
constexpr uint32_t Stwu(int s, int a, int off) { return 0x94000000u | s << 21 | a << 16 | (off & 0xFFFF); }
constexpr uint32_t Lwzx(int d, int a, int b) { return 0x7C00002Eu | d << 21 | a << 16 | b << 11; }
constexpr uint32_t Stwx(int s, int a, int b) { return 0x7C00012Eu | s << 21 | a << 16 | b << 11; }
constexpr uint32_t Add(int d, int a, int b) { return 0x7C000214u | d << 21 | a << 16 | b << 11; }
constexpr uint32_t Mulli(int d, int a, int imm) { return 0x1C000000u | d << 21 | a << 16 | (imm & 0xFFFF); }
constexpr uint32_t Slwi(int a, int s, int n) { return 0x54000000u | s << 21 | a << 16 | n << 11 | 0 << 6 | (31 - n) << 1; }
constexpr uint32_t Mr(int a, int s) { return 0x7C000378u | s << 21 | a << 16 | s << 11; }
constexpr uint32_t Cmpwi(int a, int imm) { return 0x2C000000u | a << 16 | (imm & 0xFFFF); }
constexpr uint32_t kMflrR0 = 0x7C0802A6u, kMtlrR0 = 0x7C0803A6u, kBlr = 0x4E800020u;
constexpr uint32_t Bc(int bo, int bi, int32_t disp) { return 0x40000000u | bo << 21 | bi << 16 | (static_cast<uint32_t>(disp) & 0xFFFCu); }
constexpr uint32_t Bl(uint32_t from, uint32_t to) { return 0x48000001u | (static_cast<uint32_t>(to - from) & 0x03FFFFFCu); }

struct Code {
  CPUState* s; uint32_t pc;
  void Emit(uint32_t word) { Write32(s, pc, word); pc += 4; }
  void Call(uint32_t target) { Emit(Bl(pc, target)); }
};

// Copies the OPTIONS draw function to `to`, relocating calls, then applies
// the item count, label table and help-text changes.
bool CloneOptionsDraw(CPUState* s, uint32_t to, int count, uint32_t labels, int help_a, int help_b,
                      bool keep_disable_mask) {
  for (uint32_t from = kOptionsDraw; from <= kOptionsDrawEnd; from += 4) {
    uint32_t word = Read32(s, from);
    const uint32_t at = to + (from - kOptionsDraw);
    if ((word >> 26) == 18 && (word & 2u) == 0) {  // relative b/bl: keep its target
      int32_t disp = static_cast<int32_t>(word & 0x03FFFFFCu);
      if (disp & 0x02000000) disp -= 0x04000000;
      const uint32_t target = from + static_cast<uint32_t>(disp);
      if (target < kOptionsDraw || target > kOptionsDrawEnd)
        word = (word & 0xFC000003u) | (static_cast<uint32_t>(target - at) & 0x03FFFFFCu);
    }
    Write32(s, at, word);
  }
  const auto patch = [&](uint32_t original_pc, uint32_t word) { Write32(s, to + (original_pc - kOptionsDraw), word); };
  patch(0x8002277Cu, Li(4, count));                 // navigation: item count
  if (!keep_disable_mask) {                         // disabled items from ReFare
    patch(0x80022774u, Lis(29, Ha(kMaskAddr)));
    patch(0x80022778u, Lwz(29, 29, static_cast<int16_t>(Lo(kMaskAddr))));
    patch(0x80022780u, 0x60000000u);                // nop (keeps r29)
  }
  patch(0x800227C4u, Cmpwi(0, help_a));             // "A to decide" help on these
  patch(0x800227CCu, Cmpwi(0, help_b));
  patch(0x800227D0u, Bc(12, 0, 0x44));              // blt: below help_b -> left/right help
  patch(0x80022858u, Lis(4, Ha(labels)));
  patch(0x80022864u, Addi(4, 4, Lo(labels)));
  patch(0x80022870u, Li(5, count));                 // list renderer: item count
  return true;
}

// handler(r3 = selected, r4 = index): value = kValues[index], choices =
// kChoices + index * stride, count = kCounts[index]. Same shape as the game's
// own AUDIO handler (0x8001D554) without its side effects.
void EmitHandler(CPUState* s) {
  Code c{s, kHandler};
  c.Emit(Stwu(1, 1, -32)); c.Emit(kMflrR0); c.Emit(Stw(0, 1, 36));
  c.Emit(Stw(31, 1, 28)); c.Emit(Stw(30, 1, 24));
  c.Emit(Mr(31, 4)); c.Emit(Mr(30, 3));
  c.Emit(Lis(9, Ha(kValues))); c.Emit(Addi(9, 9, Lo(kValues))); c.Emit(Slwi(10, 31, 2));
  c.Emit(Lwzx(0, 9, 10)); c.Emit(Stw(0, 1, 8));
  c.Emit(Cmpwi(30, 0));
  const uint32_t skip_adjust = c.pc; c.Emit(0);     // beq draw (patched below)
  c.Emit(Lis(9, Ha(kCounts))); c.Emit(Addi(9, 9, Lo(kCounts))); c.Emit(Lwzx(5, 9, 10));
  c.Emit(Li(3, 0)); c.Emit(Addi(4, 1, 8));
  c.Call(kAdjust);
  c.Emit(Cmpwi(3, 1));
  const uint32_t skip_store = c.pc; c.Emit(0);      // bne draw
  c.Emit(Lis(9, Ha(kValues))); c.Emit(Addi(9, 9, Lo(kValues))); c.Emit(Slwi(10, 31, 2));
  c.Emit(Lwz(0, 1, 8)); c.Emit(Stwx(0, 9, 10));
  const uint32_t draw = c.pc;
  Write32(s, skip_adjust, Bc(12, 2, static_cast<int32_t>(draw - skip_adjust)));
  Write32(s, skip_store, Bc(4, 2, static_cast<int32_t>(draw - skip_store)));
  c.Emit(Li(3, 16407));                             // 0x4017: not selected
  c.Emit(Cmpwi(30, 0));
  c.Emit(Bc(12, 2, 12));                            // beq +12
  c.Emit(Li(3, 0)); c.Emit(Ori(3, 3, 0xC017));      // selected
  c.Emit(Lis(4, Ha(kChoices))); c.Emit(Addi(4, 4, Lo(kChoices)));
  c.Emit(Mulli(9, 31, static_cast<int>(kChoiceStride))); c.Emit(Add(4, 4, 9));
  c.Emit(Lis(9, Ha(kCounts))); c.Emit(Addi(9, 9, Lo(kCounts))); c.Emit(Slwi(10, 31, 2));
  c.Emit(Lwzx(5, 9, 10));
  c.Emit(Lwz(6, 1, 8));
  c.Call(kDrawChoice);
  c.Emit(Lwz(0, 1, 36)); c.Emit(kMtlrR0); c.Emit(Lwz(31, 1, 28)); c.Emit(Lwz(30, 1, 24));
  c.Emit(Addi(1, 1, 32)); c.Emit(kBlr);
}

struct State {
  bool checked = false, usable = false, installed = false;
  int rows = 0;                       // value rows in the REFARE submenu
  std::vector<std::vector<std::string>> written_choices;
  std::array<int, kMaxRows> shown{};  // value last written to the guest
  std::array<int, kMaxRows> pending{};
  std::array<int, kMaxRows> pending_ticks{};
  uint32_t mask = 0;
};
State& St() { static State s; return s; }

bool GameMatches(CPUState* s) {
  return Read32(s, kRootOptionsItem) == kOptionsMenu && Read32(s, kOptionsMenu) == kOptionsDraw &&
         Read32(s, kOptionsMenu + 4) == 9 && Read32(s, 0x8002277Cu) == 0x38800009u &&
         Read32(s, 0x80022780u) == 0x547D0630u && Read32(s, 0x800227C4u) == 0x2C000006u &&
         Read32(s, 0x800227CCu) == 0x2C000008u && Read32(s, 0x800227D0u) == 0x40820044u &&
         Read32(s, 0x80022858u) == 0x3C808015u && Read32(s, 0x80022864u) == 0x38846604u &&
         Read32(s, 0x80022870u) == 0x38A00009u && Read32(s, kOptionsDrawEnd) == kBlr;
}

bool RegionFree(CPUState* s) {
  if (Read32(s, kBase) == kMagic) return true;
  for (uint32_t a = kBase; a < kEnd; a += 4)
    if (Read32(s, a) != 0) {
      std::fprintf(stderr, "[refare] in-game settings: guest RAM %08x is in use (%08x); not installing\n", a, Read32(s, a));
      return false;
    }
  return true;
}

void WriteChoices(CPUState* s, int row, const std::vector<std::string>& choices) {
  const int count = std::clamp(static_cast<int>(choices.size()), 1, kMaxChoices);
  const float y = RowY(row);
  for (int i = 0; i < count; ++i)
    WriteLabel(s, kChoices + static_cast<uint32_t>(row) * kChoiceStride + static_cast<uint32_t>(i) * kRecord,
               kValueX, y, i < static_cast<int>(choices.size()) ? choices[i] : "");
  Write32(s, kCounts + static_cast<uint32_t>(row) * 4, static_cast<uint32_t>(count));
}

void Install(CPUState* s, const std::vector<GameMenuRow>& rows) {
  State& st = St();
  st.rows = std::min(static_cast<int>(rows.size()), kMaxRows);
  // OPTIONS: the game's 8 setting items, REFARE SETTINGS (item 8) and EXIT
  // (item 9). The list renderer gives only the last item the short EXIT bar,
  // so EXIT stays last and keeps its own row and record; REFARE SETTINGS gets
  // a regular bar on the same row, to EXIT's right.
  Write32(s, kOptMenu, kOptDraw);
  Write32(s, kOptMenu + 4, 10);
  for (uint32_t i = 0; i < 9; ++i) {
    const uint32_t to = i < 8 ? i : 9;  // the game's EXIT becomes item 9
    Write32(s, kOptMenu + 8 + to * 8, Read32(s, kOptionsMenu + 8 + i * 8));
    Write32(s, kOptMenu + 12 + to * 8, Read32(s, kOptionsMenu + 12 + i * 8));
    for (uint32_t w = 0; w < kRecord; w += 4)
      Write32(s, kOptLabels + to * kRecord + w, Read32(s, kOptionsLabels + i * kRecord + w));
  }
  Write32(s, kOptMenu + 8 + 8 * 8, kRfMenu); Write32(s, kOptMenu + 12 + 8 * 8, 0);
  WriteLabel(s, kOptLabels + 8 * kRecord, kRefareX, kExitY, "REFARE SETTINGS");
  // Items 6 (CONTROLLER SETTING) and 8 and up open something: "A to decide".
  CloneOptionsDraw(s, kOptDraw, 10, kOptLabels, 6, 8, true);

  // REFARE SETTINGS submenu.
  Write32(s, kRfMenu, kRfDraw);
  Write32(s, kRfMenu + 4, static_cast<uint32_t>(st.rows + 1));
  st.written_choices.assign(st.rows, {});
  for (int i = 0; i < st.rows; ++i) {
    Write32(s, kRfMenu + 8 + static_cast<uint32_t>(i) * 8, 0);
    Write32(s, kRfMenu + 12 + static_cast<uint32_t>(i) * 8, kHandler);
    WriteLabel(s, kRfLabels + static_cast<uint32_t>(i) * kRecord, kLabelX, RowY(i), rows[i].label);
    WriteChoices(s, i, rows[i].choices);
    st.written_choices[i] = rows[i].choices;
    Write32(s, kValues + static_cast<uint32_t>(i) * 4, static_cast<uint32_t>(rows[i].value));
    st.shown[i] = rows[i].value;
    st.pending[i] = -1;
  }
  Write32(s, kRfMenu + 8 + static_cast<uint32_t>(st.rows) * 8, kBackAction);
  Write32(s, kRfMenu + 12 + static_cast<uint32_t>(st.rows) * 8, 0);
  WriteLabel(s, kRfLabels + static_cast<uint32_t>(st.rows) * kRecord, kLabelX, RowY(st.rows), "EXIT");
  Write32(s, kMaskAddr, 0);
  CloneOptionsDraw(s, kRfDraw, st.rows + 1, kRfLabels, st.rows, st.rows, false);
  EmitHandler(s);
  Write32(s, kBase, kMagic);
  Write32(s, kRootOptionsItem, kOptMenu);  // last: the game now opens the new OPTIONS
  st.installed = true;
  std::fprintf(stderr, "[refare] in-game settings: REFARE SETTINGS added to OPTIONS (%d rows)\n", st.rows);
}

void Sync(CPUState* s, const std::vector<GameMenuRow>& rows) {
  State& st = St();
  uint32_t mask = 0;
  for (int i = 0; i < st.rows && i < static_cast<int>(rows.size()); ++i) {
    const GameMenuRow& row = rows[i];
    if (!row.enabled) mask |= 1u << i;
    if (row.choices != st.written_choices[i]) {
      WriteChoices(s, i, row.choices);
      st.written_choices[i] = row.choices;
    }
    const int guest = static_cast<int>(Read32(s, kValues + static_cast<uint32_t>(i) * 4));
    if (guest != st.shown[i]) {  // changed in the game's menu
      st.shown[i] = guest;
      st.pending[i] = guest;
      st.pending_ticks[i] = 0;
      RequestGameMenuChoice(i, guest);
      continue;
    }
    if (st.pending[i] >= 0) {  // wait until ReFare reports the new value
      if (row.value == st.pending[i] || ++st.pending_ticks[i] > 120) st.pending[i] = -1;
      else continue;
    }
    if (row.value != guest) {  // changed elsewhere (overlay menu)
      Write32(s, kValues + static_cast<uint32_t>(i) * 4, static_cast<uint32_t>(row.value));
      st.shown[i] = row.value;
    }
  }
  if (mask != st.mask) { Write32(s, kMaskAddr, mask); st.mask = mask; }
}
}  // namespace

void GameMenuTick(CPUState* state) {
  State& st = St();
  if (st.checked && !st.usable) return;
  const std::vector<GameMenuRow> rows = GameMenuSnapshot();
  if (rows.empty()) return;  // options not published yet
  if (!st.checked) {
    st.checked = true;
    st.usable = GameMatches(state) && RegionFree(state);
    if (!st.usable) {
      std::fprintf(stderr, "[refare] in-game settings: unexpected game data; using the overlay only\n");
      return;
    }
  }
  if (!st.installed || Read32(state, kBase) != kMagic || Read32(state, kRootOptionsItem) != kOptMenu) {
    if (st.installed) std::fprintf(stderr, "[refare] in-game settings: guest data was overwritten; reinstalling\n");
    if (Read32(state, kRootOptionsItem) != kOptionsMenu && Read32(state, kRootOptionsItem) != kOptMenu) return;
    Install(state, rows);
    return;
  }
  Sync(state, rows);
}
}  // namespace refare
