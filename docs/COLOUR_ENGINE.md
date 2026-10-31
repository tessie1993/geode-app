# The colour engine: how colour follows the music

Every surface (the screen, exports, the live wallpaper, the dream) draws through the native renderer, and
the renderer colours every style through one function, `pal()` in `lib_palette.glsl`, then one grade,
`grade()` in `lib_scene_grade.glsl`. The colour engine (`core/viz/colour/`) sits behind both. Each frame
it reads the sound engine's frame and its harmony stage, and it writes the palette that `pal()` returns.
So a style keeps its own look, and the music moves the colours away from that look and back.

This file records the mapping and the reasons for it. The research behind it is summarised at the end,
with sources.

## What it hears

| Musical idea | Where it comes from | Kind |
|---|---|---|
| **Tonal centre** (the key, without a label) | `GeodeHarmony.fifths`: the song's ~10 s pitch-class profile scored against all 24 Krumhansl-Kessler key profiles, blended by fit, as an angle on the circle of fifths | glides, ~1.5 s |
| **Key clarity** | `GeodeHarmony.clarity`: how clearly one key signature wins | 0..1, ~1 s |
| **Major / minor** | `GeodeHarmony.mode`: the major keys' share of that blend | 0..1, ~2 s |
| **The chord** | `GeodeHarmony.chordFifths`, `chordStrength`, from a ~0.6 s profile | glides |
| **Harmonic tension** | `GeodeHarmony.tension`: the chord against the home key's blended profile, plus semitone and tritone roughness | 0..1, up 0.25 s, down 0.8 s |
| **Chord change** | `GeodeHarmony.change` (trigger), `motion` (changes per second) | event |
| **Melody** | `pitchClass`, `pitchHeight`, `pitchConfidence`, `noteChange` (sound frame) | per note |
| **Timbre** | `centroid` (dark to bright), `flatness` (tonal to noisy) | continuous |
| **Energy** | `level`, `intensity`, `energyTrend`, `loudness` | continuous |
| **Hits** | `kickEnv`, `onsetEnv` | envelopes |
| **Sections** | `hardCut` (a big moment), `surge` (a drop), `returned` (after a break) | events |

Why the key has no label: the sound engine never commits to a guess it can get wrong. That rule came
from the tempo decision in `docs/research/music-visual-mapping.md`, and a key is the same kind of
guess. Relative keys (C major and A minor) and neighbouring keys (C and G) are the commonest confusion in
key finding. A colour that jumped between them every few seconds would show the guess, not the music.
Instead the harmony stage blends all 24 key scores:

- a clear key lands exactly on its hue;
- an ambiguous passage sits between its likely keys, with low clarity;
- a modulation glides round the wheel the short way.

Relative major and minor share an angle, because they share a key signature. Mode is a separate value.

## The mapping

Colour is worked out in **OKLab / OKLCH** (Ottosson), so lightness and chroma stay put while hue turns.
A cosine or HSV rotation pumps brightness: yellow is far lighter than blue at the same saturation.

