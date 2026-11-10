# iOS gameplay testing

The personal gameplay target adds **Play Crazy Taxi** to the working importer.
It keeps the same bundle identifier, so sideloading over the importer should
preserve the imported disc. If needed, use **Choose Disc Folder…** to import
again. This is an experimental device build; a successful build is not a
verified gameplay session on an iPhone or iPad.

The app links the personal DolRecomp module into its signed executable before
installation. Importing supplies disc assets, not executable code. JIT is not
required on the device: the CPU uses StaticRecomp with interpreter fallback;
the fallback JIT is disabled, DSP uses HLE, and vertex conversion uses the
portable implementation. Large JIT/fastmem virtual address mappings are disabled.
Metal still compiles GPU shaders through Apple's normal graphics API.

The host uses a UIKit CAMetalLayer, native RemoteIO stereo audio,
GameController input and mobile-style touch controls. Left/right arrows steer;
small up/down arrows navigate menus. Hold **D** to select Drive and accelerate,
**R** to select Reverse and accelerate, or **BRAKE** for the left trigger.
**START** remains available at the top. Controls accept simultaneous touches,
release on interruption, and adapt to portrait/landscape safe areas.

A simultaneous **three-finger tap** opens or closes the shared options menu.
External controllers can use L1, D-pad, A and B. Touch the menu arrows to change
resolution, aspect ratio, centered/stretched HUD, FXAA and interpolation; swipe
vertically to scroll. Changes apply live and persist. Metal HUD is available on iOS 16+; disabling display sync and rebinding a desktop key
are omitted on iOS. **Exit The Game** asks for confirmation, with No on the left
and Yes on the right. Game input is blocked while the options menu is open.

Backgrounding and audio interruptions pause the game. Existing saves live in
the application support ReFare/user directory alongside gameplay.log and
Dolphin's Logs. **View Gameplay Log** in the importer shows the latest startup
log and can copy it for troubleshooting.

The device app and binary target iOS 15.0 minimum. The IPA must be signed by
your sideloading tool. The 0.2.0 gameplay IPA includes
game-derived native code. No disc image or extracted game assets are bundled.

Build with your own generated C module inputs:

```sh
python3 scripts/ios.py gameplay-app --generated /absolute/path/to/generated --ipa
```

The generated inputs must match the startup-patched Crazy Taxi USA revision 0
DOL. Import validation checks the original disc; Play applies the recorded,
expected-word startup patches to its extracted DOL. The module descriptor
and CPU ABI are validated before boot.

Local verification: the ARM64 iOS gameplay app links, includes the static
module export and Metal resources, declares iOS 15.0 in the Mach-O and plist,
and contains no host-library paths or game assets. Public ISO/CISO/GCM
import fixtures and patch reproducibility are checked separately. There is
no installed iOS simulator runtime or connected iOS test device here;
on-device rendering, audio, performance and controls remain unverified.

Version 0.2.1 (build 5) fixes startup manifest packaging: CMake translates the
canonical JSON description into the native loader's sorted CSV format. IPA
packaging validates this resource. The regression test invokes the native
loader against a fabricated DOL, including repeat launch and rejection without
partial writes. The exact packaged resource also patches a copy of the personal
original DOL to the expected compiled-module hash, with no changes on reapply.

Version 0.3.0 (build 6) adds the mobile-style controls and three-finger options
gesture. The pure touch-layout/input fixture checks safe areas, distinct hit
targets and simultaneous actions on phone/tablet sizes. The existing native
menu regression and public tests pass; the device IPA builds. Actual iOS
gesture recognition and touch interaction still need physical-device testing.

Version 0.3.1 (build 7) restores the top Exit button. It appears initially,
fades after three seconds without touch activity, and reappears on touch.
Touch begins, moves and releases restart the timeout. Startup errors retain
a permanent Back button; Exit requests a clean stop back to the importer.

Version 0.3.2 (build 8) hides the system status bar for the entire gameplay
cover, including touch interactions and the options menu. The importer keeps
its normal status bar. The device app builds; visibility needs device confirmation.

Version 0.3.3 (build 9) restricts iPhone/iPad orientations to landscape left
and landscape right. The app requests full-screen operation on iPad and the
game waits for landscape dimensions before starting its rendering surface.
The IPA's orientation declarations are verified; device rotation needs confirmation.

Version 0.3.4 (build 10) automatically hides the mobile driving controls when
a compatible extended gamepad is connected, including at launch, and restores
them on disconnect. Touch state is cleared on each transition. The transparent
touch surface remains active for the three-finger menu gesture, touch-operated
settings and revealing Exit. The app builds; controller hot-plug behavior
still needs a device check.

## MetalFX spatial upscaling (0.4.0 / build 11)

The triple-tap options menu includes **MetalFX spatial upscaling**, off by
default. On supported hardware, turn it on to upscale the cropped game image
to its final display rectangle. The internal resolution setting is preserved.
MetalFX runs before final post-processing and the options overlay; UIKit touch
controls also remain at display resolution. FXAA can be used independently.
The choice persists in Config/GFX.ini under Enhancements/MetalFXSpatial.

MetalFX requires iOS 16+ and a GPU accepted by Apple's supportsDevice query.
The app still targets iOS 15: MetalFX is weak-linked and all API calls are
availability-guarded. Unsupported devices show Unavailable and use normal
scaling. If scaler/texture creation fails, presentation falls back to the
normal path and records a MetalFX message in gameplay.log. Scaling is bypassed
when the image already meets or exceeds the target dimensions.

The exact scaler implementation passed a real GPU pixel-output test on the
M3 Pro, including crop bounds, nonuniform widescreen scaling, resource reuse,
resize and invalid/downscale rejection. The iOS menu code also passes a host
regression that changes/saves On and Off and rejects unsupported hardware.
The iOS device IPA links with LC_LOAD_WEAK_DYLIB for MetalFX and a 15.0 minimum.
iPhone/iPad rendering and performance still need device testing.

GPU regression on a Mac with MetalFX support:

```sh
xcrun clang++ -std=c++20 -fno-objc-arc \
  -I lib/ModernGekko/vendor/dolphin/Source/Core/VideoBackends/Metal \
  tests/ios_metalfx_test.mm \
  lib/ModernGekko/vendor/dolphin/Source/Core/VideoBackends/Metal/MTLFXScaler.mm \
  -framework Metal -framework Foundation -weak_framework MetalFX \
  -o build/ports/ios-metalfx-test
build/ports/ios-metalfx-test
```

## Metal HUD (0.4.1 / build 12)

The triple-tap menu now has a separate Metal HUD toggle. It applies directly to
the game CAMetalLayer and persists independently of MetalFX. iOS 16+ is required
for the HUD; the app retains its iOS 15 minimum. The device build and host menu
persistence checks passed; visual verification on an iPhone/iPad is still pending.
