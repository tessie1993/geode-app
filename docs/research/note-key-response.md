# Note & key reactions

Settings › Behaviour › **Note & key reactions** (free, on by default; Customize › Advanced › Note & key,
which tunes it per style, is Pro; Customize › Look › Note blooms, which picks the bloom's look, is free). The styles
answer more than the beat: the notes, the chords, the key, the mood, how the music is played and how
hard. Everything is real time: it reads the sound frame just heard and never anticipates.

Code: `core/viz/NoteResponse` (the decisions, pure C++), `core/viz/NoteBloomPass` with
`note_bloom_frag.glsl` and `lib_bloom.glsl` (the flowers), 
`data/NoteResponseStore` (free, on by default), `ui/NoteResponseSettings`, Customize › Advanced › Note & key and Look's note blooms. Tests:
`core/tests/note_response_test.cpp` (CI, "Native host tests"),
`NoteResponseStoreTest`.

## What the owner asked for

Gathered by interview; each line is a requirement this layer meets, not a fixed event-to-effect wire.

- Notes: pitch → colour, varied and multicolour across the picture (a melody paints a spread of
  colours, not one hue); pitch height → shape (forms open and stretch up for high notes, pull in for low
  ones); each new note blooms its own element; intervals → motion (leaps swing and twist, steps glide).
- The bloom: a detailed, layered, paradoxical fractal flower (petals that contain the flower again,
  Escher-like nested rotation, translucent iridescent neon petals, dense filigree), growing and fading
  with its note. Original code.
- Key and mood: the key sets the palette family; major warm and bright, minor cool and deep; harmonic
  tension distorts, resolution smooths it back; a chord change shifts the scene.
- Drums: their weight depends on the input, measured continuously.
- A switch, persisted, free and on for new installs; off is exactly today's picture.
- Variation (second round): tint strength and bloom density are not fixed amounts but change, truly
  at random per play, and again at every section; the result is judged on variation, harmony, the
  qualities of good trippy visuals, whether the perspective changes and whether the viewer feels motion.
- It lives inside the auto-director: the director keeps the section-level palette and camera, this
  layer adds note- and chord-level life within them.

## Where it sits in the frame

```
sound frame ─► Director.step (feeling, look) ─► NoteResponse.step (reads the frame, the feeling, the look's hue)
displayed params ─► Director.apply ─► NoteResponse.apply ─► macros ─► LFOs ─► ADSR ─► reactions
                ─► macro routes ─► director routes ─► MotionField ─► safety::apply
composite ─► NoteBloomPass (screen) ─► the user's overlay
```

The user's own settings, LFOs, the motion field and the safety clamp all act after this layer, so they
win; the flash and motion limits cap whatever it adds. The director's emotion is stepped whether or not
the director drives, so this layer always has a mood to read.

## Notes

**Colour.** A note's hue is its pitch class against the key's tonic on the circle of fifths, times
0.85 of the wheel per twelve fifths, around an anchor hue. Harmonic neighbours are colour neighbours:
the tonic is the anchor, the dominant one step one way, the subdominant one step the other. A diatonic
melody spans about half the wheel; chromatic notes reach the far side. Octave adds a small lift (±0.03).
The last six notes' hues are a ring (`uNoteHues`), newest first; before the first note it is a spread
around the anchor, so it is never one colour.

**Anchor.** With the director off, the key: its place on the circle of fifths (`keyHue`) pulled toward
amber for major and deep blue for minor by up to 55 % of the key's clarity (Palmer et al. 2013:
major and happy music is matched to lighter, warmer, yellower colours; minor and sad to darker, bluer
ones). With the director on, its look's hue, weighted by how much it drives: the notes then spread
around the director's palette instead of starting a palette of their own.

**Shape.** `k3 TONE` follows the melody's height (pitch height trusted as far as the pitch confidence,
plus the strongest partial). On shared params it opens `morph` for high notes and closes it for low ones
(±0.15). On the blooms: a high note's flower stretches up (×1.3) and sits higher in the frame; a low
note's squats (×0.8) and sits lower.

**Intervals.** A leap (a fourth or more) kicks a damped spring (0.45 Hz, damping 0.35) in the leap's
direction, harder the wider it is: `twist` and `rotation` swing and settle in about two seconds, and
every bloom turns with it. A step adds to a leaky glide (1.5 s) that sways the picture. `k5 SPACE`
hears both.

## Blooms

One flower per note onset (`noteChange` or `reattack` with a pitch), never on a drum alone. Up to eight
live; when full, the faintest gives its place. The previous note's flower lets go when a new one starts
(so they overlap while one fades and the next grows), when the pitch is lost for 120 ms, or after four
seconds held. Release time follows the playing: 2 s for held notes down to 0.6 s for fast runs.

