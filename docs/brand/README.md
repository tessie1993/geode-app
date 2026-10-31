# MuzViz brand

The mark is an eye made of sound: spectrum bars (waveform) inside a rippling oscilloscope lid,
around a twisted-hexagon vortex iris (fractal). The palette is the old neon purple, on brand night
`#0E0620`. Chosen on 2026-10-30 from three directions and four hybrids ("H4, all three").

Two sources, each used where it is strongest:

| Where | Source | File |
|---|---|---|
| Launcher icon, themed icon, status-bar icon, splash animation | Vector, `tools/brand_assets.py` | `app/src/main/res/drawable/ic_launcher_*`, `ic_stat_muzviz`, `avd_splash` |
| Vector masters | Vector, `tools/brand_assets.py` | `muzviz-icon.svg` (with background), `muzviz-mark.svg` (transparent), `muzviz-monochrome.svg`, `muzviz-lockup.svg` |
| Play Store icon, 512×512 32-bit PNG | Google Flow render | `muzviz-icon-512.png`, from `flow/icon-art.jpg` |
| Play feature graphic, 1024×500 | Google Flow banner + our outlined wordmark | `muzviz-feature-graphic.svg` → `muzviz-feature-graphic-1024x500.png` |

The app keeps the vector: it stays crisp at 40 px, has a one-colour themed form, and the splash
can animate its parts. The store gets the neon render, which shows off the glow at 512 px.

## Palette

| Name | Hex | Use |
|---|---|---|
| Night | `#0E0620` | Icon background edge, splash background (`@color/brand_night`) |
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
python tools/brand_assets.py
```

Then the PNG exports, with headless Chrome (run from `docs/brand/`):

```bash
chrome --headless=new --hide-scrollbars --allow-file-access-from-files --window-size=1024,500 --screenshot=muzviz-feature-graphic-1024x500.png muzviz-feature-graphic.svg
```

`muzviz-icon-512.png` is `flow/icon-art.jpg` resized to 512 with Lanczos and saved as RGBA.

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
