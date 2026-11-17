# ReFare icon

`ReFareIcon.png` is the user's original 2048×2048 artwork, copied unchanged.
It is the default app icon across macOS, Windows, Linux, Android and iOS,
including both the iOS builder and the gameplay app it exports.

`icons/` contains checked-in PNG sizes, a Windows ICO, a macOS ICNS, and an SDL
BMP. Build hosts do not need an image converter. To regenerate these files on
macOS after changing the canonical image:

```sh
swift scripts/generate-icons.swift
```

The converter preserves the square artwork and removes alpha from derived
images for iOS compatibility. Operating systems apply their usual icon masks.