Each bloom gets: position (across by the fifths around the key's centre, turned by the chord layout; up by
pitch height; a seeded jitter), radius 0.1–0.18 of the short side, hue, stretch, petals (5–10: more
with harmonic tension, one more in minor, moved by the chord's fold shift), recursion depth (2–4: deeper
with tension and a confident pitch), spin and seed. All draws are splitmix64 keyed by the play's seed
(the director's) and the note count: an export of the same play draws the same flowers in the same
places.

**The flower (`lib_bloom`).** In the bloom's unit disc:

- A back whorl: larger petals half a petal round, faint, in the previous note's colour, with lace.
- A heart: a logarithmic spiral rosette, the petals winding inward forever.
- Up to four levels of the front whorl. Each folds the point into one petal's sector (a mod, no loop
  over petals), draws the petal (an ellipse from the heart outward: thin fill, hot rim, contour-ring
  filigree, a vein), then maps the petal's own ellipse back onto the unit disc and draws the flower
  again, clipped to the petal. Each level turns against the one around it, a little faster (Escher's
  nested rotation), and appears a moment after the one around it as the flower opens.
- Iridescence: a thin-film sweep of the hue with the distance to the petal edge, the radius and the
  level, between the note's colour and earlier notes' colours.
- Tension frays the edges; resolution smooths them.

**Safety.** The pass screens the flowers over the frame (`ONE, ONE_MINUS_SRC_COLOR`), which only
lightens and never passes white; the output is capped at 0.8 and scaled by 0.9. A bloom takes 160 ms to
reach full brightness (no rising edge sharp enough to count as a flash), each is a small patch of the
frame, and past three new blooms a second each new one is dimmer in proportion (VisualSafety's
three-a-second budget, by the square root of the excess), so a fast run shimmers rather than strobes.

## Key, mood, harmony

- **Temperature and brightness**: major warms (+0.25 × clarity), minor cools; a happy feeling brightens
  up to 6 %. Only where the director is not deciding the palette (scaled by 1 − its weight).
