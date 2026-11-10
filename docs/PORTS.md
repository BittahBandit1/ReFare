# Port development

The new desktop entry point is `python scripts/ports.py`. It runs on native
Windows x86-64, Linux x86-64, Linux ARM64 (AArch64), and macOS ARM64 hosts.
It keeps each platform's build, generated module, settings and saves in
`build/ports/<target>/`; existing macOS builds and user profiles are separate.
The C backend is used on every platform. ARM32 is not a target.

These are development build paths, not validated releases. See the verification
notes below before reporting a platform as supported.

For the pre-game graphical interface, use `python scripts/ports.py launcher`.
See [the desktop launcher guide](DESKTOP-LAUNCHER.md) for disc import, Play,
storage locations and prerequisites.

## Windows x86-64

Install Visual Studio 2022 with **Desktop development with C++** and its Windows
SDK, Python 3, Git, CMake and Ninja. Open the **x64 Native Tools Command Prompt
for VS 2022** so the game module can use the same compiler as the runtime.

```powershell
git submodule update --init --recursive
python scripts/ports.py build --target windows-x86_64
python scripts/ports.py run --target windows-x86_64 --disc "D:\Games\Crazy Taxi (USA).iso"
```

GameCube block-map `.ciso` is also supported. Extraction does not invoke Wii
tools. The first run imports the disc; later runs use the extracted directory:

```powershell
python scripts/ports.py run --extracted build/ports/windows-x86_64/imported
```

The default renderer is Vulkan. MSVC builds also support Direct3D 11 with
`--graphics D3D`; MinGW builds use Vulkan or OpenGL. The existing SDL controller detection and keyboard bindings are shared
with macOS. Metal HUD is omitted from the menu on Windows and Linux.

## Linux x86-64 and ARM64

Build natively on a 64-bit Linux installation. Both X11 and Wayland paths are
enabled. Install a Vulkan driver for the GPU, alongside the compiler dependencies.
On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install -y git cmake ninja-build python3 pkg-config g++-14 \
  libasound2-dev libpulse-dev libudev-dev libevdev-dev \
  libx11-dev libxi-dev libxrandr-dev libxext-dev libxfixes-dev \
  libxinerama-dev libxcursor-dev libgl1-mesa-dev libegl1-mesa-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols libdbus-1-dev \
  libusb-1.0-0-dev libcurl4-openssl-dev libssl-dev libbluetooth-dev
