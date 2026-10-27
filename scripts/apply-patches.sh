#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
apply_patch() {
    repo="$root/$1"
    patch="$root/patches/$2"
    if git -C "$repo" apply --reverse --check "$patch" 2>/dev/null; then
        return
    fi
    git -C "$repo" apply --check "$patch"
    git -C "$repo" apply "$patch"
}
apply_patch lib/ModernGekko moderngekko-path-quoting.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-path-quoting.patch
apply_patch lib/ModernGekko moderngekko-options-runtime.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-options-overlay.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-metal-drawable.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-centered-hud.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-input-gate.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-sprite-edge-clamp.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-gfx-trace.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-macos-focus.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-present-catch-up.patch
apply_patch lib/ModernGekko/vendor/dolphin recompcore-frame-interpolation.patch
