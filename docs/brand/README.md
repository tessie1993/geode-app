# BeatRealms 3D brand

The product was renamed from MuzViz Studio Pro to BeatRealms 3D on 2026-11-11. The old MuzViz
lockup and feature graphic are gone; the files below carry the new name. (`ic_stat_muzviz.xml` is
a resource id in the app and keeps its name.)

The mark is an eye made of sound: spectrum bars (waveform) inside a rippling oscilloscope lid,
around a twisted-hexagon vortex iris (fractal). The palette is the old neon purple, on brand night
`#0E0620`. Chosen on 2026-10-30 from three directions and four hybrids ("H4, all three").

Since 2026-11-01 the neon render (`flow/icon-neon.jpg`, the owner's art) is the face of the app.
The vector stays wherever a single flat colour is needed:

| Where | Source | File |
|---|---|---|
| Launcher icon foreground (all densities), system splash, BootIntro's first frame | Neon render, `tools/brand_raster.sh` | `res/drawable-*dpi/ic_launcher_foreground.webp`, `res/drawable-nodpi/splash_mark.webp` |
| Splash video (silent, 4.5 s cut of the owner's 10 s clip) | `tools/brand_raster.sh <source.mp4>` | `res/raw/splash_intro.mp4` |
| Play Store icon, 512×512 32-bit PNG | Neon render | `beatrealms-icon-512.png` |
| Launcher background layer (hidden under the opaque foreground), themed icon, status-bar icon | Vector, `tools/brand_assets.py` | `res/drawable/ic_launcher_background.xml`, `ic_launcher_monochrome.xml`, `ic_stat_muzviz.xml` |
| Wordmark on the system splash (its branding image, 200×80 dp) and in the app (`ui/theme/Wordmark`, BootIntro) | Outlined Pacifico and Orbitron, `tools/wordmark.py` | `res/drawable/splash_wordmark.xml` |
| Vector masters | Vector, `tools/brand_assets.py` | `beatrealms-icon.svg`, `beatrealms-mark.svg`, `beatrealms-monochrome.svg` |
| Play feature graphic, 1024×500, no alpha | The neon eye beside the wordmark (read from `splash_wordmark.xml`, gradients and rims included) over an indigo ground, `tools/feature_graphic.py` | `beatrealms-feature-graphic.svg` → `beatrealms-feature-graphic-1024x500.png` |

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

The wordmark (since 2026-11-11, after the owner's glass-script inspiration picture) is
"BeatRealms" in Pacifico, a connected brush script, filled cyan `#4FD8FF` to violet `#B06CFF`
with a white sheen on its top half, a light rim and a deep violet extrusion, over a squared
"3d" in Orbitron Black, banded chrome. Both fonts are SIL OFL 1.1; only their outlines ship, as
one vector drawable (no blur, so the glass is gradients and a rim). The system splash can draw no
text, so that vector is its branding image, and `ui/theme/Wordmark` draws the same vector in the
app, so the two always match.

The old lockup's wordmark is Montserrat Regular (SIL Open Font License 1.1), outlined to paths and
emboldened with a matching stroke, so no font ships and none needs to be installed.

## Regenerating

```bash
python tools/brand_assets.py      # vectors
tools/brand_raster.sh [clip.mp4]   # icon, splash art, Play icon; with a clip, the splash video
python3 tools/feature_graphic.py   # the feature graphic's SVG
```

Then the PNG, at exactly 1024×500 and device scale 1, with Playwright's Chromium (Node, from the
repo root; Play wants no alpha, and a screenshot of an opaque page has none):

```bash
node -e "
const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1024, height: 500 }, deviceScaleFactor: 1 });
  await p.goto('file://' + require('path').resolve('docs/brand/beatrealms-feature-graphic.svg'));
  await p.screenshot({ path: 'docs/brand/beatrealms-feature-graphic-1024x500.png' });
  await b.close();
})();"
```

The ground is indigo, not the app's black: Play asks feature graphics to avoid pure black,
white and dark grey, which blend into the store. Everything that matters sits in the middle
80 %. Rerun both steps whenever `tools/wordmark.py` changes the wordmark. Alt text for the Console: "The BeatRealms 3D eye logo, made of sound waves, over a
spectrum of light".

`beatrealms-icon-512.png` is `flow/icon-neon.jpg` resized to 512 and saved as RGBA by `tools/brand_raster.sh`. `flow/icon-art.jpg` is the earlier Flow render, kept for the record.

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
