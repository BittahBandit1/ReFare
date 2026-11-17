# Android ARM64 personal test port

The Android host uses a Java document picker and bounded ISO/CISO importer,
Vulkan rendering, native AAudio output and Android gamepad events. The new
0.2.5 build generates and compiles a personal game module on the device
before play; gameplay does not require JIT. The APK bundles a generic compiler,
linker and runtime, without a precompiled Crazy Taxi module.
Android 8.0 (API 26) or later and an ARM64 Vulkan-capable device are required.
Only Crazy Taxi USA GCTE51 revision 0 is accepted. Disc assets are imported on
the device, kept in private app storage, and never included in the APK.

The initial controller layout maps A/B/X/Y directly, right shoulder to Z,
L2/R2 to GameCube triggers, Start to Start, and left stick/D-pad to steering
and navigation. L1 opens the shared options menu; use D-pad and A/B there.
Options and Exit touch buttons are also provided. This initial Android host
is intended for controller-equipped handhelds; it does not yet include the
iOS touch driving overlay. Backgrounding pauses gameplay; destroying the
render surface stops the session and returns to the library.

## Build on macOS

Install Java 21, CMake, Ninja and Google's Android SDK/NDK. The script expects
SDK platform 35, build-tools 35.0.0 and NDK 27.3.13750724 below `--sdk`.
Build the generic APK (no disc or generated game code is needed):

```sh
python3 scripts/android.py --sdk build/android-sdk
```

Output: `build/release/ReFare-Android-arm64-test.apk`. It is signed with a local
ignored debug keystore for USB installation. The generic compiler packages are
downloaded from official Termux servers and verified against pinned SHA-256
hashes in `ports/android/compiler-packages.json`. Compiler licenses and NDK
notices are bundled in the APK. The roughly 77 MiB APK is larger because it
includes Clang/LLVM and LLD.

```sh
adb install -r build/release/ReFare-Android-arm64-test.apk
```

Launch ReFare, choose an ISO/CISO using Android's file picker, then Play.
The first preparation generates C code, compiles it and links the game module
in private app storage. Keep the app open; this can take many minutes on a
handheld. Cancel or Back stops preparation. Later launches reuse the module
after checking the disc, toolchain identity and module SHA-256. Changing the
disc or compiler invalidates the cache. Preparation logs are available in the
app alongside gameplay diagnostics. Personal modules and imported assets stay
on the device; do not distribute them.

The 0.2.0 APK used an older approach that embedded a precompiled game module; it
has been withdrawn. These instructions describe the local-compilation build.

## Apple graphics options

The macOS options menu now includes MetalFX spatial upscaling too. It requires
macOS 13+ and a GPU accepted by Apple's MetalFX support check. It is off by
default and persists in `Config/GFX.ini` as `Enhancements/MetalFXSpatial`.
Use a rendering resolution below the window resolution to get upscaling;
at equal or larger resolutions the ordinary presentation path remains active.
HUD/menu placement is applied independently. The M3 Pro test reached 60 FPS
with MetalFX enabled and a separate test profile.

## Device validation (November 8, 2026)

Test device: Retroid Pocket Nova, Android 13, ARM64, QCS8550, stock Qualcomm
Vulkan driver. The personal CISO imported 143 files directly on-device.
The native static module boots, renders menus, the 3D attract sequence and active Arcade gameplay,
and feeds non-silent 48 kHz stereo samples to AAudio. L1 opens the shared
options overlay; touch FXAA changes persist in GFX.ini. The Exit confirmation
was checked and a confirmed exit returned cleanly to the library. Top buttons
hide after three seconds, reappear on screen touches and hide during gamepad use.
The app retained its imported assets and memory-card data across APK updates.
A test session passed 1,168,920 presented frames without a native crash.

The first build is experimental: startup/loading and shader compilation still
cause pauses. Input verification uses injected Android gamepad key/motion events;
D-pad and analog navigation, driver selection and R2 acceleration were checked
in Arcade gameplay. Physical button/analog feel still needs user testing. The initial port has no
touch driving overlay or custom Vulkan driver picker. Public importer tests
use synthetic images and require only a host JDK; all 25 Python test cases pass.

### D-pad fix (0.1.1-test)

D-pad input now accepts Android HAT_X/HAT_Y motion events as well as key
events. Hat directions feed both GameCube D-pad controls and the main-stick
fallback used by Crazy Taxi menus, plus the L1 options navigation. Neutral
and canceled events release the directions. Host regression checks cover
diagonals, reversals, release and analog-stick coexistence. Version 0.1.1-test was installed and checked on the reconnected Retroid.
Android reports its physical D-pad as joystick HAT_X/HAT_Y. Injected matching
hat events verified all four directions in the L1 menu, changing and restoring
aspect ratio, and Up/Down movement in the game menu (Arcade to Original and
back). Returning the hat to neutral stopped navigation.

## Local compilation validation (November 16, 2026)

The new generic APK was installed on a Retroid Pocket 5 (Snapdragon 865,
Android 13). It imported the personal CISO, generated 56 C chunks, compiled
all 63 translation units locally with bundled Clang, linked the module and
loaded it through the runtime's dynamic module path. The first preparation
took about 28 minutes. The title screen, 3D attract scene, game menus and
non-silent 48 kHz audio were observed. Injected held Start/A/B button events
navigated the game menus. This is not a physical controller feel assessment.

Reinstalling the updated generic APK retained the import/module and reopened
the game without invoking the compiler again. A fresh compilation was then
canceled with Back: staging was removed and compiler processes stopped. The
completed module was restored and the app left at its library. The tested
firmware reports permissive SELinux, so this does not establish compatibility
with every stock/enforcing Android configuration. Startup and shader-loading
pauses remain noticeable even after the game module is cached.

### FPS counter and frame pacing diagnostics

The L1 menu includes Turbulence's FPS counter: Off, On, and On + frame times.
The expanded view shows game FPS, emulation speed, the active backend, VPS,
and a graph of intervals between new frames. Duplicate presentations do not
inflate the graph's frame count. The preference is saved as `[Video] FPSCounter`
in `user/Config/ReFare.ini` (0, 1, or 2). It defaults to Off.

Android sends each controller snapshot in one JNI call under one state lock,
instead of taking the game input lock separately for all 18 controls. Menu
input blocking, D-pad mapping, analog values and release behavior are retained.

For an explicit diagnostic capture, create `user/pacing-request` in the app's
private files directory. The next 600 presentation events are recorded as
`user/pacing-N.csv`. Captures are opt-in; no continuous trace file is written.
The frame-time overlay reports renderer callback intervals; use Android
SurfaceFlinger presentation timestamps when measuring actual screen delivery.
