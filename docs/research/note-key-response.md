# Note & key reactions

Settings › Behaviour › **Note & key reactions** (free, on by default; Customize › Advanced › Note & key,
which tunes it per style, is Pro). The styles answer more than the beat: the notes, the chords, the key,
the mood, how the music is played and how hard. Everything is real time: it reads the sound frame just
heard and never anticipates.

Code: `core/viz/NoteResponse` (the decisions, pure C++), the tint in `composite_frag.glsl`,
`data/NoteResponseStore` (free, on by default), `ui/NoteResponseSettings`, Customize › Advanced › Note & key. Tests:
`core/tests/note_response_test.cpp` (CI, "Native host tests"),
`NoteResponseStoreTest`.

## What the owner asked for

Gathered by interview; each line is a requirement this layer meets, not a fixed event-to-effect wire.

- Notes: pitch → colour, varied and multicolour across the picture (a melody paints a spread of
  colours, not one hue); pitch height → shape (forms open and stretch up for high notes, pull in for low
  ones); intervals → motion (leaps swing and twist, steps glide).
- The note blooms (a fractal flower, glow, ripple or burst drawn over the picture per note) were
  removed at the owner's request (2026-11): they never asked for them.
- Key and mood: the key sets the palette family; major warm and bright, minor cool and deep; harmonic
  tension distorts, resolution smooths it back; a chord change shifts the scene.
- Drums: their weight depends on the input, measured continuously.
- A switch, persisted, free and on for new installs; off is exactly today's picture.
- Variation (second round): tint strength is not a fixed amount but changes, truly
  at random per play, and again at every section; the result is judged on variation, harmony, the
  qualities of good trippy visuals, whether the perspective changes and whether the viewer feels motion.
- It lives inside the auto-director: the director keeps the section-level palette and camera, this
  layer adds note- and chord-level life within them.

## Where it sits in the frame

```
sound frame ─► Director.step (feeling, look) ─► NoteResponse.step (reads the frame, the feeling, the look's hue)
displayed params ─► Director.apply ─► NoteResponse.apply ─► macros ─► LFOs ─► ADSR ─► reactions
                ─► macro routes ─► director routes ─► MotionField ─► safety::apply
composite (the notes' tint) ─► the user's overlay
```

The user's own settings, LFOs, the motion field and the safety clamp all act after this layer, so they
win; the flash and motion limits cap whatever it adds. The director's emotion is stepped whether or not
the director drives, so this layer always has a mood to read.

## Notes

**Colour.** A note's hue is its pitch class against the key's tonic on the circle of fifths, times
0.85 of the wheel per twelve fifths, around an anchor hue. Harmonic neighbours are colour neighbours:
the tonic is the anchor, the dominant one step one way, the subdominant one step the other. A diatonic
melody spans about half the wheel; chromatic notes reach the far side. Octave adds a small lift (±0.03).
The last six notes' hues are a ring (`NoteUniforms::hues`), newest first; before the first note it is a spread
around the anchor, so it is never one colour.

**Anchor.** With the director off, the key: its place on the circle of fifths (`keyHue`) pulled toward
amber for major and deep blue for minor by up to 55 % of the key's clarity (Palmer et al. 2013:
major and happy music is matched to lighter, warmer, yellower colours; minor and sad to darker, bluer
ones). With the director on, its look's hue, weighted by how much it drives: the notes then spread
around the director's palette instead of starting a palette of their own.

**Shape.** `k3 TONE` follows the melody's height (pitch height trusted as far as the pitch confidence,
plus the strongest partial). On shared params it opens `morph` for high notes and closes it for low ones
(±0.15).

**Intervals.** A leap (a fourth or more) kicks a damped spring (0.45 Hz, damping 0.35) in the leap's
direction, harder the wider it is: `twist` and `rotation` swing and settle in about two seconds. A step
adds to a leaky glide (1.5 s) that sways the picture. `k5 SPACE` hears both.

## Key, mood, harmony

- **Temperature and brightness**: major warms (+0.25 × clarity), minor cools; a happy feeling brightens
  up to 6 %. Only where the director is not deciding the palette (scaled by 1 − its weight).
- **Tension → distortion**: 60 % the chord's dissonance, 40 % the feeling's tension, eased up in a
  quarter second and back down over 0.9 s (resolution). It adds `warp` (up to +0.35) and chromatic split
  (+0.22). Never `glitch`: a composite glitch on a chord reads as a flash, not a bend (and #216 retires
  the field).
- **Chord change → scene shift**: on `harmonicChange`, at most once per 1.5 s: the fold count
  (`symmetry`) steps by one or two (seeded, held, within ±3 of the user's), and the swing spring gets a
  turn. The fold shift lands once the switch is past half (it is an integer).

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

CPU: a few hundred flops a frame (a dozen one-pole filters). GPU: nothing of its own; the tint is a
few instructions inside `composite_frag`, which runs anyway.

## Variation: each play its own character

The owner asked for the tint not to be fixed, and chose true randomness per play over a
seeded, replayable one. So `Renderer::noteSeed` mixes fresh `std::random_device` entropy into the
director's play seed each time that seed changes (a new track, a new play), and an export is a play of
its own: it will not match the live view note for note, by choice.

From that seed `NoteResponse` rolls a **personality** (`NoteResponse::Personality`), and rolls it again on
every `sectionChange` and every `drop`, easing to the new one over ~2.5 s:

| Trait | Range | What it changes |
|---|---|---|
| `tint` | 0.04..0.3 (squared draw: mostly light) | the whole picture's pull toward the notes' colour |

Given a seed the layer is still a pure function of (seed, frames, dt), which is what the host test pins.

## Tint and perspective

- **Tint**: a circular mean of the last notes' hues (newer weigh more), smoothed, sent to
  `composite_frag` as `uNoteTint`. Every family's finished frame leans toward it by the personality's
  amount × Note colours × (1 − ½ the director's weight), capped at 0.45, keeping each pixel's luma and
  leaving blacks and near-whites alone.
- **Perspective**: a leap rolls and turns the camera (`camRoll`, `camYaw`), a run of steps leans it,
  and a build pushes it in (`camPush`), all through SceneParams so `CameraRig` springs toward them and
  reduced motion and the safety clamp act on top.

## Exports

The offscreen export runs the same `Renderer::render`, steps the same sound engine at 1/fps and reads
the same process-wide switch, so the exported film has the same kind of knobs, distortion and tint;
it rolls its own personality (see Variation).
