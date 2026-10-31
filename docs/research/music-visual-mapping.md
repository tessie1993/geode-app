# Music to visuals: what the engine hears, what moves, and the mapping we chose

For Customize › Camera, the layered motions (spawn, fractal growth, the fall-through zoom) and the
redone Reactions. Part 1 lists what the sound engine gives every frame. Part 2 is the research.
Part 3 is the mapping we build. Earlier research this builds on:
[`audio-reactive-mapping.md`](audio-reactive-mapping.md) (MilkDrop, TouchDesigner, Resolume, VDMX,
Synesthesia and why every style used to look the same).

## 1. What the engine hears, frame by frame

Source of truth: `core/sound/SoundFrame.h` (`GeodeSoundFrame`, 74 scalars plus 16 EQ bands, a 64-point
spectrum and a 128-sample waveform). The same frame reaches the reaction matrix
(`core/viz/Reactions.cpp`, one `ReactSource` per value), the camera rig (`core/viz/CameraRig.cpp`),
the motion field, the macros and every shader (`lib_sound`: `SND_*`, `uSoundTex`). Export steps its own
engine at 1/fps over the track (`OffscreenSceneRenderer`, `TrackAudio`), so an export hears what the
screen heard.

**There is no tempo, beat grid, bar, downbeat, section label, key or chord, and there won't be**
(sound-wiring decision, 2026-10-31: the engine never guesses a tempo). Musical structure comes from
counted drums, adaptive events and slow trends instead. The table says which value stands in for
each musical idea.

### Musical ideas and the values that carry them

| Musical idea | Values | What it does |
|---|---|---|
| **Pulse** (the beat) | `kick`, `kickEnv`, `bassAtt`, `subAtt` | the kick trigger fires per hit; its envelope jumps and decays (decay set in Settings › Audio) |
| **Backbeat** | `snare`, `snareEnv`, `snare2`, `snare4` | the other side of the groove |
| **Subdivision** (the fast layer) | `hat`, `hatEnv`, `trebleAtt`, `onset`, `onsetEnv` | ticks, shakers, note starts |
| **Bar / phrase** | `kick4`, `kick8`, `kick16`, `snare4` | every Nth hit, so "every bar" or "every phrase" with no tempo; in 4/4 with a kick per beat, `kick4` ≈ a bar, `kick16` ≈ a 4-bar phrase |
| **Section change / "big moment"** | `hardCut` | everything jumped past an adaptive threshold that then backs off for ~60 s: choruses, drops, key entries |
| **Drop** | `surge` (+ `hardCut`) | loudness jumped after a quieter passage |
| **Break / stop** | `silence`, then `returned` | the music stopped; it came back after ≥1 s |
| **Build / tension** | `energyTrend` (>0.5 rising), `intensity` | short loudness over long: a rise creeps above 0.5; intensity creeps toward 1 the longer the song stays above its own average |
| **Section energy** (verse vs chorus) | `intensity`, `bassPresence`, `midPresence`, `treblePresence` | ~3 s activity per band, deaf to single hits |
| **Music is playing at all** | `presence` | 0..1 fade over ~2 s |
| **Melody** | `pitch`, `pitchHeight` (C2..C6 → 0..1), `pitchClass` (for hue), `pitchConfidence`, `noteChange`, `interval`, `noteRandom` | the lead line: where it is, when it moves, how far and which way |
| **Phrasing of the melody** | `legato` (slide/tie), `reattack` (same note struck again), `glide`, `vibrato`, `runDensity` (busy runs vs held notes) | how the line is played |
| **Timbre / colour of the sound** | `centroid` (dark ↔ bright), `rolloff`, `flatness` (tonal ↔ noisy), `evenness` (hollow ↔ full), `densitySlow` / `densityFast` (how many partials) | texture rather than level |
| **Change in the sound** | `flux` (spectral change), `onset` | something new arrived |
| **Space** | `stereoWidth`, `stereoPan` | mono ↔ wide, left ↔ right |
| **Loudness** | `vol`, `volAtt`, `rms`, `loudness` (dB), `level` | relative to the song (`vol`) or absolute (`loudness`) |
| **Tempo feel** (without a tempo) | clocks: `bassTime`, `midTime`, `trebleTime`, `curvedTime`, `hitTime`, `noteTime` | they only move forward, as fast as their part of the music; quiet barely moves `curvedTime`, loud races it |
| **Variation per hit** | `kickToggle`, `snareToggle` (flip 0/1), `kickRandom`, `noteRandom` (new value per hit/note; seeded, so export draws the same) | stops every hit doing the same thing |
| **Fine detail** | `eq[16]` (25 Hz … 22 kHz), `spectrum[64]`, `waveform[128]` | for textures and shaders |

