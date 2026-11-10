# ReFare 0.2.0

## Mobile gameplay

- Android ARM64 port with on-device ISO/CISO import, Vulkan rendering and AAudio sound.
- Retroid D-pad support for both key events and HAT_X/HAT_Y axes, including game and options-menu navigation.
- iOS/iPadOS gameplay build targeting iOS 15+, with direct disc import and no gameplay JIT requirement.
- Mobile touch controls, three-finger options gesture, controller-aware control visibility, landscape gameplay and hidden status bar.
- iOS Metal HUD toggle and MetalFX spatial upscaling on supported iOS 16+ hardware.

## Desktop and graphics

- MetalFX spatial upscaling in the macOS L1/Tab menu on supported macOS 13+ hardware with the Metal backend.
- Cross-platform desktop launcher and build support for Windows x86-64 and Linux x86-64/ARM64.
- Shared resolution, aspect-ratio, centered/stretched HUD, FXAA and exit-confirmation options.

## Downloads and setup

- **ReFare-v0.2.0-macOS-arm64.zip**: extract ReFare.app on an Apple Silicon Mac running macOS 13+, then import your own supported disc. First-time compilation requires Xcode Command Line Tools, CMake and Ninja. The app is ad-hoc signed and not notarized.
- **ReFare-0.2.0-Android-arm64.apk**: install on an ARM64 Vulkan-capable Android 8.0+ device, then import your Crazy Taxi USA (GCTE51), revision 0 disc.
- **ReFare-0.2.0-iOS15-arm64-unsigned.ipa**: sign with your sideloading tool, install on iOS/iPadOS 15+, then import the same game revision.
- **SHA256SUMS.txt**: download checksums.

The Android and iOS gameplay binaries include the precompiled game module. The macOS app builds the module locally from your own disc. They do not include disc images or extracted disc assets; importing your own supported disc is required. Source builds can generate the module from your own copy.

## Validation and current limits

- Android checked on Retroid Pocket Nova (Android 13): import, Arcade gameplay, injected controller navigation/acceleration, hat-axis D-pad, audio, FXAA and clean exit.
- macOS checked on M3 Pro: MetalFX game rendering and options persistence.
- 26 host Python tests pass, alongside native options/MetalFX checks.
- iOS device build targets iOS 15; physical iPhone/iPad gameplay for this build remains unverified.
- Android is experimental: loading/shader-compilation pauses remain; touch driving controls and a custom Android Vulkan driver picker are not included.
- Windows/Linux build support is included; this release does not provide newly verified gameplay binaries for those systems.
