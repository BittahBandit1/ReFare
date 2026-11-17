# ReFare settings inside the game's OPTIONS screen

Desktop builds add a **REFARE** button, next to EXIT, to Crazy Taxi's own OPTIONS screen
(GCTE51 revision 0). It opens a submenu that looks and works like the game's
other options: resolution, aspect ratio, HUD and menus, FXAA, MetalFX
upscaling (Apple), interpolation, V-Sync, Metal HUD (Apple) and the FPS
counter, then EXIT. Up/down picks a row, left/right changes it, EXIT or B goes
back. The ImGui overlay (L1 / Tab) still works and the two stay in sync.

## How it works

Crazy Taxi's menus are data, driven by one routine that runs every frame while
a menu is shown (`0x800232A4`). Starting from the root menu (`0x800DDFB0`) it
follows the cursor stack at `0x80229590` (depth, then one cursor per depth) to
the current menu, calls each item's handler `(selected, index)`, then the
menu's draw function.

```
menu  { void (*draw)(); u32 count; { menu* submenu; void (*handler)(int, int); } items[count]; }
label { float x, y, scale; char text[32]; }    // 44 bytes
```

OPTIONS is the menu at `0x800DDEC8` (9 items), reached from root item
`0x800DDFD0`. Its draw function `0x8002274C` renders the label table
`0x80156604` with the game's list renderer `0x8001B3DC` and sets up
navigation for 9 items (`0x8001A798`). Handlers draw their current choice
with `0x8001B98C` and change it with `0x8001AC54` (left/right, clamped).
Choosing an item with a submenu opens it; the action menu `0x800DDDC8` goes
back one level.

`src/refare_game_menu.cpp` hooks the menu routine. The first time it runs, it
checks the game code it relies on, then writes into guest RAM the game never
uses (`0x80001820`–`0x80002FFF`; Dolphin's HLE stub ends at `0x8000180B`):

- a 10-item OPTIONS menu: the game's 9 items, then REFARE. The game's layout
  is unchanged: REFARE sits on EXIT's row, to its right, with the same short
  button (down from EXIT selects it). The list renderer gives the short button
  only to the last item it draws, so the OPTIONS draw copy calls it twice:
  items 0-8, then REFARE on its own;
- the REFARE submenu (rows 31 units apart so 9 settings and EXIT fit above
  the help bar), whose items share one small handler;
- label and choice tables in the game's format;
- two copies of `0x8002274C` with the table, item count and help text changed;
- the handler, built like the game's AUDIO handler (`0x8001D554`).

Finally it points root item `0x800DDFD0` at the new OPTIONS menu. Only the
game's own code (or copies of it) runs; ReFare reads and writes the data.

Every frame the hook compares the submenu's values with ReFare's settings.
A change made in the game's menu is sent to the render thread, which applies
and saves it exactly like the overlay does (`RequestGameMenuChoice`); a change
made in the overlay is written back into the game's menu
(`GameMenuSnapshot`).

If the expected code isn't found, or that RAM isn't empty, ReFare logs a
message and leaves the game's menus alone. `REFARE_GAME_MENU=0` disables the
feature.

## Status

- macOS: tested with the runner (layout, every row visible, left/right changes
  applied and saved, back/EXIT, normal play afterwards).
- Windows and Linux: the same code path; untested.
- Android and iOS: their hosts don't load ReFare's built-in game hooks yet, so
  only the overlay / three-finger menu is available there.
