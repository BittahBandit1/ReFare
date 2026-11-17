# ReFare app icon

The user chose `assets/ReFareIcon.png` as the canonical app icon for every
platform and every build, including the iOS builder and exported gameplay app.
Keep using it unless the user explicitly requests a replacement. New build or
packaging paths must include this icon rather than an upstream/default icon.

Derived platform assets are checked in under `assets/icons/`. Regenerate them
on macOS with `swift scripts/generate-icons.swift` after an authorized change
to the canonical artwork. Do not change the artwork while converting formats.

All pre-game/import/build interfaces use the ReFare UI font in
`assets/branding/ReFareUI.ttf` (Handjet Bold, OFL; never a font or artwork taken from the game), black body text, a `#FFCC20` background, and
`assets/branding/ReFareLogo.png` centered in an edge-to-edge white header bar.
The launcher logo is separate from the app icon. Preserve this styling for new pre-game
interfaces. It does not replace the in-game HUD font or game menus.

The selected launcher artwork is the user's plain black wordmark, supplied as
`ReFareIconNoBKGBlack2.png`. Keep its exact artwork, with no red background or
drop shadow. Do not regenerate it or apply the app icon's glow to the launcher.

# No game code or data in releases

Never package a precompiled game module, generated game sources, disc files or
anything traced from the game (fonts, textures, logos) into an app, release or
the repository. Every platform compiles the game on the player's own device
from their own disc.
