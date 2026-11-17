# On-device iOS builder (experimental)

ReFare Builder imports your Crazy Taxi USA revision 0 ISO/GCM/CISO directly
from Files, generates portable C with DolRecomp, compiles ARM64 iOS machine
code with embedded Clang/LLVM 20.1.8, and links a personal game framework with
LLD. It exports a separate unsigned gameplay IPA. The builder never executes
the generated machine code and does not need JIT.

The builder and gameplay app both target iOS/iPadOS 15.0 minimum. This is a
development workflow, not an App Store distribution or automatic installer.
The exported gameplay app must be signed and installed separately; the
signing tool must sign its embedded `ReFareGame.framework` along with the main
app. Whether this step can be completed without a computer depends on your
signing/install tool and provisioning. No signing credentials, installation
exploits, or signing service are built into ReFare.

## On the phone or tablet

1. Sign and install the generic ReFare Builder IPA with your signing tool.
2. Choose your disc (or its folder) and wait for import verification.
3. Choose **Build Gameplay IPA**. Keep the builder in the foreground. The
   first build is CPU intensive and can take a long time; a single compilation
   worker limits peak memory. Cancel takes effect after the current compiler
   step. Moving the builder into the background requests cancellation.
4. Use **Share Gameplay IPA…**, or retrieve `ReFare-Personal-iOS15.ipa` from
   the builder's Documents folder in Files.
5. Sign and install that IPA as a second app. It contains your personal code
   and imported assets, so you should not have to import the disc again.
   Launch ReFare and choose **Play Crazy Taxi**.

Keep exported personal IPAs private. The generic builder contains the compiler,
open-source runtime/helper code and generic resources, without a personal
compiled module or disc. The gameplay template is bundled in a checked data
archive, then reconstructed only in the export staging folder. The generated
framework is added to its normal `Frameworks` directory before IPA export.

Finished frameworks are cached against the patched DOL and complete builder
toolchain identity. The cached binary's SHA-256 is verified before reuse.
Incomplete staging folders are removed on normal failure/cancellation; a
completed IPA is replaced only after the next export succeeds. **View Build
Log** provides compiler and linker diagnostics. The app does not download or
upload your disc, generated sources, or personal executable.

## Build the generic builder on macOS

Install Xcode, CMake, Ninja and LLVM 20.1.8 with its host `llvm-tblgen` and
`clang-tblgen` tools. The default host tools path is Homebrew's LLVM 20 prefix.

```sh
python3 scripts/ios_builder.py --jobs 6
```

The script downloads the official LLVM 20.1.8 sources and checks their pinned
SHA-256, builds only the embedded compiler/linker libraries and AArch64 backend
for iOS, builds the generic gameplay template, and packages:

`build/release/ReFare-Builder-0.2.5-iOS15-arm64-unsigned.ipa`

After a complete compiler-library build, `--skip-compiler-build` reuses those
libraries. No personal disc or generated game sources are needed to build the
generic builder. An Apple SDK is used locally to build generic runtime/helper
objects; the builder distributes small freestanding C declarations, not a
copy of Apple's SDK. Compiler/runtime license notices are bundled.

Host tests compare generated module tables to the reference implementation,
validate IPA CRCs and executable permissions, reject invalid template paths,
corrupt/truncated templates and symlinks, and check canceled exports preserve
the previous IPA. The native smoke driver exercises actual in-process Clang
code generation and repeated LLD invocation with ARM64 iOS 15 output.
Physical-device compilation, signing/import into a third-party installation
tool, and execution of its exported gameplay app remain required validation.

## Local validation (November 16, 2026)

The generic ARM64 builder IPA builds with an iOS 15.0 minimum and includes its
compiler resources and generic app template. Package inspection found no
personal disc or generated game module in it. The same Swift orchestration
and in-process compiler/linker bridge ran a full personal export on macOS:
56 generated chunks plus module glue compiled, the iOS framework linked, and
the resulting approximately 180 MiB personal IPA passed ZIP CRC checks. Its
framework declares iOS 15.0, exports the module entry point, and depends only
on iOS's libSystem. A second export reused the verified module cache.

All 33 host tests pass, including cache corruption rejection without replacing
the previous IPA. This validates the export pipeline on the host, not its
memory use or speed on a phone. No signing identity is installed on the test
Mac, so installation and execution on the paired physical iOS devices have
not been verified. Both the builder and exported gameplay IPA need signing.
