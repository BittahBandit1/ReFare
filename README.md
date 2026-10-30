# ReFare

Crazy Taxi (GameCube) static recompilation project using
[ModernGekko-Template](https://github.com/ExpansionPak/ModernGekko-Template),
[DolRecomp](https://github.com/ExpansionPak/DolRecomp), and
[ModernGekko](https://github.com/ExpansionPak/ModernGekko).

macOS ARM64 is the first target. Windows, Linux, Android and iOS are currently
being researched. Thorough testing of arcade rounds has been done, and play at full
speed on an M3 Pro MacBook Pro with GPU headroom to spare. Testing has not 
been done on lower end hardware at this time. 
(see [docs/120FPS.md](docs/120FPS.md)). See [project status](docs/STATUS.md).

Features:
- Faster loading (removed Acclaim animated video)
- Variable aspect ratio (32:9, 21:9, 16:9 etc.)
- Up to 180Hz gameplay via matrix interpolation
- Preserved HUD art (Centered by default; Stretched is optional)

**You need your own NTSC-U copy of the game.** ReFare contains no game code, data or
assets. It translates the `main.dol` from your own legally obtained Crazy Taxi
(USA, GCTE51) disc image and loads the rest of the game's files from it at run
time.

To facilitate faster loading, the beginning copyright screen and the Acclaim animated video were removed.

## The ReFare app (no build needed)

Releases include **ReFare.app** for Apple Silicon Macs (macOS 13 or later). It
contains no game code or data. Open it, then drop your own Crazy Taxi (USA,
GCTE51) `.iso` or `.ciso` onto the window, or click **Choose File…**. ReFare
checks the disc, extracts it and recompiles the game for your Mac once (a few
minutes); after that the window just shows **Play**. Everything it makes is kept
in `~/Library/Application Support/ReFare`.

Recompiling needs free developer tools, which the app checks for and explains:
Apple's Command Line Tools (`xcode-select --install`) and CMake + Ninja
(`brew install cmake ninja` from [Homebrew](https://brew.sh)).

The app is not notarized (that needs a paid Apple Developer account), so macOS
blocks the first launch. Open it once, then go to **System Settings → Privacy &
Security** and click **Open Anyway** (on macOS 14 and earlier, right-click the
app and choose **Open**).

Maintainers build the release with `make tools` and then
`bash scripts/make-app.sh`, which writes `build/release/ReFare-<version>-macOS-arm64.zip`
and refuses to package anything from a disc.

## Build on Apple Silicon

Install Xcode command line tools and `brew install cmake ninja pkg-config`.
Then, from the project directory:

```sh
git submodule update --init --recursive
make check
make tools JOBS=8
```

The portable C backend is the initial baseline. The optional LLVM backend needs
LLVM 19 or 20; it is not required for the first native ARM64 build.

## Prepare your disc

For a GameCube block-map CISO, convert to a local sparse ISO:

```sh
mkdir -p iso
python3 scripts/ciso-to-iso.py '/path/to/Crazy Taxi (USA).ciso' iso/Crazy-Taxi-USA.iso
make extract ISO=iso/Crazy-Taxi-USA.iso
```

For an existing ISO, pass its path directly to `make extract ISO=...`.
The initial supported revision is USA `GCTE51`, revision 0. Disc metadata and
main executable hash are recorded in [disc-profile.json](docs/disc-profile.json).
No disc image, extracted assets, or generated game code is tracked by Git.
Keep your personal dump outside the source tree or under the ignored `iso/` directory.

## Recompile and run

Once extracted:

```sh
make configure-video
make recompile GAME=Crazy-Taxi-USA JOBS=8
make run GAME=Crazy-Taxi-USA JOBS=8 RUN_ARGS="--graphics Metal --user-dir build/user"
```

These commands build the runtime, generate game code, compile a native module,
and (for `run`) start the runner. A successful compile alone does not establish
that gameplay, graphics, sound, or controllers work correctly.

## Portability

On macOS the Makefile defaults to `CMAKE_OSX_ARCHITECTURES=arm64`. This flag is
omitted on other systems. `CMAKE_PLATFORM_ARGS` can override it. Local CMake
quoting patches are applied automatically by `make` (see `patches/`). GameCube
extraction does not download Wii extraction tools. No platform-specific game
patches have been introduced.

[The original template guide](docs/UPSTREAM-TEMPLATE.md) covers other platforms,
LLVM, controller profiles, build variables, and upstream component licenses.

## Arbitrary aspect ratios

`make configure-video` enables a window-following aspect ratio in `build/user`.
Restart the runner after applying it. Resize the window or enter fullscreen to
match the display, including 16:10, 16:9, and ultrawide proportions. No native
module rebuild is required. The internal rendering resolution is independent of
the window's aspect ratio.

This uses the runtime's perspective-projection correction: wider windows reveal
more of the 3D view instead of simply stretching it. HUD and menu projections
retain their original proportions in a centered region. Narrow windows center
the layout vertically; the 3D view still uses the runtime's vertical-view
correction. A built-in code mod widens the game's own view culling to match, so the sides
of wide and ultrawide views stay populated.

To restore the original 4:3 presentation:

```sh
make configure-video ASPECT=original
```

For a different runtime profile use `USER_DIR=/path/to/user` with this target and
pass that same directory through the runner's `--user-dir` argument. The helper
preserves unrelated GFX settings and keeps the first existing file as
`GFX.ini.before-refare`.

## In-game options

Press **L1** on a connected DualShock 4 (left shoulder on other SDL gamepads),
or **Tab** on the keyboard, to open the ReFare options overlay. It is available
at the title screen and during play; no game-state addresses are assumed.
Both buttons can be changed from the menu itself (see **Options menu button** below).

- D-pad up/down selects resolution scale, aspect ratio, HUD and menus
  (Centered/Stretched), FXAA, Interpolation (experimental: Off, Match display, 90–180 FPS), V-Sync, Metal HUD (macOS),
  options menu button, or Exit The Game.
- D-pad left/right changes the selected setting; Cross/Enter toggles or selects.
- **Options menu button**: press Cross/Enter (or click Change), then press the
  controller button or key that should open the menu from now on. A controller
  press changes the controller button, a key changes the key; Circle/Escape
  cancels, and left/right (or Reset) restores L1 / Tab. The D-pad, Cross, Circle,
  sticks, arrow keys, Enter and Escape can't be used. Saved in
  `Config/ReFare.ini`.
- Exit The Game asks "Are you sure?" (No on the left, Yes on the right).
- Circle or the menu button (L1 by default) closes the menu. Keyboard alternatives: arrow keys, Enter, and Escape or the menu key (Tab by default).

Cmd+Return toggles fullscreen. The game ignores input while another app (for
example Spotlight) is active.

Resolution scales: 1x, 2x, 3x, 4x, 6x, 8x, and 12x. Aspect choices: match window,
original 4:3, 16:9, 16:10, 21:9, and 32:9. Changes apply immediately and save in
the active user directory. Fixed wide ratios use projection correction and fit
inside the current window; match-window mode follows arbitrary window sizes.
The game-culling limitations described above still apply.

On macOS, **Metal HUD** defaults to **On**. Its menu toggle changes visibility
live and saves the choice as `metal_hud` in the profile's `config.ini`. A saved
Off choice remains off on subsequent launches. This controls Apple's HUD for
ReFare's Metal layer; no system-wide preferences are changed. Launch with the
Metal backend to display it.

The runtime automatically maps a detected SDL gamepad to GameCube port 1,
prioritizing a DualShock 4 and retaining keyboard controls. It checks for
reconnection and preserves hand-authored profiles. The raw L1 overlay binding
is independent of the game's pad mapping. The overlay does not
pause the game, but blocks ordinary game input while open using the runtime's
UI input gate (with the default foreground-input mode).

The menu code lives in `src/`; small recorded runtime patches attach it to the
existing renderer. Build it with the usual `make run` command. To run its
focused rendering/state/persistence test:

```sh
cmake --build lib/ModernGekko/build --target refare-options-test
lib/ModernGekko/build/refare-options-test build/options-unit-test
```

## Startup skips and controller setup

The normal `make run` path creates a separate revision-checked copy under
`build/games/` and skips the copyright display and Acclaim Cheltenham movie.
The Sega movie remains. The original disc image and extraction are untouched.
The copyright texture still loads for initialization, but its draw is suppressed.
To run the original startup sequence, add `SKIP_STARTUP=0` to `make run`.

Connect the DualShock 4 before launching, or reconnect it while the game runs.
The runtime creates `Config/GCPadNew.ini` in the selected user directory if no
custom mapping exists. On DS4: Cross=A, Circle=B, Square=X, Triangle=Y,
Options=Start, R1=Z, L2/R2=GameCube triggers, and the sticks/D-pad map normally.
L1 opens ReFare Options. Existing keyboard bindings are retained alongside it.
Automatic profiles have `[ReFare] AutoConfigured=True`; set this to False after
making custom changes if you want the runtime to leave the profile alone.

## Keyboard button prompts

Crazy Taxi's on-screen prompts are baked into textures, e.g. "Use the A
Button to decide." on the mode select screen. When you play with the
keyboard, ReFare shows "Use the X Key to decide." instead. The prompt follows
the device you used last: any key switches to keyboard prompts, any controller
button or stick switches back. A key only counts while the game window has
focus.

The keyboard texture is built at startup from your own `sprCENG.all`. It keeps
the game's own glyphs for every letter except X, K and y, which are rendered
from Roboto Condensed (SIL Open Font License 1.1). No game data is stored in
this repository. Set `REFARE_PROMPT_DUMP=<dir>` to write the generated image
for inspection.

## Frame pacing

New profiles default to VSync, precise timing, and presentation smoothing.
On Metal with a 60/120 Hz-compatible display, a minimum presentation duration
prevents recurring short/long display intervals. Other display cadences retain
the normal presentation path. Fast-forward with VSync inactive is not capped
by this path. The overlay no longer polls devices on the rendering thread.

Explicit user settings are respected. In `Config/GFX.ini`, the optional
`[Settings] MTLMinimumPresentDuration` value is -1 for automatic 60 Hz pacing,
0 to disable the minimum-duration policy, or a duration in seconds. Automatic
selection uses the display associated with the window at renderer startup.
Restart after changing to a display with a different refresh cadence.

See [performance validation](docs/PERFORMANCE.md) for measured results and limits.

## Centered HUD and menus

The default `CenterOrthographicProjection=True` in `Config/GFX.ini` centers the
original 2D layout without distortion while retaining the wide 3D view. It
follows window resizing and fixed aspect choices. Use Match window / display
in Options for arbitrary window dimensions; Original 4:3 still applies to the
entire picture when explicitly selected.

The correction scales both XY rows of the 2D projection, including translation,
so the center stays fixed and element proportions remain unchanged. Z/W and
perspective projection behavior are preserved. This applies to the game's
orthographic passes, including 2D transition artwork. It does not alter the
native ReFare options overlay, which is already rendered in screen coordinates.

## Contributors

- [BittahBandit1](https://github.com/BittahBandit1): creator and maintainer
- ChatGPT Codex (OpenAI): development
- Claude (Anthropic): development

## License

ReFare is free software under the [GNU General Public License v3.0](LICENSE).
Its code is built into ModernGekko and its Dolphin-derived runtime, which are
GPL-3.0 (see `lib/ModernGekko/LICENSE` and `lib/DolRecomp/LICENSE`). The three
glyphs used for keyboard button prompts are rendered from Roboto Condensed,
licensed under the SIL Open Font License 1.1.

ReFare is an unofficial fan project. It is not affiliated with or endorsed by
SEGA or Nintendo. Crazy Taxi is a trademark of SEGA. No disc image, extracted
game data or copyrighted game assets are included in this repository.
