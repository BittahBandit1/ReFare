# Pre-game branding

All pre-game interfaces use `ReFareLogo.png` centered in a full-width white
header bar, black body text, the sampled sRGB yellow (`#FFCC20`) background,
and the ReFare UI pixel font. This launcher logo is the user's no-background
variant; the app icon continues using `assets/ReFareIcon.png`.
This styling covers the macOS SwiftUI launcher, Windows/Linux SDL launcher,
Android library/import/preparation screen, and iOS importer/builder.

`ReFareUI.ttf` is Handjet Bold (SIL Open Font License 1.1), a fixed
weight-700 instance of the Handjet variable font, renamed "ReFare UI" so the
launchers can load it by that name. `NOTICE.txt` holds its credit and the full
license and ships inside every app. To regenerate it from Handjet's variable
font with fonttools:

```python
from fontTools.varLib import instancer  # wght=700, ELGR/ELSH at defaults
```

ReFare must not ship fonts or artwork taken from the game.

`theme.json` records the color sampled from `assets/ReFareIcon.png`.
