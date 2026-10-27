# Project status

Initial target: macOS ARM64. Windows and Linux are future targets.

## Completed

- Imported ModernGekko-Template at `c2553a99584b2045dbe46a70e1570994dae8beef`.
- Retained pinned submodules: DolRecomp `1bec3554ecc4817cf78319ca3d8a0669477f29fa`,
  ModernGekko `edd460adad0e82b56909af1d793642c83b6d31c8`, and its RecompCore
  `10f7ac4fe48f74485fbc161ebf4d67242e11e671`.
- Passed template compiler/tool preflight on Apple Silicon (AppleClang 21).
- Built DolRecomp natively and passed all 19 upstream CTest tests.
- Built ModernGekko port tool and runner as native ARM64 Mach-O executables.
- Compiled and linked the Crazy Taxi module as an ARM64 Mach-O `.dylib`.
- Headless startup logged static-recomp core initialization and native module
  loading at entry `0x80003100`, with no interpreter fallback enabled.
- Identified personal dump: Crazy Taxi USA, `GCTE51`, revision 0.
- Expanded the CISO block map into a local ISO and verified every present block.
- Extracted main.dol and 143 game asset files; no `.rel` files found.
- Generated initial Gekko C output: 56 chunks, 222,640 decoded instruction slots,
  16 classified as embedded data, zero unknown instructions.

## User testing

- User reports visible gameplay running well with keyboard input.
- DualShock 4 identified; controller mapping has not been validated.
- Window-following aspect ratio and perspective correction enabled in
  `build/user/Config/GFX.ini`; resizing and ultrawide gameplay need visual checks.

## Investigation

DolRecomp reports possible executable-memory patching. The report is local at
`generated/crazy-taxi/generated/generated_smc.txt`. These are candidates, not
confirmed game-specific incompatibilities. Validate runtime behavior before
adding any patches.

## Next milestones

1. Validate visible startup, frame progress, and clean shutdown with Metal.
2. Investigate any runtime failures and executable-memory patching candidates.
3. Test title screen, menus, both cities, Crazy Box, audio, input, and saves.
4. Package a macOS application and a user-supplied-disc preparation flow.
5. Add Windows/Linux build and gameplay validation using the shared pipeline.

No playable-game claim is made at this stage. Keep generated code, binary
modules, extracted assets, personal images, and runtime user data out of commits.

## Local upstream fixes

The workspace path includes spaces and parentheses. Two reproducible patches
add CMake `VERBATIM` to ModernGekko copy commands and RecompCore's module-table
generator command. The ModernGekko patch also avoids forcing Ninja response
files on POSIX, where parenthesized object paths were not quoted. The Windows
response-file behavior is preserved. `make` applies these patches idempotently through
`scripts/apply-patches.sh`; the upstream submodule revisions remain pinned.
These are build quoting fixes, not changes to game behavior.

## Local artifacts

- Native modules: `build/modules/GCTE51/` (DOL/toolchain-keyed cache).
- Build and initial headless startup logs: `build/logs/`.
- Isolated startup user data: `build/smoke-user/`.

The headless check uses Null graphics/audio and a bounded run; module loading
does not establish title-screen progress or successful gameplay.

## Options overlay

- Added native L1/F1 options overlay, available at the title screen and during
  gameplay. A title-screen-only gate is not implemented because a verified
  game-state address has not been established.
- Resolution scale and aspect ratio update immediately and persist to the
  active runtime user profile; native game code is unchanged.
- Rebuilt the ARM64 runner and passed the focused ImGui/control/persistence
  test, including input release on close and preservation of other settings.
- Physical DualShock 4 input remains unverified. Attempts at visual Metal
  testing in this session failed to create a render encoder before gameplay;
  UI inspection also timed out. The cause has not been established. This test
  used isolated `build/options-test-user` data; `build/user` was not changed.
- Test logs: `build/logs/options-build.log`, `options-test.log`, and
  `options-ui-test.log`.

## Metal performance HUD

- Metal HUD defaults on for the macOS runner. Its process-local HUD machinery
  is enabled before runtime/device initialization, allowing live re-enabling
  even when the profile's saved preference is Off.
- Added a third options row on macOS, toggled with D-pad left/right. Visibility
  is applied to the render view's CAMetalLayer on the main thread using
  `developerHUDProperties`; unrelated layer properties are preserved.
- The choice is saved in `[Video] metal_hud` in `config.ini` and retained by
  the normal frontend config reader/writer. No global macOS preferences change.
- ARM64 build and focused menu tests passed, including default On, toggling
  Off/On, reopen/reload, and retaining Off through a frontend settings save.
  Live visual HUD verification remains outstanding; this session previously
  encountered Metal render-encoder failures during visual testing.
- Logs: `build/logs/metal-hud-build.log` and `metal-hud-test.log`.

## Later: 120 FPS presentation

Investigate render-only sub-frame interpolation while keeping simulation and
physics at the original rate. Requires game-specific camera/object/animation
snapshots, render hooks, and validation of culling, particles, HUD, and latency.
This is a future milestone, not enabled by changing the frame limiter.

## Startup, controller, and pacing fixes

- Startup skips use a separate staged DOL checked against the USA revision's
  SHA-256 and expected instructions. Copyright initialization is retained while
  the draw is suppressed; Acclaim playback is bypassed after its pending read,
  preserving Sega playback and common finalization. The animated title screen
  was confirmed in `build/final-pacing-automation/screen-4.png`.
