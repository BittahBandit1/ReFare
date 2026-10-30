#!/bin/bash
# Packages ReFare.app for the releases page (macOS arm64). Run on a Mac after
# `make tools`:
#   bash scripts/make-app.sh
#
# The app holds only ReFare's own tools: the SwiftUI launcher, the runtime,
# the recompiler, the open-source module sources the recompile needs, and the
# Homebrew libraries the runtime links. It contains no game code, data or
# assets: the launcher builds those on the player's Mac from their own disc.
# The result is ad-hoc signed (no Apple Developer account) and not notarized.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
MG=$ROOT/lib/ModernGekko
BUILD=$MG/build
DOLPHIN=$MG/vendor/dolphin
OUT=$ROOT/build/release
APP=$OUT/ReFare.app
VERSION=${VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}

for tool in moderngekko-run moderngekko-port dolrecomp; do
  [ -x "$BUILD/$tool" ] || { echo "Missing $BUILD/$tool; run 'make tools' first." >&2; exit 1; }
done
[ -d "$BUILD/Sys" ] || { echo "Missing $BUILD/Sys; run 'make tools' first." >&2; exit 1; }
for cmd in swiftc otool install_name_tool codesign ditto; do
  command -v "$cmd" >/dev/null || { echo "Missing $cmd (Xcode Command Line Tools)." >&2; exit 1; }
done

echo "Packaging ReFare $VERSION"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
CONTENTS=$APP/Contents

# Launcher.
swiftc -O -parse-as-library -target arm64-apple-macos13 \
  "$ROOT/launcher/ReFareLauncher.swift" -o "$CONTENTS/MacOS/ReFare"

# Runtime and recompiler. moderngekko-port finds the other two next to itself.
for tool in moderngekko-run moderngekko-port dolrecomp; do
  cp "$BUILD/$tool" "$CONTENTS/MacOS/$tool"
  chmod 755 "$CONTENTS/MacOS/$tool"
done

# Dolphin's Sys data (shaders, fonts, game settings); found at
# <bundle>/Contents/Resources/Sys on macOS.
cp -R "$BUILD/Sys" "$CONTENTS/Resources/Sys"

# Module sources moderngekko-port compiles against (MODERNGEKKO_SOURCE_DIR).
# Only files the dolphin repository tracks (plus ReFare's patch additions) are
# copied, never build output.
TOOLCHAIN=$CONTENTS/Resources/toolchain/vendor/dolphin
mkdir -p "$TOOLCHAIN"
(cd "$DOLPHIN" && git ls-files -z --cached --others --exclude-standard -- \
    module-template GXRuntime/CMakeLists.txt GXRuntime/LICENSE GXRuntime/THIRD_PARTY.md \
    GXRuntime/include GXRuntime/src GXRuntime/backends GXRuntime/graphics \
    Source/Core/Core/PowerPC/StaticRecomp Data/Sys/GameSettings/GCTE51.ini \
  | xargs -0 tar cf -) | (cd "$TOOLCHAIN" && tar xf -)

# ReFare's startup patch script and defaults.
REFARE=$CONTENTS/Resources/refare
mkdir -p "$REFARE/scripts" "$REFARE/config"
cp "$ROOT/scripts/prepare-game.py" "$REFARE/scripts/"
cp "$ROOT/config/startup-patches.json" "$ROOT/config/GFX.ini" "$REFARE/config/"
cp "$ROOT/LICENSE" "$CONTENTS/Resources/LICENSE"

cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>ReFare</string>
  <key>CFBundleDisplayName</key><string>ReFare</string>
  <key>CFBundleIdentifier</key><string>io.github.bittahbandit1.refare</string>
  <key>CFBundleExecutable</key><string>ReFare</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION#v}</string>
  <key>CFBundleVersion</key><string>${VERSION#v}</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
  <key>NSHumanReadableCopyright</key><string>ReFare is GPL-3.0 software. Not affiliated with SEGA or Nintendo. Contains no game data.</string>
</dict>
</plist>
PLIST

# Copy every non-system library the tools link into Frameworks and point the
# tools at those copies, so players don't need Homebrew's libraries.
FRAMEWORKS=$CONTENTS/Frameworks
resolve() {  # resolve <dependency> <file that references it>
  local dep=$1 from=$2 name
  case "$dep" in
    @rpath/*|@loader_path/*|@executable_path/*)
      name=${dep##*/}
      for dir in "$(dirname "$from")" /opt/homebrew/lib /usr/local/lib; do
        [ -f "$dir/$name" ] && { echo "$dir/$name"; return; }
      done
      echo "Can't find $dep (needed by $from)" >&2; return 1 ;;
    *) echo "$dep" ;;
  esac
}
bundle_deps() {  # bundle_deps <binary to fix> <where it came from>
  local file=$1 origin=$2 dep src name
  otool -L "$file" | tail -n +2 | awk '{print $1}' | while read -r dep; do
    case "$dep" in /System/*|/usr/lib/*) continue ;; esac
    name=${dep##*/}
    [ "$name" = "$(basename "$file")" ] && continue   # the library's own id
    if [ ! -f "$FRAMEWORKS/$name" ]; then
      src=$(resolve "$dep" "$origin")
      cp -L "$src" "$FRAMEWORKS/$name"
      chmod 644 "$FRAMEWORKS/$name"
      install_name_tool -id "@rpath/$name" "$FRAMEWORKS/$name" 2>/dev/null
      bundle_deps "$FRAMEWORKS/$name" "$(cd "$(dirname "$src")" && pwd -P)/$(basename "$src")"
    fi
    [ "$dep" = "@rpath/$name" ] || install_name_tool -change "$dep" "@rpath/$name" "$file" 2>/dev/null
  done
}
drop_rpaths() {
  otool -l "$1" | awk '/cmd LC_RPATH/{getline; getline; print $2}' | while read -r rp; do
    install_name_tool -delete_rpath "$rp" "$1" 2>/dev/null || true
  done
}
for tool in moderngekko-run moderngekko-port dolrecomp ReFare; do
  bundle_deps "$CONTENTS/MacOS/$tool" "$BUILD/$tool"
  drop_rpaths "$CONTENTS/MacOS/$tool"
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$CONTENTS/MacOS/$tool" 2>/dev/null
done
for lib in "$FRAMEWORKS"/*.dylib; do
  [ -e "$lib" ] || continue
  drop_rpaths "$lib"
  install_name_tool -add_rpath "@loader_path" "$lib" 2>/dev/null
done

# Nothing may still point at Homebrew.
if otool -L "$CONTENTS"/MacOS/* "$FRAMEWORKS"/*.dylib 2>/dev/null | grep -E '/opt/homebrew|/usr/local' ; then
  echo "A bundled binary still links a library outside the app." >&2; exit 1
fi

# The release must not contain anything from a game disc.
bad=$(find "$APP" -type f \( -iname '*.iso' -o -iname '*.ciso' -o -iname '*.gcm' -o -iname '*.rvz' \
  -o -iname '*.dol' -o -iname '*.all' -o -iname '*.afs' -o -iname '*.adx' -o -iname '*.sfd' \
  -o -iname '*.thp' -o -iname '*recomp*.dylib' -o -iname 'GCTE51.c' -o -iname 'boot.bin' \
  -o -iname 'bi2.bin' -o -iname 'apploader.img' -o -iname 'fst.bin' \) -print)
if [ -n "$bad" ]; then
  echo "Refusing to package game files:" >&2; echo "$bad" >&2; exit 1
fi
for dir in games extracted prepared modules generated; do
  if find "$APP" -type d -name "$dir" | grep -q .; then
    echo "Refusing to package a '$dir' directory." >&2; exit 1
  fi
done

# Ad-hoc signatures (inside out): Apple silicon refuses to run unsigned code.
for lib in "$FRAMEWORKS"/*.dylib; do
  [ -e "$lib" ] && codesign --force --sign - --timestamp=none "$lib"
done
for tool in moderngekko-run moderngekko-port dolrecomp ReFare; do
  codesign --force --sign - --timestamp=none "$CONTENTS/MacOS/$tool"
done
codesign --force --sign - --timestamp=none "$APP"
codesign --verify --strict --verbose=1 "$APP"

ZIP=$OUT/ReFare-$VERSION-macOS-arm64.zip
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
du -sh "$APP" "$ZIP"
echo "Done: $ZIP"