export CC=gcc-14 CXX=g++-14
git submodule update --init --recursive
python3 scripts/ports.py build
python3 scripts/ports.py run --disc '/path/to/Crazy Taxi (USA).iso'
```

Host detection chooses `linux-x86_64` or `linux-arm64`. Subsequent runs:

```sh
python3 scripts/ports.py run --extracted build/ports/linux-arm64/imported
# Use linux-x86_64 in that path on an x86-64 machine.
```

Vulkan is the default. Use `--graphics OGL --window-system x11` for OpenGL;
`--window-system wayland` explicitly selects Wayland for Vulkan. ARM64 identifies the
CPU, not GPU capability: compatible graphics drivers and performance still
need testing on each board. A Raspberry Pi is not assumed to be validated.

## Public desktop tests

```sh
python scripts/ports.py test
```

This builds the actual runtime/menu test and runs rendering, input, settings,
exit-confirmation, and synthetic disc-patching tests. No game data is needed.
If the private original DOL is present locally, its revision-specific test
also runs. `.github/workflows/ports.yml` runs native build/test jobs for all
four desktop host types. Adding the workflow does not establish that CI passed;
it must run on GitHub after the changes are published.

The shared build script rejects a target that differs from the host rather
than accidentally executing a foreign recompiler or reusing a macOS module.

## iOS / iPadOS

Three build targets exist: the import-only app, a static module, and an
experimental personal gameplay app. The gameplay app now links for iOS 15;
physical-device gameplay remains unverified. See [iOS gameplay testing](IOS-GAMEPLAY.md).

### Direct disc import

`ports/ios` is a native SwiftUI import app for iPhone/iPad (iOS 15+). It opens
Files, reads ISO/GCM or GameCube block-map CISO, verifies GCTE51 revision 0 and
the original DOL hash, and extracts into Application Support inside the app.
CISO is read directly; it does not create a full temporary ISO. The source is
never changed. Existing imports are preserved and incomplete imports removed.
No executable compilation, unsigned library loading, or JIT is performed.

On a Mac with Xcode:

```sh
python3 scripts/ios.py import-app
# Build and package an unsigned IPA for your sideloading tool:
python3 scripts/ios.py import-app --ipa
# For the ARM64 simulator:
python3 scripts/ios.py import-app --simulator
# To sign with your configured Apple development team:
python3 scripts/ios.py import-app --team YOUR_TEAM_ID
```

Unsigned output is in
`build/ports/ios-import-app-iphoneos/Release-iphoneos/ReFareImport.app`.
It cannot be installed until signed. This import-only build does not include the runtime or generated game code.
The separate gameplay target adds Play and links your personal module.

The testing IPA is written to
`build/release/ReFare-Import-<version>-iOS15-arm64-unsigned.ipa`. It contains the import
prototype only and must be signed by the sideloading tool before installation.

Version 0.1.1 (build 2) registers CISO/GCM file types, uses an unrestricted item
picker with content validation, and coordinates document-provider reads while
holding security-scoped access. Selected filenames appear immediately during
import. It also accepts Open in ReFare from Files. The minimum remains iOS 15.

### No-JIT game module

The module template now accepts `RECOMPCORE_MODULE_STATIC=ON`. The iOS helper
builds a private ARM64 static library from the user's own generated C output:

```sh
python3 scripts/ios.py module --generated /path/to/personal/dolrecomp-output/generated
```

The directory must contain `main.dol`, `generated.h`, `generated_smc.txt`, and
`chunks/*.c` from the C backend. The resulting library is local game-derived
code and must not be included in public releases. The runtime already offers
an attached-module descriptor path, avoiding `dlopen` once a signed app links
this library. Building the library alone does not connect it to an iOS renderer.

### Gameplay app and execution constraint

The gameplay target integrates a UIKit/Metal surface, native audio, lifecycle
handling, GameController/touch input, sandboxed paths and the static module.
Use your generated C inputs to build the personal IPA:

```sh
python3 scripts/ios.py gameplay-app --generated /path/to/personal/generated --ipa
```

Normal iOS code signing prevents importing and loading a new unsigned native
game module. This target builds the personal module into the app **before
signing/installing**, then imports disc assets through Files. It requires no
JIT at play time. The desktop options overlay and physical-device validation
remain outstanding; see the gameplay guide for controls and diagnostics.

Apple references: [app code signing](https://support.apple.com/guide/security/sec7c917bf14/web)
and [placing code in a bundle](https://developer.apple.com/documentation/bundleresources/placing-content-in-a-bundle).

### Importer tests

The production importer is also compiled on macOS against fabricated ISO/CISO
fixtures, with no game data:

```sh
mkdir -p build/ports
swiftc -parse-as-library ports/ios/DiscImporter.swift tests/ios_import_test.swift \
  -o build/ports/ios-import-test
build/ports/ios-import-test
```

They verify nested extraction, sparse CISO blocks, original DOL preservation,
revision rejection, existing-library protection, path traversal rejection and
cleanup after failure.

## Local verification (2026-11-03)

- macOS: new desktop configuration succeeds; existing native menu/runtime
  integration test passes after the keyboard portability changes.
- Windows x86-64: MinGW cross-build of DolRecomp succeeds. A private Crazy Taxi
  module builds as a PE32+ DLL and exports `staticrecomp_get_module`. The full
  runner, port tool, and menu test executable also cross-build successfully.
  Cubeb's Speex resampler is bundled to avoid a host audio-library dependency.
- iOS: unsigned import app builds for physical ARM64 devices and the ARM64
  simulator. The private generated game module builds as an iOS 16+ ARM64
  static archive with the expected descriptor export.
- The production Swift importer passes synthetic fixture tests. On macOS it
  also imports the personal CISO: all 143 assets and five system files match
  the existing desktop extraction byte-for-byte.
- Public Python preparation/platform tests and the local original-DOL patch
  tests pass.
- All 16 recorded patches apply in order to the published submodule revisions;
  the resulting patched files match the local runtime sources byte-for-byte.
- Windows execution and Linux x86-64 execution remain unverified.
  Linux ARM64 now builds and boots in a native ARM64 Linux VM; see
  [Linux ARM64 testing](LINUX-ARM64-TEST.md). Physical Adreno 650 testing remains outstanding. Native CI has been added but has
  not been run from this local working tree. The iOS gameplay integration was
  subsequently added on 2026-11-08; physical-device execution is still unverified.


Version 0.1.2 (build 3) switches direct disc selection to document-picker copy
mode and adds **Choose Disc Folder…**. The folder fallback lists ISO/GCM/CISO
files inside ReFare, bypassing file-type filtering in Files. It retains the
folder's security-scoped access while importing, rejects directories disguised
as disc files, and shows the build number on screen. The device IPA builds
with iOS 15 minimum; importer and folder-list fixtures pass on macOS.
Physical-device Files interaction still needs confirmation.


The experimental personal iOS gameplay target now builds with Metal, RemoteIO
audio, touch/GameController input and a statically attached game module. See
[iOS gameplay testing](IOS-GAMEPLAY.md). Device execution remains unverified;
the import-only target remains available separately.

## Android ARM64

A native Android host, on-device ISO/CISO importer, Vulkan renderer and AAudio
backend are available. See [Android test build](ANDROID.md) for build and
controller instructions. The APK statically links a personal compiled module
and does not require JIT during play.

## Apple graphics additions

MetalFX spatial upscaling is now available in both macOS and iOS options menus
on supported OS/GPU combinations. iOS also has a separate Metal HUD toggle
(iOS 16+), while keeping the app minimum at iOS 15.
