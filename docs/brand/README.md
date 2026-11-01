# MuzViz brand

The mark is an eye made of sound: spectrum bars (waveform) inside a rippling oscilloscope lid,
around a twisted-hexagon vortex iris (fractal). The palette is the old neon purple, on brand night
`#0E0620`. Chosen on 2026-10-30 from three directions and four hybrids ("H4, all three").

Since 2026-11-01 the neon render (`flow/icon-neon.jpg`, the owner's art) is the face of the app.
The vector stays wherever a single flat colour is needed:

| Where | Source | File |
|---|---|---|
| Launcher icon foreground (all densities), system splash, BootIntro's first frame | Neon render, `tools/brand_raster.sh` | `res/drawable-*dpi/ic_launcher_foreground.webp`, `res/drawable-nodpi/splash_mark.webp` |
| Splash video (silent, 4.5 s cut of the owner's 10 s clip) | `tools/brand_raster.sh <source.mp4>` | `res/raw/splash_intro.mp4` |
| Play Store icon, 512×512 32-bit PNG | Neon render | `muzviz-icon-512.png` |
| Launcher background layer (hidden under the opaque foreground), themed icon, status-bar icon | Vector, `tools/brand_assets.py` | `res/drawable/ic_launcher_background.xml`, `ic_launcher_monochrome.xml`, `ic_stat_muzviz.xml` |
| Vector masters | Vector, `tools/brand_assets.py` | `muzviz-icon.svg`, `muzviz-mark.svg`, `muzviz-monochrome.svg`, `muzviz-lockup.svg` |
| Play feature graphic, 1024×500 | Google Flow banner + our outlined wordmark | `muzviz-feature-graphic.svg` → `muzviz-feature-graphic-1024x500.png` |

The raster icon puts the render at 75% of the canvas on brand void (`#06050B`, the render's own
background), with a wide feather. The eye then fits the adaptive icon's 72dp visible area and the
splash's 192dp circle, and no edge shows on either. The themed icon (Android 13+) keeps the
vector silhouette: the system tints it one colour, which a photo-like render can't survive.

## Palette

| Name | Hex | Use |
|---|---|---|
| Night | `#0E0620` | App backgrounds (`@color/brand_night`) |
| Void | `#06050B` | Splash, BootIntro and the raster icon's edge (`@color/brand_void`) |
| Night lift | `#1F1145` | Icon background centre |
| Violet | `#7C4DFF` | Bar ends |
| Violet light | `#9C6BFF` | Iris rim |
| Lilac | `#C792EA` | Lids, wordmark start |
| Cyan | `#89DDFF` | Bar centres, outer vortex, wordmark end |
| Blue | `#82AAFF` | Vortex |
| Amber | `#FFCB6B` | Pupil |

The wordmark is Montserrat Regular (SIL Open Font License 1.1), outlined to paths and
emboldened with a matching stroke, so no font ships and none needs to be installed.

## Regenerating

```bash
python tools/brand_assets.py      # vectors
tools/brand_raster.sh [clip.mp4]   # icon, splash art, Play icon; with a clip, the splash video
```

Then the PNG exports, with headless Chrome (run from `docs/brand/`):

```bash
chrome --headless=new --hide-scrollbars --allow-file-access-from-files --window-size=1024,500 --screenshot=muzviz-feature-graphic-1024x500.png muzviz-feature-graphic.svg
```

`muzviz-icon-512.png` is `flow/icon-neon.jpg` resized to 512 and saved as RGBA by `tools/brand_raster.sh`. `flow/icon-art.jpg` is the earlier Flow render, kept for the record.

## Flow prompts

Model `nano-pro`, text-to-image only (this account's Flow composer takes no reference image).

Icon (1:1): *Square app icon artwork for a music visualizer app. Centred, flat front view: a wide
horizontal almond-shaped eye. Its upper and lower eyelids are smooth rippling lilac sine-wave lines
like an oscilloscope trace. Inside the eye, on the left and right of the iris, rows of vertical
rounded audio spectrum bars in violet fading to cyan, tallest near the iris. The iris is a circle
containing a hypnotic twisted spiral of nested hexagon outlines (a fractal vortex), cyan on the
outside turning to blue, violet and lilac towards the centre, with a tiny glowing amber hexagon
pupil. Rendered as luminous neon light tubes with a glassy glow, on a deep indigo-black background
(#0E0620) with a soft violet bloom behind the iris. Premium, crisp, perfectly symmetrical, generous
empty margin around the mark, no text, no letters, no frame, no border, no phone mockup.*

Banner (16:9): *the same eye on the left third, a faint wide audio spectrum of thin vertical bars
sweeping across the whole banner, soft violet bloom, the right two thirds calm, dark and empty for
a title; no text, no letters, no logo, no frame.*
