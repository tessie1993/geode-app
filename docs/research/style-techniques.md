# Making styles 3D, fractal and musical

How the loose styles were rebuilt (2026-10-31), what the rebuild borrowed from MilkDrop presets,
Shadertoy and TouchDesigner, and how to check a style against real music off-device. Companion to
[music-visual-mapping.md](music-visual-mapping.md) (which sound drives which parameter) and
[camera-layers-research.md](camera-layers-research.md) (the camera rig and the composite layers).

## What the classic MilkDrop presets do

Read from the code of community favourites in the projectM "Cream of the Crop" pack
([github.com/projectM-visualizer/presets-cream-of-the-crop](https://github.com/projectM-visualizer/presets-cream-of-the-crop)),
which the NestDrop team picked out of about 52,000 presets, and Ryan Geiss's own favourites
([geisswerks.com](https://geisswerks.com/about_milkdrop.html)). Reddit has no ranked list; r/milkdrop
threads point at the same packs.

- **Rovastar, "Hyperspace".** The whole tunnel is one per-pixel line: zoom grows with the radius,
  `zoom = 1.01 + rad*0.15*q1`, so the edge rushes past faster than the centre. The speed `q1` is a
  bass-weighted mix of all six levels raised to the 6th power and divided by fps. The power makes
  loud passages race and quiet ones crawl; the division makes it frame-rate independent.
- **Krash's beat code** (in many EoS / Flexi / Phat presets). A beat is the bass envelope coming
  back down from a decaying peak, with a minimum gap scaled by the measured beat interval. A counter
  of beats flips modes every second beat (`flip = 2*mode-1`), so motion alternates direction in
  time with the bars. Our engine does this natively: SND_KICK, the counted SND_KICK2/4/8/16, the
  toggles.
- **Flexi, Geiss and Rovastar, "mindless tokamak".** A centre that wanders on two out-of-step
  sines (`0.6*sin(0.374 t) + 0.4*sin(0.294 t)`) with the picture swirled round it by angle. The same
  two-sine trick drives every colour, so nothing ever repeats.
- **The common grammar.** Every preset is feedback (last frame, zoomed, rotated, warped, decayed),
  plus a few drawn shapes and waves. Per-frame values set the global motion. Per-pixel values make
  it depend on radius and angle: a tunnel zooms more at the edge, a vortex turns more at the centre,
  a flower breathes with `sin(ang*n + t)`. Audio is always relative (bass against its own average),
  always smoothed, and usually split so the bass moves zoom, the mids turn and the treble warps.

Our engine already does each of these, as the table shows.

| MilkDrop idiom | Ours |
|---|---|
| bass vs bass_att | SND_BASS is already the level over its own ~4 s mean (1 typical): a beat is SND_BASS above 1. SND_*_ATT is a FAST envelope, not a slow average. |
| radial zoom tunnel, feedback | composite Layers › Fall through (octave-stacked zoom), and the styles' own endless dives |
| accumulated beat time | the music clocks SND_*_TIME (stand still in silence) |
| beat counter flips | SND_KICK_TOGGLE, counted kicks, SND_ONSET_COUNT / SND_CUT_COUNT |
| wandering centre, two-sine colour | dmtTunnelPath (incommensurate sines), the rig's Lissajous look |

## What the Shadertoy and TouchDesigner material contributes

The 3D side comes from the raymarching canon rather than from feedback. LICENCE: Shadertoy code is
CC BY-NC-SA by default and this app is sold, so the styles use the techniques and none of the code.

- **Signed distance fields and raymarching** (Inigo Quilez, [iquilezles.org](https://iquilezles.org)):
  sphere tracing, smooth min, domain repetition (`p - s*round(p/s)`: one object, infinitely many
  copies), polar repetition with whole fold counts, cosine palettes.
- **Kaleidoscopic IFS** (Knighty, Syntopia): fold onto one octant, sort the axes, scale and pull
  back, a few times. A crystal that is fractal at a cost of three or four iterations. Folding
  coordinates recursively is the cheap IFS ([gam0022 / 0b5vr, TokyoDemoFest 2021](https://scrapbox.io/files/62affedd450a17001dd6de96.pdf)).
- **Volumetric glow:** accumulate `exp(-d*k)` along the march, so near misses glow. This is
  where the neon look comes from.
- **Droste / log-polar zoom** ([Jos Leys](https://www.josleys.com/article_show.php?id=82),
  [TouchDesigner Droste tox](https://derivative.ca/community-post/asset/droste-effect-and-infinite-recursion-tox/74247)):
  `log(z)` makes the zoom periodic, so a picture can zoom forever with a new copy arriving in the
  centre.
- **TouchDesigner feedback tunnel:** noise → feedback → composite max → transform (scale, rotate)
  → blur → level (hue rotate) → back into feedback. Zoom, rotation, hue and decay are the
  parameters bound to audio ([Derivative community](https://derivative.ca/community-post/tutorial/ue4-infinite-tunnel-audio-reactive-loop-unreal-engine-touchdesigner/65160)).

## Ways sound can move the view

The viewer reads these as separate layers, so a style uses several at once, each on its own part of
the music:

- **camera:** travel speed (the rig, driven by a music clock), a banked turn every eighth kick
  (CameraRig, two bars), a dolly punch on the kick, the lens breathing with bass, shake on the snare,
  a cut at a big moment;
- **geometry:** scale pulse on the beat, fold count or fractal offset per section, twist,
  displacement by the spectrum (sndEq), objects spawned on hits and grown in, growth with intensity
  and the build;
- **material:** hue led by the melody (gently: notes change several times a second), emission
  flaring on the kick, sparkles on hats, iridescence sharpened by treble;
- **space:** fog, a light orbiting on a music clock, bloom at the drop;
- **time:** phases on music clocks, so silence is stillness and loud is fast; continuous motion gated
  by SND_PRESENCE.

## Checking a style against real music, off-device

`tools/glslcheck` renders a style headlessly (Chrome over SwiftShader):

```bash
cd tools/glslcheck
python track.py "<song.flac>" out/track/clip.json --from 60 --seconds 6 --fps 12
node check.mjs --shader kaleido --size 180 --height 320 --track out/track/clip.json --audio "<song.flac>" --out out/clips
```

`track.py` is a numpy stand-in for `core/sound` (same kinds and ranges, not the same decimals).
`--track` writes `out/clips/<id>.mp4` with the music and `<id>_sheet.jpg`, eight frames across the
clip. `--snd NAME=value` sets one sound value for a still frame (`--snd SND_KICK_ENV=1` is a kick
landing). SwiftShader proves a style compiles and draws, not that it is fast on a phone;
[DEVICE_CHECKS.md](../DEVICE_CHECKS.md) is still the authority.
