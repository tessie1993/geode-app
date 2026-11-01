# Reactive visuals: what each sound should move, and how to let people patch it

Research note, 2026-11-01. It continues [`audio-reactive-mapping.md`](audio-reactive-mapping.md), which surveys the
tools in depth. This note is about the *next* step the owner asked for: make every Customize control mean something,
make the tabs easy, and grow the reaction routes into a modulation matrix with tempo-synced LFOs and a beat tracker
that runs only when a synced LFO needs it. Links are given inline. Where only secondary sources were found, it says so.

## 1. What exists today (read from the code)

| Piece | Where | Shape today |
|---|---|---|
| Sound frame | `core/sound/SoundFrame.h` (`GeodeSoundFrame`, 74 scalars + 16 EQ + 64 spectrum + 128 waveform) | Relative levels (MilkDrop `bass`/`bass_att`), shaped 0..1 levels (TouchDesigner low/middle/high), slow trends (presence, intensity), clocks (`bassTime`, `curvedTime`, `hitTime`), descriptors (loudness, centroid, rolloff, flatness, flux, energyTrend, stereo), drum triggers and envelopes (kick/snare/hat, every 2nd/4th/8th/16th kick), events (hardCut, onset, silence, returned, surge), melody (pitchClass, pitchHeight, interval, glide, vibrato, runDensity). **No tempo, beat phase or bar phase.** The old analyser's Beat/Downbeat/BeatOsc sources were dropped with it. |
| Sound engine | `core/sound/SoundEngine.hpp`, `Stages.hpp`, `FilterBank.hpp`, `Fft.hpp` | Steps once per rendered frame (no hop); the filter bank runs per sample in `write()`. Export steps at dt = 1/fps through the same code. |
| Reaction matrix | `core/viz/Reactions.hpp` | 12 routes: source (72 `ReactSource`s, grouped LEVELS, DRUMS, EVENTS, MELODY, CLOCKS, TEXTURE, EQ), target (`LfoTarget`), mode (Follow, Step, Spin, Jump, Toggle), amount −2..2, curve, attack and release in seconds. Luminance floors 80 ms / 350 ms. |
| Style DNA | `core/viz/ReactionDna.hpp` | A personality per style on the kick side and the snare side, plus layers; leaves 3 routes free for the user and Events. |
| LFOs | `core/viz/Lfo.hpp` | 3 slots; wave (sine, triangle, saw, square, random), `rateSeconds` 0.05–60 s (free time only), depth, polarity, curve; source may also be a band (`ModSource`). Chain targets `Lfo1Rate…Lfo3Depth`. |
| Envelopes | `core/viz/Adsr.hpp` | 2 ADSRs gated by a band over a threshold, several targets each. |
| Macros | `core/viz/Macros.hpp` | 6 knobs −1..1 (Energy, Speed, Complexity, Mood, Space, Chaos), own 7-route engine. |
| UI | `app/src/main/java/dev/geode/ui/CustomizeTabs.kt` | Tabs Motion, Shape, Scene, Reactivity, Colour, FX, Fluid, Cymatics. Section headers include Movement, Drift, Distortion, Symmetry & tiling, Particles, MilkDrop, Audio response, Continuous motion, Band balance, Envelope shape, Triggered envelopes, Modulation slots, Palettes, Hue, Grading, Tone effects, Feedback & trails, Screen FX, Layers, Camera, Reactions, Water, Emitters, Look, Audio routing, Wave, Field. |
| Slider labels | `LabeledSlider` (`CustomizeTabs.kt:920`) | Shows `"%.2f"` of the raw value. **No units anywhere**: a 0.35 s release, a 1.8× gain and a 0.4 depth all read as bare decimals. |

So the engine side is rich; what is missing is (a) tempo, (b) one place that holds every modulation, (c) words and units a
non-expert can read.

## 2. Findings

### 2.1 How respected tools map sound to pictures

