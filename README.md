# ReFare

Crazy Taxi (GameCube) static recompilation project using
[ModernGekko-Template](https://github.com/ExpansionPak/ModernGekko-Template),
[DolRecomp](https://github.com/ExpansionPak/DolRecomp), and
[ModernGekko](https://github.com/ExpansionPak/ModernGekko).

macOS ARM64 is the first target. Windows and Linux remain planned targets using
the same upstream CMake runtime and generated game code. Startup and title-screen rendering have been validated on macOS ARM64. Full
gameplay coverage remains in progress. See [project status](docs/STATUS.md).

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

- D-pad up/down selects resolution scale, aspect ratio, HUD and menus
  (Centered/Stretched), FXAA, Interpolation (experimental: Off, Match display, 90–180 FPS), V-Sync, Metal HUD (macOS), or
  Exit The Game.
- D-pad left/right changes the selected setting; Cross/Enter toggles or selects.
- Exit The Game asks "Are you sure?" (No on the left, Yes on the right).
- Circle or L1 closes the menu. Keyboard alternatives: arrow keys, Enter, and Escape/Tab.

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