| Output | Driven by | Mapping | Smoothing |
|---|---|---|---|
| **Home hue** (rotation of the whole palette) | the tonal centre on the circle of fifths | fifths angle × 360° (C 0°, G 30°, D 60° …), weighted by clarity × Follow. Keys a fifth apart share six of seven notes, so they get neighbouring hues, and a tritone away is the complementary hue (Scriabin's ordering). | the stage's own glide (~1.5 s) |
| **Mode tint** | major / minor | minor: hue −20° toward blue, lightness −0.05, chroma ×0.85; major: hue +12° toward gold, lightness +0.03 (Palmer et al. 2013) | ~2 s |
| **Hue drift inside the palette** | the chosen *Hue driver*: melody pitch class (default), the chord, timbre or the key alone | moves the read position along the palette by up to ±⅙, scaled by pitch confidence | 0.4 s, eased round the wheel |
| **Tension** | `tension` × clarity | pulls alternate stops toward the complement of the home hue: analogous at 0, split-complementary (±150°) around 0.5, a full complementary accent at 1 (Itten's contrast of hue) | rise 0.25 s, fall 0.8 s |
| **Chroma (saturation)** | arousal = level, onset density, intensity | chroma ×0.7 quiet … ×1.25 loud, clamped to the sRGB gamut (Whiteford et al. 2018: loud and punchy read more saturated) | up 0.1 s, down 0.5 s |
| **Lightness** | melody height and timbre brightness (Ward et al. 2006; Anikin and Johansson 2019); not loudness | mid stops ±0.06 L | 0.3 s |
| **Contrast** | intensity (section energy) | the lightness span between the darkest and lightest stop, ×0.85 … ×1.15 | ~1 s |
| **Highlight colour** | timbre (centroid) and melody note | the palette's lightest stop, turned toward the melody note's place on the fifths wheel; `grade()` tints the brightest pixels with it | 0.15 s |
| **Hit bloom** | `kickEnv`, `onsetEnv` | +L and +chroma on the highlight, under the flash safety clamp | the envelope |
| **Palette changes** | a section change (`hardCut`, `surge`, `returned`), at least 12 s apart | crossfades in OKLab over 2.5 s to the next palette in the song's walk, which never repeats the last one | 2.5 s |
| **Variety** | the user's Variety control | 0: stays on the style's own palette and only bends it; 1: walks the whole mood group; 2: walks the whole library | n/a |

Every input is a smoothed value or an event the sound engine already makes frame-locked. Each smoothing
step scales by dt. So the same audio gives the same colours at 30 or 60 fps, live or in export. The
palette walk is seeded from the track (`setColourSeed`), not from a clock, so a song is coloured the same
way on every run.

## Keeping each style's character

A style's own palette, from its `palette` / `paletteLut` params, is home. The colour engine never replaces
it outright:

- **Follow** (0..1, default 0.6) scales every musical move.
- **Away** is how far `pal()` leans from the style's palette toward the music's palette. It rises with
  section energy and big moments and sinks back in quiet passages. At Follow 0 the style looks exactly as
  it did before this engine.
- The **home hue rotation** and **tension** act on the style's own palette too, so even at Away 0 the
  style's colours still turn with the key.

## Controls (the Colour tab API)

| Control | Values | Default |
|---|---|---|
| Palette | Style's own, or any library palette by id, or Auto (mood picks) | Style's own |
| Follow the music | 0..1 | 0.6 |
| Hue driver | Key, Melody, Chord, Timbre | Melody |
| Variety | 0..2 | 1 |

These are global, like the Macros, and kept in `ColourStore`. They reach the renderer through
`NativeViz.setColour`. Export reads the same store, so a render matches the screen.

## Sources

- Palmer, Schloss, Xu and Prado-León 2013, PNAS: music and colour associations are mediated by emotion.
  Fast or major music went with more saturated, lighter and yellower colours; slow or minor with darker,
  bluer and desaturated ones. https://pmc.ncbi.nlm.nih.gov/articles/PMC3670360
- Whiteford, Schloss, Helwig and Palmer 2018, i-Perception: across genres, loudness and arousal went with
  more saturated and redder colours (rs ≈ .64 to .77); valence with lighter and yellower.
  https://pmc.ncbi.nlm.nih.gov/articles/PMC6240980/
- Anikin and Johansson 2019: high pitch and bright timbre go with lighter and more saturated colours;
  pitch to hue is weak. https://pmc.ncbi.nlm.nih.gov/articles/PMC6407832/
- Ward, Huckstep and Tsakanikos 2006, Cortex 42: synaesthetes and controls both map high pitch to light
  colours.
- Scriabin's clavier à lumières ordered the spectrum by the circle of fifths.
  https://en.wikipedia.org/wiki/Clavier_%C3%A0_lumi%C3%A8res
- Krumhansl and Kessler 1982 key profiles (values as in music21).
- Harte, Sandler and Gasser 2006, tonal centroid and harmonic change.
- Ottosson, OKLab. https://bottosson.github.io/posts/oklab/
- Quilez, cosine palettes. https://iquilezles.org/articles/palettes
- Itten's seven contrasts (complementary contrast for peak tension).