- **MilkDrop / projectM.** Three bands (`bass`, `mid`, `treb`) relative to their own recent average, so ~1 is "normal for
  this song" and ~1.3 is loud; each has a damped `_att` twin. Preset authors detect beats by comparing the fast value with a
  slow peak of the damped one. ([authoring guide](https://geisswerks.com/milkdrop/milkdrop_preset_authoring.html),
  [example preset](https://gfiber.googlesource.com/vendor/opensource/projectM/+/master/presets_milkdrop_200/Various%20-%20Fire%20and%20Brimstone.milk),
  [variable list, forum](https://forum.powerampapp.com/topic/22699-experimenting-with-visualisation-presets/)). Geode already has this.
- **TouchDesigner `audioAnalysis`.** Outputs low/mid/high (smooth), kick/snare/rhythm (0/1 pulses), spectral centroid and
  slow/fast spectral density; only Low is on by default and thresholds set sensitivity
  ([Derivative docs](https://derivative.ca/UserGuide/Palette:audioAnalysis),
  [beginner guide](https://interactiveimmersive.io/blog/touchdesigner-lessons/audio-reactive-visuals-a-beginner-guide/)).
  The community "Audio Analysis 2.0" adds every 4th/8th/16th-beat triggers
  ([post](https://derivative.ca/community-post/asset/audio-analysis-20-extended-audio-reactive-components/73946)).
  Lesson: few sources on by default, more one tap away.
- **Resolume.** Any parameter can take FFT (composition/layer/clip) with a *Fall* control (release), or BPM Sync, where the
  parameter runs in beats instead of seconds. Resolume does **not** detect BPM; users tap, or receive tempo from outside, and
  some prefer tapping because detected tempo "jerks" playback ([Parameter animation](https://resolume.com/support/en/6/parameter-animation),
  [forum: no BPM detection](https://resolume.com/forum/viewtopic.php?p=79114), [forum: Fall](https://resolume.com/forum/viewtopic.php?p=70304)).
  Lesson: a synced LFO must never jump when the tempo estimate moves; tap must exist as a fallback.
- **Synesthesia.** ~41 audio uniforms over bass/mid/midhigh/high: level, hits, presence, time clocks, and BPM-locked
  oscillators (`syn_BPMTwitcher`, `syn_BPMSin…`) ([community thread](https://community.troikatronix.com/post/49692),
  [uniform gist](https://gist.github.com/mattorp/9de35993ad2834c4df064756e1efbc63)). Official per-uniform docs were not reached.
- **Magic Music Visuals.** Each audio feature passes through modifiers in order (Peak, Average, Smooth, Scale, Offset);
  a ~0.1 s Peak makes a size swell up fast and fall smoothly. Scale = amount of modulation, Offset = resting value
  ([forum](https://magicmusicvisuals.com/forums/viewtopic.php?p=2219), [forum](https://magicmusicvisuals.com/forums/viewtopic.php?p=8214)). Lesson: "amount" and "rest" are separate, and order matters.
- **Synth mod matrices.** Bitwig: arm a modulator, then drag on any knob to set depth; every device has modulator slots
  ([Bitwig manual](https://www.bitwig.com/userguide/latest/the_unified_modulation_system/)).
  Serum's matrix row is source → amount → destination, plus a *curve* (spend more time near the top or bottom) and an
  *aux source* that scales the row (e.g. a macro scales an LFO's depth) ([Cymatics tips](https://cymatics.fm/blog/serum-tutorial-25-tips/),
  [Syntorial](https://www.syntorial.com/tutorials/synth-tutorial-creating-a-two-tiered-mod-wheel-in-serum/)). Vital switches an LFO's
  frequency between Seconds and Tempo; synced rates are discrete and cannot glide between straight and triplet
  ([Vital forum](https://forum.vital.audio/t/lfo-rate-automation-between-straight-and-triplet/7509)). Official Vital docs were not reached.
- **Mobile routing.** AUM routes with a source × destination grid: tap a cell to connect, a line confirms it
  ([Dubspot review](https://blog.dubspot.com/ios-music-app-review-aum-pt2)); apeMatrix is built entirely on a grid
  ([Synthtopia](https://synthtopia.com/content/2018/06/09/new-app-apematrix-gives-you-complete-control-of-audio-routing-in-ios)).
  A full grid of 80 sources × 50 targets does not fit a phone; a *list of cables* with a grid only for a filtered view does.

### 2.2 Which sounds people perceive, and what they should move

Cross-modal research is consistent on a few pairs: **loudness ↔ size**, **loudness ↔ brightness**, **pitch ↔ height**,
**spectral brightness ↔ colour brightness**, low pitch ↔ large and dark
([Tsiros, cross-modal mappings](https://napier-repository.worktribe.com/output/1129780/utilising-natural-cross-modal-mappings-for-visual-control-of-feature-based-sound-synthesis),
[Springer 2024 review](https://link.springer.com/article/10.3758/s13414-024-02975-7),
[music–colour study](https://pmc.ncbi.nlm.nih.gov/articles/PMC4671663/),
[pitch-luminance](https://inquiriesjournal.com/articles/767/visualizing-sound-effects-of-pitch-height-and-tonality-on-luminance-matching)).
Emotion mediates colour choices (happy ↔ yellow, angry ↔ large red), so colour from *mood over seconds* reads better than
colour from every hit.

Onsets are best found from spectral flux (sum of positive log-spectrum differences), not from raw amplitude, because
soft-onset instruments spread their change across the spectrum ([Bello et al. 2005](https://www.cs.bu.edu/fac/snyder/cs583/Literature%20and%20Resources/bello_onset_tutorial.pdf)).
Geode's `flux` and `onset` already follow this.

**Timing budget.** Broadcast tests find mismatch detectable from about 45 ms of sound-before-picture to about 125 ms of
picture-before-sound (sources disagree on the sign convention), but the window is asymmetric: viewers forgive
pictures that *lag* sound more than pictures that lead it ([ITU-R BT.1359 summary](https://en.wikipedia.org/wiki/Audio-to-video_synchronization),
[NTIA report](https://its.ntia.gov/publications/download/11-474.pdf)). For hits this means: attack ≤ 15 ms (one frame at 60 fps),
and a release long enough to *see* (≥ 100 ms), never a smoothing that delays the peak.

Recommended smoothing by signal kind (synthesis of the tools above and the existing tuning defaults):

| Kind | Attack | Release | Why |
|---|---|---|---|
| Hit (kick, snare, hat, note start) | 0–15 ms | 120–300 ms (hat 60–120) | Peak on the frame of the hit; decay readable. |
| Band level (bass, mids, highs) | 30–80 ms | 250–600 ms | Follows the groove without flicker. |
| Loudness / energy | 100–300 ms | 0.8–2 s | Breath, not twitch. |
| Brightness, colour, mood | 0.5–1 s | 2–6 s | Colour that flickers reads as a bug. |
| Build-up, presence | 1–3 s | 4–10 s | Section-scale. |
| Anything on a luminance target | ≥ 80 ms | ≥ 350 ms | Already enforced (`kLuminanceMin*`), plus `safety::apply`. |

### 2.3 Real-time tempo and beat on a phone

- **Ellis 2007** ([PDF](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Ellis_2007.pdf),
  [LabROSA](https://www.ee.columbia.edu/~dpwe/LabROSA/projects/beattrack/)): onset-strength envelope → autocorrelation
  weighted by a log-Gaussian tempo prior (centred near 120 BPM) → dynamic programming that balances "beats on strong onsets"
  against "spacing near the tempo". Offline as published, but the forward pass is causal and ~10 lines.
- **Davies & Plumbley** ([ISMIR 2004 causal tempo tracking](https://archives.ismir.net/ismir2004/paper/000226.pdf),
  [AES 2005](https://aubio.org/articles/davies05aesbeattracking.pdf)): autocorrelation of the onset function every ~1.5 s,
  passed through a comb-filter bank (sum of lag, 2×lag, 3×lag, 4×lag) with a tempo weighting, then beat phase by
  predicting the next beat. This is what aubio's `tempo` implements (hop 512 at 44.1 kHz ≈ 11.6 ms;
  [aubio man page](https://manpages.ubuntu.com/manpages/lunar/man1/aubio.1.html)).
- **BTrack** (Stark, Davies, Plumbley, [DAFx-09](https://dafx.de/paper-archive/details/XiEJjIc0a2Hb_KTJon0SaA),
  [code](https://code.soundsoftware.ac.uk/projects/btrack)): causal, real-time, close to offline accuracy; a cumulative
  score (Ellis-style) updated per onset sample plus a periodic tempo re-estimate.
- **Scheirer 1998** ([PDF](https://www.ee.columbia.edu/~dpwe/papers/Schei98-beats.pdf)): band envelopes into banks of
  resonant comb filters per tempo hypothesis. Accurate but the heaviest of these, flagged as costly for portable devices.
- **Licences.** BTrack and aubio are GPL-3; Geode cannot vendor them. The algorithms are published and simple enough to
  write natively from the papers.
- **Tempo-sync conventions.** Ableton Link shares tempo and *phase within a quantum* (bar), not beat numbers; a peer that
  joins snaps its phase to the existing barline ([Link header](https://hackage.haskell.org/package/tidal-link-1.0/candidate/src/link/include/ableton/Link.hpp),
  [SuperCollider LinkClock](https://docs.supercollider.online/Classes/LinkClock.html)). That is the model for "phase reset
  on the downbeat": keep beat phase continuous, re-align the bar.

### 2.4 Beginner-first editors

- Progressive disclosure: defer advanced or rarely used controls to a secondary screen; it makes apps easier to learn and
  less error-prone ([NN/g](https://www.nngroup.com/videos/progressive-disclosure/)). Label the door descriptively
  ("More reaction options", not "More…").
- Material sliders show a value label while the thumb is held; Material recommends a label for discrete values, and shows the
  exact value at the handle ([Material Web](https://material-web.dev/components/slider/)). For a continuous slider with a
  meaningful unit, show the value with its unit *beside the title* all the time and the bubble only while dragging.
- Drag-to-modulate (Bitwig, Vital, Serum) is the fastest pattern on desktop. On a phone, the same idea works as
  "long-press a slider → *React to…*", which opens a source picker and drops a cable, so routing starts from the thing the
  user wants to move, not from an abstract matrix.

## 3. Sound vocabulary for the UI

Plain names, grouped as people hear music. Each maps to fields that already exist in `GeodeSoundFrame`
(the matrix may read the `*Env` twin for triggers so cables get a decay). New fields are marked **new**.

| Group | UI name | Hint line under it | Frame field(s) |
|---|---|---|---|
| Drums | Kick | The low drum hit | `kick` / `kickEnv` |
| | Snare & clap | The backbeat | `snare` / `snareEnv` |
| | Hi-hats | Ticks and shakers | `hat` / `hatEnv` |
| | Every 2nd / 4th / 8th / 16th kick | Counts the kicks | `kick2…kick16` |
| | Kick flip-flop | On, off, on, off | `kickToggle` |
| | Kick dice | New random value per kick | `kickRandom` |
| Levels | Sub | What you feel more than hear | `sub`, `subAtt` |
| | Bass | Bass line and kick body | `bass` / `low` |
| | Mids | Vocals, guitars, keys | `mid` / `middle` |
| | Highs | Cymbals, air, sparkle | `treble` / `high` |
| | Volume | Overall loudness | `level` (follow) / `loudness` (dB) |
| | Build-up | Rises the longer it stays loud | `intensity`, `energyTrend` |
| Character | Brightness | Dark ↔ bright sound | `centroid` |
| | Noisiness | Tone ↔ noise | `flatness` |
| | Change | How much the sound is changing | `flux` |
| | Fullness | How many notes at once | `densitySlow` / `densityFast` |
| | Width | Mono ↔ wide stereo | `stereoWidth` |
| | Left ↔ right | Where the sound sits | `stereoPan` |
| Moments | Big moment | Drops and hard changes | `hardCut` |
| | Any note starts | Every new sound | `onset` / `onsetEnv` |
| | Gets louder | A swell after a quiet part | `surge` |
| | Silence / Comes back | Stops, and returns | `silence`, `returned` |
| Melody | Note height | Low ↔ high melody | `pitchHeight` |
| | Note colour | Which note (C…B), for hue | `pitchClass` |
| | New note | The melody moves | `noteChange` |
| | Slide / Vibrato | Bends and wobble | `glide`, `vibrato` |
| | Busy melody | Fast runs vs held notes | `runDensity` |
| Time | Beat | Ramps 0→1 every beat | **new** `beatPhase` |
| | Bar | Ramps 0→1 every bar | **new** `barPhase` |
| | On the beat / On the bar | One pulse per beat / bar | **new** `beat`, `downbeat` (+ `beatEnv`) |
| | Tempo | Slow songs low, fast high (60–180 BPM → 0..1) | **new** `tempo` |
| Modulators | LFO 1–4, Envelope 1–2, Macro knobs, Random | The app's own shapes | `LfoEngine`, `AdsrEngine`, `Macros` |
| EQ (Advanced only) | 25 Hz … 22 kHz | One slice of the spectrum | `eq[16]` |

Hide clocks (`bassTime`, `curvedTime`…) as sources: they are what **Spin** mode integrates; expose them as "keeps
moving while the bass plays" via the mode, not as a source. Proposed GLOSSARY entries: **Cable** (one source → one
destination; *Avoid*: binding), **Beat phase**, **Bar phase**, **Tempo sync**, **Division**, **Tap tempo**.

## 4. Default routings per destination

Amount is a share of the destination's range. These are the defaults a cable should start with when the user picks that
source for that destination (the "smart default"), and what the DNA should keep drawing from.

| Destination | Best source (alt) | Mode | Amount | Attack / release | Notes |
|---|---|---|---|---|---|
| Zoom / scale / camera push | Kick (Bass) | Follow | 10–20 % | 5 ms / 200 ms | Loudness ↔ size. |
| Camera shake | Kick, Big moment | Follow | 10 % | 0 / 180 ms | Trauma-style; already in `CameraRig`. |
| Forward travel / speed | Bass (Volume) | Spin | 30 % | 100 ms / 600 ms | Motion from integrals, not twitch. |
| Overall speed | Volume, Build-up | Follow | 25 % | 300 ms / 1.5 s | Energy ↔ tempo of motion. |
| Rotation | Mids | Spin; or Snare → Step | 20 % | 50 / 400 ms | Step on snare = a notch per backbeat. |
| Hue shift | Brightness (Note colour for melodic music) | Follow | 15 % | 0.8 s / 3 s | Slow; Jump on Snare for "new colour per hit". |
| Palette swap / second palette | Every 16th kick, Big moment, On the bar ×8 | Toggle | 100 % | 300 / 300 ms | Phrase-scale change. |
| Saturation | Build-up | Follow | 20 % | 1 s / 4 s | Grows into the chorus. |
| Brightness, bloom, intensity | Volume (Kick at low amount) | Follow | ≤ 25 % | ≥ 80 / ≥ 350 ms | Floors and flash budget apply. |
| Warp / turbulence / ripple | Change, Mids | Follow | 20 % | 30 / 250 ms | Texture ↔ texture. |
| Detail, particles, sparkle | Highs, Hi-hats | Follow | 25 % | 5 / 100 ms | Small and fast for small and fast sounds. |
| Glitch, chromatic split | Big moment, Snare | Jump | 30 % | 0 / 150 ms | Rare events only; never on every kick. |
| Morph (shader look) | Bar phase, New note | Follow / Step | 30 % | 200 / 800 ms | Designed change over time. |
| Camera look / new angle | Every 4th kick, On the bar | Jump | 40 % | 150 / 600 ms | Glided, not cut. |
| Tilt / pan | Left ↔ right | Follow | 15 % | 200 / 600 ms | Stereo ↔ space. |
| Height / vertical drift | Note height | Follow | 15 % | 150 / 500 ms | Pitch ↔ height. |
| Vignette / space | Silence, Volume (inverted) | Follow | 20 % | 0.5 / 2 s | Quiet closes in. |
| Trails / feedback | Fullness, Build-up | Follow | 20 % | 0.5 / 2 s | Dense music, longer trails. |

## 5. The matrix proposal

**One engine, one list.** Fold `ReactionEngine` routes, `LfoEngine` slots and `AdsrEngine` targets into one native
`ModMatrix` in `core/viz` that holds *cables*. LFOs and envelopes become **sources** (modulators) instead of owning
their targets; their old target fields migrate into cables. Keep `Reactions.cpp` modes and smoothing as the cable's
processing. Keep wire ordinals append-only and keep the DNA writing cables, so live, wallpaper, dream and export stay equal.

- **Sources:** section 3's list (≈ 45 user-facing, 72 + new tempo fields underneath), plus LFO 1–4, Envelope 1–2, the six
  Macros, Random (new value per beat/bar), and Constant (for offsets).
- **Destinations:** the existing `LfoTarget` list, grouped as the tabs are (Motion, Shape, Colour, FX, Camera, Scene-specific),
  plus modulator parameters (LFO n rate/depth/phase, Envelope n amount) and the matrix's own cable depths ("depth of cable 3").
  Scene-specific targets appear only for styles whose `ParamScope` uses them, as the sliders already do.
- **Per cable:** on/off; amount −100…100 % (bipolar, with "invert" as the sign); curve (Linear, Ease in, Ease out, S-curve;
  today's Exponential/Logarithmic/Smooth renamed); mode (Follow, Step, Spin, Dice=Jump, Flip=Toggle); attack and release
  (ms/s); optional **scaled by** (Serum's aux: a second source multiplies the amount, e.g. Build-up × Kick); optional
  **every Nth / not faster than** (divider and refractory from the earlier note's item 3, in hits or in divisions when tempo is on).
- **Slots:** 16 cables per style (DNA uses ≤ 9, leaving ≥ 7 for the user and Events); macros keep their separate 7.

### 5.1 LFO rate: Hz or tempo

Each LFO gets **Rate mode: Free (Hz) | Synced (division)**, plus phase (0–360°) and **Restart: never / every bar / on kick /
on big moment**. Free keeps today's 0.01–30 Hz, shown as Hz ≥ 1 and as "every 12 s" below 0.1 Hz.

Divisions, in the order of the picker (the owner's list in bold; `T` = triplet ×2/3, `D` = dotted ×3/2; 4/4 assumed):

`1/32` · `1/16T` · **`1/16`** · `1/16D` · `1/8T` · **`1/8`** · `1/8D` · `1/4T` · **`1/4` (1 beat)** · `1/4D` · `1/2T` · `1/2` ·
`1/2D` · **`1 bar`** · **`2 bars`** · **`4 bars`** · **`8 bars`** · `16 bars`.

Period in seconds = `60 / bpm × beats`, where beats = 4 × fraction for note values (1/16 = 0.25 beats) and 4 × bars for bars.
Show the resolved time next to the choice ("1/8 · 0.25 s at 120 BPM") so the unit is never abstract. Rate cannot glide between
divisions (as in Vital); a cable to a synced LFO's rate steps through the division list instead.

**Phase rule (the anti-jerk rule from Resolume users).** A synced LFO's phase is `fract(beatCount / beatsPerCycle) + phaseOffset`,
derived from the tracker's continuous beat count, never from wall time × current BPM. When the tempo estimate moves, the beat clock
changes *speed*, not position; when a downbeat re-aligns, the clock slews to it over ¼ beat (Link-style quantum alignment) instead of jumping.

### 5.2 Built-in tempo tracker

**Where:** a new stage `TempoTracker` in `core/sound` (`core/sound/Tempo.{hpp,cpp}`), owned by `SoundEngine`, writing
`tempo`, `beatPhase`, `barPhase`, `beat`, `downbeat`, `beatEnv`, `tempoConfidence` into `GeodeSoundFrame` (append fields;
bump `GEODE_SOUND_SCALARS`; add the matching `ReactSource`s at the end of the enum).

**Algorithm (written from the papers; nothing GPL vendored):**
1. **Onset strength at a fixed 100 Hz**, computed in the per-sample `write()` path from the existing filter bank: every 10 ms of
   samples, half-wave-rectified log-energy rise summed over the bands, kick band weighted ×2. Doing this per sample, not per
   rendered frame, makes it independent of fps and of how audio is chunked, so export (1/fps steps) and live agree.
2. **Tempo every 0.5 s:** autocorrelate the last 6 s (600 values) over lags for 60–200 BPM (30–100 lags); apply a comb sum
   (lag + 2·lag + 3·lag + 4·lag, Davies–Plumbley) and a log-Gaussian prior centred on 120 BPM, σ ≈ 1 octave (Ellis).
   Parabolic interpolation on the peak for sub-lag precision. Resolve octave errors by preferring 80–160 BPM unless the
   other octave scores ≥ 1.5× higher.
3. **Hysteresis:** adopt a new tempo only if it wins two estimates in a row or differs < 3 %; confidence = peak / mean of the
   weighted ACF, smoothed over 2 s.
4. **Beat phase:** a causal Ellis/BTrack-style cumulative score at 100 Hz (`score[n] = onset[n] + α·max over the previous
   beat window weighted by a log-Gaussian around one period`), predicting the next beat; a phase-locked loop nudges the beat
   clock ≤ 10 % per beat toward predicted beats. The clock free-runs between beats.
5. **Bar (downbeat):** 4 beats per bar; pick which of the 4 positions is "1" from a running histogram of kick strength per
   position (kicks land on 1 and 3 far more than 2 and 4), decaying over ~16 bars; ties keep the current choice.
   **Tap** on the tempo chip sets tempo from ≥ 3 taps and the downbeat from the last tap; a manual tempo locks until cleared.

**CPU budget:** per 10 ms, ~bands × 2 ops; per 0.5 s, 600 × 70 ACF MACs ≈ 42 k plus comb ≈ 10 k; DP ≈ 100 Hz × 100 ops.
Well under 0.1 ms per second of audio on a mid phone, < 8 KB state. Target: ≤ 0.05 ms per rendered frame, measured
on-device with `GlProfile`-style timing; budget it at 1 % of the 16.6 ms frame.

**Latency:** tempo confident after 4–6 s of steady music; beat clock is *predictive*, so a beat-synced LFO lands on the
beat with 0 latency once locked (better than a reactive kick, which is ≥ 1 frame). Until confidence ≥ 0.4, synced LFOs run
at the last good tempo (else 120 BPM), and the tempo chip shows "Listening…".

**On only when needed:** `SoundEngine` exposes `setTempoWanted(bool)`. The renderer sets it each frame from
`ModMatrix::needsTempo()` = any enabled LFO in Synced mode, any cable whose source is Beat/Bar/On the beat/On the bar/Tempo,
any divider in divisions, or the tempo chip open. When it goes off the tracker stops computing (onset ring keeps filling
cheaply so a re-enable warms up from history); after 30 s off it drops its state. The export renderer computes the same flag
from the same cables, and starts tracking from the track's first sample, so an export's beat grid is at least as good as live.

## 6. Units and labels

Replace `LabeledSlider`'s `"%.2f"` with a per-param formatter declared next to the range (Kotlin reads it with the
catalogue, or a `ParamUnit` enum in `:engine:scenes`). Show "Title · value unit" on the row, the bubble only while dragging,
and tap the value to type it.

| Kind of value | Unit | Example | Use for |
|---|---|---|---|
| Amount, depth, mix, cable depth | % (signed for bipolar) | `+35 %`, `−20 %` | Route amounts, LFO depth, opacity, saturation, bloom |
| Gain, multiplier, zoom factor | × | `1.8×` | Band gain, reactivity, speed, audio drive, zoom |
| Time ≥ 1 s | s | `2.5 s` | Release, LFO free period, fade, presence |
| Time < 1 s | ms | `35 ms` | Attack, short release, drum decay |
| Frequency | Hz / kHz | `0.25 Hz`, `2.3 kHz` | Free LFO rate, EQ band |
| Tempo | BPM | `124 BPM` | Tempo chip, tap |
| Division | note / bars | `1/8D`, `4 bars` | Synced LFO, dividers |
| Angle, hue, phase | ° | `90°` | Rotation, hue shift, LFO phase, kaleido |
| Level | dB | `−12 dB` | Silence threshold, loudness |
| Count | plain integer | `6` | Symmetry, tiles, every-Nth |
| Abstract 0..1 style control | 0–100, no unit, with end labels | `Calm … Wild` | Macros, chaos, detail |

Ends of an abstract slider get words ("Tight ↔ Loose", "Dark ↔ Bright") rather than numbers.

## 7. UX outline: simple first, Advanced tucked

**Simple (default, every style):**
1. **Macros** row (exists): Energy, Speed, Complexity, Mood, Space, Chaos.
2. **"What reacts"**: up to five plain cards from the current cables, each one sentence: *"Kick → zooms in (strong)"*,
   *"Brightness → shifts colour (gentle)"*. Each card: on/off, a three-step strength (Gentle / Normal / Strong ↔ 10/25/50 %),
   and a live meter showing the source moving. "+ Add a reaction" opens a two-step picker: *What should move?* (destination
   tiles with icons) → *To what?* (section 3 sources, the recommended one first per section 4), and fills attack, release, mode
   from section 4.
3. **Tempo chip** (only once something synced exists): `124 BPM ●` with the confidence dot; tap = tap tempo, long-press = reset to auto.
4. Long-press any slider in any tab → **React to…** (drop a cable onto it) and **Wobble with LFO…** (new LFO cable). A slider
   with a cable shows a thin ring arc of its modulation range, as Bitwig/Vital do.

**Advanced (one descriptive door: "Advanced reactions & modulators"):**
- **Cables list:** one row per cable: source chip → mode → amount bar → destination chip; expand for curve, attack/release,
  scaled-by, every-Nth/not-faster-than. Rows from the style DNA are tagged "style" and can be reset to DNA.
- **Grid view** (toggle): sources down, destinations across, filtered to one destination group at a time so it fits a phone;
  a cell's dot size is the amount (AUM-style tap to connect).
- **Modulators:** LFO 1–4 (wave preview, Free Hz / Synced division, phase °, restart), Envelope 1–2 (ADSR drawn as a shape,
  trigger source), each with its outgoing cables listed.
- **Sound tuning** (Settings › Audio today) linked, not duplicated.

**Per tab:** each detail tab keeps two to four essential sliders visible per section and puts the rest under the section's
own "More" disclosure; the Reactivity tab becomes the "What reacts" page above, and "Modulation slots" / "Triggered envelopes"
move into Advanced › Modulators.

## 8. Suggested order of work

1. Units: `ParamUnit` and formatter in `LabeledSlider` (UI only, no engine change).
2. `TempoTracker` in `core/sound` + new frame fields + `ReactSource`s + a `core/tests` test feeding click tracks at
   90/120/174 BPM and checking tempo ± 1 %, phase within 20 ms, and live vs 1/fps stepping equality.
3. LFO Synced mode, phase, restart; `needsTempo` gating.
4. `ModMatrix` unifying routes, LFO and ADSR targets (with a saved-format migration in `ReactionRouteCodec`).
5. Simple "What reacts" page, long-press "React to…", then the Advanced cables list and grid.