### Kinds of value, and how a route reads them

`Reactions.cpp kSourceTable` turns every field into a 0..1 (or −1..1) view:

- **Relative** (`bass` … `midHighAtt`, `flux`, `eq`): ~1.0 typical for this song now, 2 a strong hit;
  halved, so typical reads 0.5.
- **Shaped / unit** (`low`, `middle`, `high`, `level`, presences, descriptors, envelopes, melody): 0..1 as is.
- **Clock**: the clock's rate per second (Spin mode integrates it back into a phase).
- **Trigger**: held as an envelope (instant rise, a per-source fall: 0.15 s hat … 2.5 s hard cut).

### What already reacts, and to what (before this work)

| Stage | Hard-wired sound | Who can change it |
|---|---|---|
| `CameraRig` sound moves | orbit on `midTime`, roll sway on `curvedTime`, lean on `stereoPan` + `pitchHeight`, note look on `noteChange`/`noteRandom`, dolly on `surge`/`returned`/`energyTrend`, FOV breath on `bassAtt`, trauma shake on `kick`+`snare`, reframe on `kick16`, cut on `hardCut` | only the Space/Chaos macros' scale; **no source choice** |
| `MotionField` | breath on bass, drift on mids, hue on a style driver, orbit re-aim on `hardCut` / `kick16` | Customize › Reactivity: amount, breath, drift, hue |
| Style DNA (`ReactionDna`) | ≤ 9 routes per style: a lead personality on a kick source, a second on a snare source, then layers (big moments, sustained levels, top end, colour, rises, melody) | Reactions: re-roll, edit, add |
| Macros | Energy/Speed/Chaos/Space scale the above, plus 7 routes of their own | Customize › Macros |
| Shaders | each reads `SND_*` directly for its own detail | nobody |

Gaps this work closes: the camera's sources are fixed; nothing spawns, grows outward or zooms
through itself on a sound of its own (the existing Endless zoom is a constant-speed dive with no
sound and only inward); and the reaction editor speaks in raw engine names ("Spectral flux",
"Follow", "Spin") rather than in music.

## 2. Research, in short

Full notes with sources: [`camera-layers-research.md`](camera-layers-research.md) (camera grammar,
cross-modal correspondences, layering, spawn and zoom techniques, consumer reaction UIs) and
[`audio-reactive-mapping.md`](audio-reactive-mapping.md) (the VJ tools).

- **Cuts follow phrases, not beats.** Across 548 music videos the mean shot is 4.76 s, about two bars,
  and a section about 20 s; pop lines cuts up with section changes most (Prétet et al. 2021). Without a
  tempo, "every 8 kicks" and "big moment" stand in for "two bars" and "section change".
- **Energy reads as speed, quiet as settling** (Eitan & Granot 2006). So energy drives fly and dolly
  speed and a break lets the camera sink back, rather than energy driving height.
- **Directions people agree on** (Spence 2011, Marks, Küssner 2014, Adeli 2014, Palmer 2013): higher
  pitch → higher, smaller, brighter; louder → bigger, brighter; harsher timbre → sharper, coarser;
  fast and bright music → saturated, warm. We keep those signs; inverted ones read as wrong.
- **Layer by time scale.** Hits (tens of ms), levels (hundreds of ms), presence and trends (seconds),
  structure (sections). Every tool that looks alive gives each layer its own band or signal and makes
  structural changes on counted hits (every 4th/8th/16th kick), not on every hit (Synesthesia, Notch,
  TouchDesigner Audio Analysis 2.0).