- **Tension → distortion**: 60 % the chord's dissonance, 40 % the feeling's tension, eased up in a
  quarter second and back down over 0.9 s (resolution). It adds `warp` (up to +0.35) and chromatic split
  (+0.22), and frays the blooms' edges. Never `glitch`: a composite glitch on a chord reads as a flash,
  not a bend (and #216 retires the field).
- **Chord change → scene shift**: on `harmonicChange`, at most once per 1.5 s: the fold count
  (`symmetry`) steps by one or two (seeded, held, within ±3 of the user's), the bloom layout turns, and
  the swing spring gets a turn. The fold shift lands once the switch is past half (it is an integer).

## Drums depend on the input

**Percussiveness** (0 tonal … 1 percussive), from leaky rates over 3 s and means over 2 s:

```
drums = 0.4·min(hits/s ÷ 4, 1) + 0.3·min(onsets/s ÷ 5, 1) + 0.15·min(2·flatness, 1) + 0.15·min(1.5·hfc excess, 1)
tones = 0.45·pitch confidence + 0.3·key clarity + 0.25·scale fit
percussiveness → clamp(0.5 + 0.9·(drums − tones), 0.05, 0.95), eased over 3 s
```

It weighs `k0 PULSE` (the drum envelopes times percussiveness) and a small drum zoom pump (+3.5 %).
A drum-led track reads 0.94 in the test, a tonal one 0.06; silence settles at 0.5.

## The style knobs (`uKnob`)

`ShaderScene::uploadParams` uploads them from `NoteResponse` (0.5 while off); the neon set reads each as an
offset from 0.5 through `lib_neon`'s `neonKnob`, on top of the shared params and sound.

Each from several fields, smoothed at a musical rate (attack / release):

| Knob | Meaning | Fields | Smoothing |
|---|---|---|---|
| k0 PULSE | drums, as much as the track is drums | kick/snare/hat envelopes, onset envelope, bass att, × percussiveness | 20 ms / 180 ms |
| k1 TEXTURE | timbre | centroid, partial density, flatness, treble att | 250 / 500 ms |
| k2 FLOW | sustained energy | intensity, level, energy trend | 0.8 / 1.2 s |
| k3 TONE | melodic height | pitch height × pitch confidence, peak frequency | 100 / 250 ms |
| k4 BUILD | where it is heading | feeling tension and energy, build-up, chord tension | 0.6 / 1.5 s |
| k5 SPACE | how far it moves | interval swing and glide, stereo width | 150 / 600 ms |

Off, every knob is exactly 0.5, the midpoint the styles of #209 were tuned around. Switching on or off
eases over about 0.7 s; once fully off the layer forgets everything, so switching on starts fresh.

## Cost

CPU: a few hundred flops a frame (one pass over eight blooms, a dozen one-pole filters).

GPU: nothing while no bloom lives. With blooms, one fullscreen-triangle draw scissored to the blooms'
bounding box; each pixel tests eight discs (eight dot products) and only pixels inside a disc run the
flower: a back whorl, the heart and 2–4 levels, each an atan, a sincos and a few exp/pow, about 250 ALU
instructions at depth 4. On a 1080 × 2400 phone a bloom of radius 0.15 covers about 82 k pixels; six
live blooms are about 0.5 M flower pixels, ~125 M instructions a frame, about 7.5 GFLOP/s at 60 fps:
roughly 2–3 % of a mid-range GPU (Adreno 618 / Mali-G57 class, 250–400 GFLOPS), or under half a
millisecond. The scissor keeps the empty frame cost to the box around them.

## Variation: each play its own character

The owner asked for the tint and the density not to be fixed, and chose true randomness per play over a
seeded, replayable one. So `Renderer::noteSeed` mixes fresh `std::random_device` entropy into the
director's play seed each time that seed changes (a new track, a new play), and an export is a play of
its own: it will not match the live view bloom for bloom, by choice.

From that seed `NoteResponse` rolls a **personality** (`NoteResponse::Personality`), and rolls it again on
every `sectionChange` and every `drop`, easing to the new one over ~2.5 s:

| Trait | Range | What it changes |
|---|---|---|
| `maxAlive` | 3..8 | blooms on screen at once (past it the faintest gives its place) |
| `budget` | 1.5..3 / s | new blooms a second before each is dimmed (never past WCAG's three) |
| `size` | 0.75..1.35 | bloom radius |
| `tint` | 0.04..0.3 (squared draw: mostly light) | the whole picture's pull toward the notes' colour |
| `travel` | 0.15..1 | how fast near blooms drift out past the viewer |
| `tilt` | 0..0.9 rad | how far each flower's disc turns in 3D |

Given a seed the layer is still a pure function of (seed, frames, dt), which is what the host test pins.

## Tint, looks, perspective and motion

- **Tint**: a circular mean of the last notes' hues (newer weigh more), smoothed, sent to
  `composite_frag` as `uNoteTint`. Every family's finished frame leans toward it by the personality's
  amount × Note colours × (1 − ½ the director's weight), capped at 0.45, keeping each pixel's luma and
  leaving blacks and near-whites alone.
- **Looks** (`SceneParams::bloomLook`, 0 = the style's own from `Catalogue::bloomLook`): **flower** (the
  paradoxical fractal, unfurls in 0.7 s) for the fractal, kaleidoscopic and neon styles and Acid;
  **glow** (an orb, halo and star rays; 0.25 s, 0.8× size) for gas, glass, branches, silk and life;
  **ripple** (three interfering rings spreading like a drop on water; 1.4 s, 1.6×) for liquids,
  landscapes, scopes and tilings; **burst** (sparks thrown out, a heart that flashes and gives way;
  0.4 s, 1.3×) for dives, fly-throughs, particles and MilkDrop, whose presets already flower.
- **Perspective**: each bloom's disc is turned in 3D (`uBloomD.xy`, foreshortened with a touch of
  perspective) and rocks with the swing and glide; a leap rolls and turns the camera (`camRoll`,
  `camYaw`), a run of steps leans it, and a build pushes it in (`camPush`), all through SceneParams so
  `CameraRig` springs toward them and reduced motion and the safety clamp act on top.
- **Motion (vection)**: each bloom sits at a depth. Near ones drift outward from the centre and grow
  as they age, as if the viewer were travelling through a field of flowers, and slide against the
  camera's pan, roll and zoom more than far ones (parallax); far ones are dimmer and hazier.

## Exports

The offscreen export runs the same `Renderer::render`, steps the same sound engine at 1/fps and reads
the same process-wide switch, so the exported film has the same kind of knobs, distortion and blooms;
it rolls its own personality and bloom positions (see Variation).

## Previews

`core/tests/note_response_test --dump <melody|minor|held|run> <seconds>` prints the bloom uniforms the
engine produces after that much of a scripted scenario; `tools/glslcheck/check.mjs --bloom <json>
[--over <style>]` renders them over black or over a style, through the same screen blend.
