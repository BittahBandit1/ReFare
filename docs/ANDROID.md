# Android ARM64 personal test port

The Android host uses a Java document picker and bounded ISO/CISO importer,
Vulkan rendering, native AAudio output and Android gamepad events. It links a
personal compiled game module into the APK; gameplay does not require JIT.
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
Generate portable C game code from your personal disc first, then run:

```sh
python3 scripts/android.py --sdk build/android-sdk --generated /path/to/dolrecomp-output/generated
```

Output: `build/release/ReFare-Android-arm64-test.apk`. It is signed with a local
ignored debug keystore for USB installation. The generated module makes this
a private personal build; do not publish the APK containing it. Source and
public runtime resources can be shared independently.

```sh
adb install -r build/release/ReFare-Android-arm64-test.apk
```

Launch ReFare, choose an ISO/CISO using Android's file picker, then Play.
The View gameplay log button shows the most recent runtime diagnostics.

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
