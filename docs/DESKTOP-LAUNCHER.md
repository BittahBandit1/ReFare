# Windows and Linux launcher

The Swift pre-game window now has a native SDL3/ImGui counterpart for Windows,
Linux x86-64, and Linux ARM64. The existing SwiftUI macOS app is retained.

From a source checkout, with the platform dependencies in [PORTS.md](PORTS.md):

```powershell
# Windows
python scripts/ports.py launcher --target windows-x86_64
```

```sh
# Linux: chooses x86-64 or ARM64 from the host
python3 scripts/ports.py launcher
```

These commands build and open the GUI. Later, open
`build/ports/<target>/ReFare.exe` on Windows or `build/ports/<target>/ReFare`
on Linux. The `build` and `test` actions now build the launcher too.

## What the window does

- **Choose File…** opens the system's file chooser. Dropping a disc image onto
  the window also works. ISO and GameCube block-map CISO are supported.
- It checks for Crazy Taxi USA (GCTE51), disc 1, revision 0, then converts CISO
  if necessary, extracts files, applies the revision-checked startup patches,
  and recompiles a native game module. Progress and a collapsible log stay
  available while the window remains responsive.
- **Play** appears after successful preparation and on later launches. It starts
  the prepared module directly. The compiler, CMake and Ninja are only needed
  for preparation. Python 3 is still needed by the launcher's service.
- Quitting the game returns to **Play**. A game error opens the log.
- **Copy log** copies the visible log for troubleshooting; long lines can be
  scrolled horizontally.
- **Use a different disc…**, **Show game files**, missing-tool instructions, and
  **Check Again** follow the Swift launcher's flow.

The native GUI uses the SDL and ImGui dependencies already in the project;
Tkinter, Qt, Electron and a browser are not required. It uses the system's
sans-serif font when available and falls back to the runtime's bundled font.

Windows MSVC builds locate installed Visual Studio C++ tools using `vswhere`
and initialize their environment before compilation, so players do not have
to launch the GUI from a developer terminal. A MinGW build instead needs GCC
on PATH. Missing tools are reported; the launcher does not install them.

## Game files and settings

Default library locations:

- Windows: `%LOCALAPPDATA%\ReFare`.
- Linux: `$XDG_DATA_HOME/ReFare`, normally `~/.local/share/ReFare`.

An optional `--support-dir <directory>` argument selects an isolated profile.
The library contains an `installation.json` pointer, private prepared games
under `installs/`, the runtime `user/` directory with saves/settings, and
`launcher.log`. A failed new import removes its incomplete files and preserves
the previous installation. Successful imports preserve saves and existing
graphics settings. Previous successful preparations are retained when choosing
another disc; the launcher does not automatically delete them.

The installation marker checks the host platform, CPU architecture, native
module and prepared DOL hash. An OS lock prevents two launcher windows from
preparing or running the same library at once. To close the launcher during
preparation, wait for preparation to finish; during play, quit the game first.

## Build inputs and verification

`launcher/refare_launcher.cpp` implements the native window. The standard-library
Python service in `launcher/launcher_backend.py` handles preparation and game
processes. CMake stages its scripts, defaults and explicitly selected open-source
module-compilation sources in `refare-data/` beside the executable. It does not
copy a disc, an extraction, generated game code, or a compiled game module.
Keep `refare-data/`, `Sys/` and the companion tools beside the launcher; copying
the GUI executable alone is insufficient. Release dependency packaging is
separate from building this frontend.

Public workflow tests:

```sh
python3 -m unittest discover -s tests -p launcher_test.py
```

`scripts/ports.py test` also builds and runs a windowless rendering smoke test
with SDL's dummy video/software renderer, and runs the workflow tests. These
checks require no game data. Native file choosers and graphics/controller
behavior still require interactive testing on Windows and Linux.

Local verification on 2026-11-04: the Windows x86-64 GUI cross-build succeeds;
the native UI renders on macOS using SDL's software test renderer; disc, ready,
missing-tool and error views were visually checked. Ten workflow tests and
the native GUI-to-Python status probe pass. The copied preparation resources
successfully imported and recompiled the personal CISO, and the Play path
booted the resulting game, advanced 121 frames, stopped normally, and returned
to Ready. Those execution checks used macOS; native Windows and Linux desktop
interaction remains unverified.


Linux adds a **Vulkan driver…** button. Select System driver or a native Linux
ICD JSON (or ZIP containing one and its libraries). The launcher validates
architecture and rejects Android libraries. The choice applies to the next
game process through Vulkan loader variables, without changing OS drivers.
See [Linux ARM64 testing](LINUX-ARM64-TEST.md) for the personal test bundle.
