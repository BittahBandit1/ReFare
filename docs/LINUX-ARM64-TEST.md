# Linux ARM64 / Snapdragon 865 testing

Extract `ReFare-Linux-arm64-SD865-test.tar.gz`, open its folder in a terminal,
and run `./ReFare`. Select your Crazy Taxi USA revision 0 ISO or CISO.
Python 3 is required. The personal test package includes your precompiled game
module, so preparation does not require GCC, CMake, or Ninja on the device.
Keep this personal bundle private; public source snapshots exclude game data
and generated game code.

The target is native ARM64 Linux, including ArmadaOS with Fedora desktop.
The binaries use ARMv8-A with CRC, with glibc 2.39 as their baseline.
The first import defaults to Vulkan, 1× rendering and fullscreen. Existing
settings are preserved. Start with VSync on and interpolation off; use L1
in the game to adjust settings. Saves, settings and logs live under
`~/.local/share/ReFare`.

## Selecting a driver

Use **Vulkan driver…** in the launcher before Play. **System driver** uses
ArmadaOS's configured graphics stack. A custom driver needs a Linux ARM64
Vulkan ICD JSON and its matching Linux shared libraries. A ZIP containing
these files can also be imported. The choice is saved for ReFare and applies
only to its next game process. It does not install or replace OS drivers.

[TurnipZ v0.3.0](https://github.com/AlexSanz0902/turnip-sd865-z/releases/tag/v0.3.0)
is an Android AdrenoTools package. Its library depends on Android components
including libhardware, liblog and libnativewindow. That release cannot load
on native Fedora; a native Linux/DRM build is required. The launcher reports
this incompatibility and preserves the previous working selection.

## Verification

The runtime, launcher, recompiler and port tool build on native ARM64 Ubuntu
24.04. Launcher workflow tests and the native UI smoke tests pass. The
relocated package imports the personal CISO with no compiler tools available,
loads a selected Linux software Vulkan driver, boots the game, advances frames,
and stops normally back to Ready. This verifies the launch and driver-selection
path; Snapdragon 865 performance and Adreno 650 rendering still need device testing.

If launching fails, retain `launcher.log` and `user/Logs` from the data folder.
The exact runtime source snapshot accompanies the test archive as
`ReFare-Linux-arm64-test-source.tar.gz`.