- A live test detected `SDL/0/DUALSHOCK 4 Wireless Controller` and created a
  GameCube port-1 profile with keyboard alternatives. Reconnection handling and
  preserving custom mappings were tested; physical button presses still need
  user confirmation in a focused game window.
- Enabled VSync/precise-timer/smoothing defaults, removed render-thread input
  polling, guarded absent Metal drawables, and added display-compatible
  minimum-duration presentation. Explicit settings remain respected.
- Metal tracing confirmed regular 16.67 ms display intervals, replacing the
  previous recurring 8.33/25 ms alternation. Loading/capture outliers remain;
  see PERFORMANCE.md. The final native test exited cleanly with no interpreter
  fallback or failed SMC verification.
- Focused menu/controller/settings tests and startup patch guards passed.

The earlier broad legal-screen call skip was rejected during validation in
favor of preserving initialization. Only the current manifest is used by make.
The future 120 FPS interpolation milestone must also update the 60 FPS Metal
presentation policy; it is not enabled in this build.

## Centered, undistorted HUD/menu rendering

- User selected wide 3D with only HUD/menus centered and unstretched.
- Added an opt-in orthographic projection correction to RecompCore, enabled by
  default for ReFare. It scales clip XY around the center, including translation,
  while preserving depth/W and the existing wide perspective view.
- Projection constants now refresh when aspect correction changes, including
  window resizing without a new projection upload from the game.
- Matrix tests passed for 16:9, ultrawide, portrait, and original 4:3 layouts:
  equal-size HUD axes retain equal physical size; the center and depth stay fixed.
- ARM64 runner rebuilt; current `build/user` configured for window-following
  perspective correction and centered 2D projection, preserving resolution scale.
- Live Metal verification at 1280x720 confirmed a centered, proportion-correct
  title/menu layout. Capture: `build/centered-hud-automation/screen-1.png`.
  The test exited cleanly with no interpreter fallback or failed SMC validation.

## Session 2 (Claude): reproducible patches, widescreen culling, menus, pacing

### Build reproducibility
- The previous checkpoint left fixes only in local submodule commits; `patches/`
  no longer matched, so `make` failed. Every runtime change now lives in
  `patches/` against the upstream pins (ModernGekko `edd460a`, RecompCore
  `10f7ac4`), and the superproject pins those upstream commits again.
- Verified: clean bases + `scripts/apply-patches.sh` reproduce the working tree
  byte-for-byte, and the script detects every patch as applied (no-op).
- The stale `patches/checkpoint/` backup diffs were removed.

### Rendering
- Shadows: aligned at all aspect ratios (screen-space composite excluded from
  HUD centering). Confirmed in play.
- Tiling seams on menus: 2D sprite draws whose texture coordinates stay in
  [0,1] (identity texgen and dual-transform matrices) sample with clamp instead
  of wrap (`[Hacks] ClampSpriteTextureEdges`, on by default). Manual texture
  sampling is not used; it bypasses this and is slower.
- Widescreen culling, via a built-in code mod (`src/refare_widescreen_culling.cpp`):
  - Entry hook on `0x8000D9BC` (frustum plane builder) rewrites the camera's
    horizontal half-angle sin/cos (+24/+32, +48/+56) for the widened view.
  - Patch of `0x80065E40` (per-frame cone setter for the pedestrian, traffic and
    prop visibility test `0x80065ECC`): stores 1/sin and 1/tan of the widened
    half-angle. Verified in live memory at 32:9 (59.4 degrees vs 30 at rest).
  - Recomputed from unmodified fields each call, so nothing compounds; no effect
    in 4:3 mode.

### Menu, input and window
- L1 menu: HUD and menus Centered/Stretched, FXAA (Dolphin FXAA shader), Exit
  The Game with an "Are you sure?" prompt (No left and default, Yes right).
  Cross/Enter confirm. Focused test updated.
- Input only while ReFare is the active app (Spotlight no longer drives the
  game); plain key presses no longer beep; Cmd+Return toggles fullscreen.
- Automation no longer forces background input.

### Frame pacing (measured with the pacing recorder, on-glass times)
- macOS: latency-critical activity (no App Nap or timer throttling) and
  user-interactive QoS for the CPU and video threads. Fixed focus-loss stutter
  and cut submit latency from ~23 ms to ~2-5 ms.
- Presentation catch-up: after a stall (fullscreen switch, resize), stale frames
  are skipped until presentation is back on schedule, instead of carrying 70+ ms
  of extra latency for a minute (59.94 Hz emulation vs 60 Hz display).
- Measured: windowed 595/597 frames at exactly 16.67 ms on glass; fullscreen
  ~4 ms submit latency with ~4% of frames one 120 Hz vblank early/late.
- Rejected: scheduled presentation (`presentDrawable:atTime:`) produced severe
  judder and no presented times in this setup.

### Diagnostics (off unless the environment variable is set)
- `REFARE_GFX_TRACE_DIR=<dir>`: touch `<dir>/trace-request` for a GPU draw/copy
  trace, `<dir>/pacing-request` for a 10 s frame-timing CSV (with on-glass times).
- `REFARE_DL_TRACE=1`: display-list call-stack tracer (slow; hooks fire
  unreliably after the fallback JIT takes over, see below).
- Runtime note: after an entry hook fires, execution continues in the fallback
  JIT, and JIT-linked calls can bypass later hooks. Prefer hooks on functions
  reached through the dispatcher, or function patches.

### Open
- Metal HUD occasionally reports GPU 0.00 ms.
- 120 FPS presentation (render-only interpolation) not started.