- **Many-to-many beats one-to-one once several things change at once** (Hunt & Kirk 2000): ship coupled
  routes as presets and keep each route editable.
- **Shake = trauma² × smooth noise** (Eiserloh, GDC 2016); trauma piles up over hits and bleeds off.
- **Fall-through and growth:** MilkDrop's feedback zoom, Droste and log-polar zoom, layered "infinite
  fall" (the same image at octave scales, faded by scale), KIFS folds for outward fractal copies, and a
  noise-threshold dissolve that sweeps with a spawn's release envelope so things die on the decay.
- **Consumer apps hide the engine**: one Reactivity and one Speed knob, presets named by feel. Our grid
  shows every engine value (the user asked for that), so labels, groups and smart cells carry the
  plain language instead.

## 3. The mapping we build

Decisions (user, 2026-10-31): one C++ table of every sound value and every visual parameter
(`core/viz/ParamMatrix`); a grid editor with all engine values as rows, a tap sets depth with a smart
mode and a long press opens the details; 32 routes; the camera's feel picked per style; spawn, fractal
growth and fall-through strong in every style; the fall direction drawn uniquely per style, varied and
combined; editing Camera, the grid and the layers is Pro (free users see and hear each style's DNA).

### Everything is a route

The Camera tab, the layers and the grid are three views of the same 32 reaction routes, so all of it
is saved, exported and drawn by the wallpaper and the dream exactly like the routes already are.
New targets in `ParamMatrix`: **Camera rise** (crane), **Camera cut**, **Fall through** (signed: + falls
in, − falls out), **Fractal growth** and **Spawn**. When a route drives a camera channel, the rig's
built-in move for that channel steps aside, so the user's choice replaces the default rather than
fighting it.

### Camera channels

| Channel | Target | What the music does | Default sources the DNA draws from |
|---|---|---|---|
| Fly | Camera speed | thrust along the path | energy trend, intensity, loudness clock |
| Dolly | Camera push | push in / pull out | surge, kick envelope, rise |
| Orbit | Camera turn (Spin) | keeps circling, faster when busy | mid / bass / note clock |
| Crane | Camera rise | rise and sink | pitch height, centroid, intensity |
| Roll | Camera roll | lean or flip | snare toggle, stereo pan, loudness clock |
| Shake | Camera shake | handheld trauma | kick, snare, hat envelopes |
| Lens | Camera FOV | breathe the lens | bass fast, sub fast |
| Cut | Camera cut | jump to a new place | big moment, every 8th or 16th kick |

Feels the DNA picks per style: **Music video** (cut on big moments and every 8th kick, push through
rises, clock orbit, crane on melody, light shake), **Floaty journey** (no cuts, fly on energy, wide
orbit, crane on melody, no shake), **Club** (dolly on every kick, strong shake, cut every 4th kick,
roll flip on the snare). 2D styles see the same moves as pan, zoom, tilt and roll of the picture, and
fall-through gives them depth.

### Layers

| Layer | Target | Reads as | What drives it |
|---|---|---|---|
| Fall through | Fall through (−1..1) | the picture falls into itself (+) or back out (−), endlessly, at octave scales | per style: one of energy trend, a clock, kick envelope; a second opposing source; or flip on big moments |
| Fractal growth | Fractal growth (0..1) | mirrored copies grow outward in generations | intensity, rise, density, mid activity |
| Spawn | Spawn | a copy of the picture is born, grows and dissolves on each edge | kick, snare, onset, note change, every 4th kick |

Each style draws its layers so no two share a source, and none shares the lead personality's
source: the kick, the melody and the structure each move something different.

### Grid presets

Named by feel, each a full layered set over different time scales (hit, level, trend, structure):
Punchy, Floaty, Builds & drops, Melodic, Glitch, Fractal dive, Calm. A preset replaces the routes;
the style's DNA comes back with "Back to style DNA".
