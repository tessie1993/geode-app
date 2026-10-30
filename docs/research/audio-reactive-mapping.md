# Audio-reactive mapping: how visualizers and VJ tools stay alive

Research note, 2026-10-31. Primary sources only: tool manuals and source code. Tags such as [G1] refer to the
Sources list at the end. Quotes are kept to a few words. Where a page could not be reached, this says so.

## Summary

1. **Normalise against the song itself.** MilkDrop and projectM divide each band by its own ~4 s mean, so `bass` sits
   near 1 whatever the volume [P1]. Synesthesia normalises levels to 0..1 [S1]. Fixed scales go dead on quiet tracks and pin on loud ones.
2. **Every band comes in at least two speeds:** a raw one and a damped one (`bass`/`bass_att` [G1], Level/Presence [S1],
   Lag CHOP with separate rise and fall times [T3]). Beats are found by comparing the fast speed against the slow one.
3. **Every tool separates three signal kinds:** continuous *levels*, *events* (hits and beats, each an impulse with a decay) and
   *clocks* (phase that advances faster when the music is loud: `syn_BassTime` [S1], Speed CHOP [T6]).
4. **Motion should mostly come from integrated or phase signals, not raw levels.** A camera that advances by `syn_BassTime` keeps
   moving forward. A zoom that follows bass only twitches in place [S2].
5. **Events should choose things, not only push them.** Common patterns: a new random value on each beat (`syn_RandomOnBeat` [S1], AVS beat code
   [A1]), a toggle on each beat (`syn_ToggleOnBeat`, Logic CHOP Toggle [T5]), and a counter that selects a target (Count CHOP [T4], AVS Custom
   BPM "Skip" [A2]).
6. **Variety on a phrase timescale comes from switching whole states,** such as presets, banks or layers. Switches happen on a timer with
   random jitter, on a loud transient with an adaptive threshold that doubles after each cut, or on a section. Continuous
   values crossfade and discrete ones snap at the midpoint [M1][M3].
7. **Thresholds adapt and have refractory periods.** Preset authors raise the threshold when a beat fires and let it fall back
   slowly [PR1]. MilkDrop's hard cut doubles its threshold on each cut, which then decays with a 60 s half-life [M3]. The Trigger
   and Count CHOPs both have a "re-trigger delay" [T2][T4].
8. **Each band should get a different role** (bass for scale and push, highs for detail and sparkle, onsets for cuts, slow energy for
   speed and palette). This many-to-many routing is what stops the picture repeating one zoom-and-twist on every beat.
9. **Envelope shape matters as much as amount:** asymmetric lag, overshoot [T3], easing curves including Hold, Bounce
   and Elastic [R2], and peak-hold (Resolume "Fall" [R1]).
10. **Brightness gets its own limiter.** WCAG 2.3.1 allows at most 3 flashes in any second unless the flash is under the area and luminance
    thresholds [W1]. Luminance routes need slow attack, slow release and a flash budget.

---

## 1. MilkDrop 2 (Geiss) and projectM

**Signals.** `bass`, `mid` and `treb` are instantaneous band levels: "1 is normal", below about 0.7 is quiet and above about 1.3 is loud [G1, §3a].
`bass_att`, `mid_att` and `treb_att` are "damped in time" versions, and a value above 1 means the band is spiking [G1, shader inputs].
Other inputs are `time`, `frame`, `fps` and `progress` (0..1 through the preset). Shaders also get `rand_preset` (once per preset) and `rand_frame` (each frame) [G1].
Custom waves read `value1` and `value2` from the waveform or the spectrum [G1, §e].

**How `_att` is computed** (projectM v4.1.7, `src/libprojectM/Audio/Loudness.cpp:31-50` [P1]; identical in MilkDrop3
`code/vis_milk2/plugin.cpp:8735-8779` [M2]):
- The bottom half of the FFT is split into three equal linear thirds, then summed (`imm`).
- `avg` is a one-pole average with an asymmetric rate per 30 fps frame: 0.2 when the level rises and 0.5 when it falls. That gives time constants
  of roughly 20 ms to rise and 50 ms to fall.
- `long_avg` is a one-pole average at 0.992 per frame (0.9 for the first 50 frames), a time constant of about 4 s.
- `bass = imm / long_avg` and `bass_att = avg / long_avg`. Both are self-normalising ratios around 1.0, not absolute levels.
- Rates are made frame-rate independent by raising them to a power (`AdjustRateToFps`).

**Per-frame targets** [G1, §3a]: `zoom`, `zoomexp`, `rot`, `warp`, `cx` and `cy` (pivot), `dx` and `dy` (drift), `sx` and `sy` (stretch), `decay` (feedback fade;
about 0.98 is recommended), `echo_zoom`, `echo_alpha` and `echo_orient` (a second, mirrored copy of the frame), `gamma`, `wave_mode` (8 types, 16 in MD3),
`wave_a`, `ob_*` and `ib_*` (outer and inner borders), the on/off filters `darken`, `brighten`, `invert` and `solarize`, plus `darken_center`, `wrap` and the
`mv_*` motion vectors. Per-vertex code reads `x`, `y`, `rad` and `ang` and can vary any warp target across the screen.

**q-variables.** `q1..q32` (`q1..q64` in MD3 [M1]) are reset every frame to their init values. Per-frame code sets them, and they flow on to
per-vertex code, shapes, waves and shaders, "to bridge the gap" between the variable pools [G1, §3d]. This is the analysis-to-render bus:
compute a feature once in per-frame code and let every stage read it.

**Beat detection in presets.** MilkDrop has no beat variable. Authors write it in expressions using `above()`, `below()`, `if()` and `rand()` [G1, §i]:
- *Fast against slow:* `beat = above(bass, bass_att)`. This is common in the projectM curated pack, for example "suksma - staves" [PR1].
- *Adaptive threshold with decay:* the threshold jumps to 2 when bass crosses it, then relaxes toward 1.3 by ×0.96 per frame.
  This gives a beat gate with a built-in refractory period ("Geometric/Wire Trace/41.milk", per_frame_12 [PR1]). The same pattern appears with
  `beat*4 + (1-beat)*(...)` in "Eyes through the ether" [PR1].
- *Event with decay:* a colour channel jumps to a sine-driven value when `bass > 1.2`, otherwise decays by ×0.85 per frame
  ("crow bowalls2", per_frame_57-59 [PR1]).
- *Sample-and-hold:* when a slow sine crosses 0.9999, new `rand()` values are latched into q-variables and held until the next crossing
  (same preset, per_frame_64-75 [PR1]). AVS's "Random Rotate" does the same thing on each beat [A1].
- Simple scaling: `zoom = zoom + 0.1*(my_volume - 1)` [G1].

**Variation over time.** Presets switch on a timer: 16 s, plus 0 to 10 s of random jitter, with a 2.7 s blend by default
(MD3 `plugin.cpp:940-942`, `milkdropfs.cpp:771-785` [M2]). *Hard cuts* switch on a loud transient. MilkDrop compares bass+mid+treb against 3× a
threshold, which doubles after each cut and decays back toward the base value (base 2.5, half-life 60 s). Hard cuts are off by default
(`milkdropfs.cpp:825-846`, `plugin.cpp:944-946` [M2]). projectM cuts when `vol` jumps from the previous frame by more than
"hard cut sensitivity", and only after a minimum "hard cut duration" (`ProjectM.cpp:116-120`, `TimeKeeper.cpp:52-55`, API `parameters.h:76-118` [P2]).
During a soft blend, continuous per-frame values are interpolated. Booleans and integers (invert, solarize, wrap, echo_orient, wave flags) instead
*snap* at a 0.5 "snap point" (`milkdropfs.cpp:587-618, 727-739` [M2]). When the echo orientation changes, the echo layer fades out to zero before
the snap and back in after it (`milkdropfs.cpp:4066-4080` [M2]). In other words, a discrete state dips through zero rather than popping.

## 2. MilkDrop 3 (milkdrop2077)

The approach is described in my own words; no code is copied. Note: the repo's `code/` tree was last changed in April 2023,
while the README describes v3.37 (2026). Beat-mode presets, double presets and transitions appear **only in the README**. The published source
shows the inherited MilkDrop 2 machinery plus the mash-up tools [M1][M2].

- **Beat-driven preset changes (F8, README l.61-68):** a menu of "hardcut" recipes. Each pairs *one band*, *a threshold* and *a minimum
  delay*. Examples: bass > 1.5 with at least 0.2 s between cuts; treble > 2.9 with 0.5, 1, 3 or 5 s between cuts. One recipe has a
  second, much higher threshold that overrides the delay. One sends extreme bass (> 4.9) to a dedicated preset. Hardcut 7 (v3.31, l.196)
  "injects an effect" on a treble peak and does not change preset. The lesson: the *refractory time* is what sets the character
  (frantic or stately). A tier above the normal threshold can break through the refractory, and an event can switch on an overlay instead of
  replacing the whole look.
- **Mash-up by parts:** a preset is split into five "bins": general post-processing, motion equations, waves and shapes, warp shader and
  composite shader (`plugin.h:392`, `plugin.cpp:4705-4740, 4821-4825` [M2]). Mash-up swaps *one* bin from another preset and keeps the rest.
  Shortcut keys load a random preset but keep the warp shader or the composite shader locked (`plugin.cpp:6369-6420`). "Deep mash-up" swaps all five
  (README l.49). Variety comes from recombining parts more than from new presets.
- **Randomise colours** (`c`, README l.54) rewrites only the colours in the composite shader. The motion stays and the palette changes.
- **Double presets (.milk2, README l.14, 91-100):** two presets render together, mixed by a spatial blend *pattern* such as zoom, side,
  plasma, circle, square, stars, clock or cross, at a fixed blend progress. Pattern randomness is seeded, stored in the file and can be re-rolled
  (`o`). So a transition shape is also used as a lasting compositing look.
- **More slots:** 16 custom shapes and 16 waves instead of 4, and q1..q64 (README l.36-44). Sprites have 6 blend modes, including
  invert and cut-off (l.245).
- **Timing UI:** F3 cycles the auto-change interval in real time (15, 30, 45 s and so on, l.34). The `A`, `Z` and `C` keys step back to the previous random result, so a random choice can always be undone.

## 3. TouchDesigner (CHOP chains)

Signals:
- **audioAnalysis palette component:** Low, Mid and High levels, Kick and Snare detection, "Rythm", spectral centroid, and slow and fast "partials density".
  Each band has Threshold, Smooth, Gain and Add, and Kick, Snare and Rythm have their own thresholds. The page does not define these parameters [T1].
  The page `docs.derivative.ca/Audio_Analysis_CHOP` returned 404.
- **Audio Spectrum CHOP:** magnitude and phase, plus a "High Frequency Boost" for tilting the spectrum. It has no smoothing of its own [T10].
- **Analyze CHOP:** Average, RMS Power, Maximum and peak counts over a channel [T7].

Smoothing:
- **Lag CHOP:** separate lag up and lag down ("a quick rise, and a slow fall"), overshoot up and down, and a limit on slope and acceleration [T3].
- **Filter CHOP:** Gaussian, Box, Edge (high-pass, for change detection), De-spike and One Euro (adaptive: low jitter when slow, low lag when fast) [T8].
- **Slope CHOP:** takes the derivative. The rate of change is a feature in its own right, a rising or falling trend [T9].

Events:
- **Trigger CHOP:** a full delay–attack–peak–decay–sustain–release envelope fired on a threshold crossing. It has separate trigger and release thresholds
  (hysteresis), "retrigger" delay, minimum sustain and attack shape (Linear, Ease In, Ease Out, Ease In Ease Out).
  Its multi-trigger setting is ignore, add or restart [T2].
- **Logic CHOP:** turns a channel into 0 or 1, with Toggle, Rising Edge, Falling Edge and Radio Button options [T5].

Routing and variation:
- **Count CHOP:** counts threshold crossings. It can loop, clamp or zigzag (ping-pong) between limits, has a re-trigger delay and reset conditions,
  and can increase by time [T4]. Count → Select (or an index into a Switch) is the standard "every hit picks the next look" chain.
- **Speed CHOP:** integrates velocity into position, with loop or zigzag limits [T6]. Level → Speed gives a clock that runs faster when the music is louder.
- **Beat CHOP:** outputs ramp, pulse, sine and count channels, Bar, Beat and Sixteenths, ramps per bar and per beat, and a configurable period in beats
  (for example 12 or 3.33). Multiples with Shift Step stagger several channels, and Random Offset and Random Seed give a seeded jitter in phase [T11].

Canonical chain: level → Lag (fast up, slow down) → Trigger (threshold, retrigger) → Count (loop 0..N) → Select or Switch a target.
Beat CHOP count or ramp supplies the bar-locked timing.

## 4. Resolume Arena/Avenue

- **Parameter animation modes:** Timeline (forward, backward, loop, ping-pong, "random frames", one-shot), BPM Sync (the same, timed in
  beats), Clip Position, Crossfader Phase, and Audio Analysis/FFT [R1].
- **FFT:** sources are External, Composition, or Clip/Layer/Group FFT. The **L/M/H** buttons pick a band, and in/out points narrow it.
  **Gain** boosts and **Fall** sets how fast the value drops after a peak [R1]. Forum reports say that from v6 Fall became a peak-*hold* time. That is
  not in the manual and is not verified here.
- **Direction buttons** `>` `<` `-` `+`. The `-` and `+` buttons let audio control animation *speed* rather than value [R1]. This is the "integrate" mode inside a VJ tool.
- **Envelopes:** applied to the parameter's *input phase*, whatever drives it (BPM, FFT, MIDI). Curves include Quadratic, Sine,
  Circular, Exponential, Elastic, Back, Bounce and **Hold**. Keyframes are placed on phase rather than seconds, so one envelope shape works for
  beats or FFT [R2].
- **Discrete parameters** (toggles, dropdowns, colour palettes) can be animated too. Dropdown choices and palette colours sit along the timeline, and
  FFT plus "Color Interpolation" sweeps through a palette [R1]. Audio can therefore choose between discrete states.
- **Dashboard:** one dial linked to many parameters, each with its own in/out range, dial range and invert. The manual suggests stacking
  envelopes and bypasses on one dial to sweep through looks [R3]. This is a one-to-many macro.
- **BPM Phase Lock** can be turned off, which keeps a parameter at the tempo but not on the beat [R1]. Unreached: the manual pages for autopilot and random clip
  triggering were not fetched.

## 5. VDMX

- **Audio Analysis plugin:** input gain plus "any number" of filters. Each filter has its own centre frequency, range, gain and
  smoothing, and each is published as a data source [V1][V2]. Waveform and FFT also come out as greyscale video for shaders [V1].
- **Number FX chains** sit on every receiver. "Fall" makes a level fall slowly after peaks [V3]. A full list of Number FX was not found in the docs.
- **Clock:** Internal, MIDI Follow or BPM Detection, with Tap. It publishes data sources for "individual beats, every downbeat, every
  couple bars" [V1]. Clip triggers can be **quantised** to wait for a data source [V4].
- **LFO:** sine, cosine, random and so on, and piecewise multi-segment shapes. Length is in measures when synced to a clock [V1].
- **Step Sequencer:** tracks of type Index, Number, Boolean or Color, each step-programmed. Interpolation sets how far into a step the value fades to the
  next. Length is in measures under a clock [V1]. This is the main "sequencer step per beat" pattern.
- **Triggering media:** a threshold on any data source, Next/Previous/Random buttons, and quantisation [V4].

## 6. Synesthesia (SSF) — the clearest vocabulary

The audio uniforms [S1], one family per band (whole spectrum, Bass, Mid, MidHigh, High):
- **Level:** 0..1, smoothed "to reduce jitter".
- **Hits:** 0..1, spikes on "isolated transients". Best for drums.
- **Time:** a clock that runs faster when the band is loud. It is not normalised and has the same scale as `TIME`. `syn_CurvedTime` accelerates harder.
  The docs suggest using it for "a camera that moves forward on each kick" [S1].
- **Presence:** "rising or falling action" without reacting to individual hits. A trend signal.
- **Beat:** `syn_OnBeat` (impulse, fast fall), `syn_ToggleOnBeat` (flips 0↔1 with a logistic transition), `syn_RandomOnBeat` (a new random 0..1 each
  beat, logistic transition) and `syn_BeatTime` (+1 per beat).
- **BPM:** `syn_BPM`, `syn_BPMConfidence`, and `syn_BPMTwitcher` (a beat clock that jumps exponentially to the next beat; floor gives a count, fract
  gives the per-beat motion). `syn_BPMSin`/`Tri` come at ×1, ×½, ×¼ and ×⅛ rate.
- **Large features:** `syn_FadeInOut` (music starting or ending) and `syn_Intensity` (builds slowly with the song's intensity).
- **Textures:** `syn_Spectrum` (raw and "Juiced" FFT plus waveform) and `syn_LevelTrail` (history, for trails).

Best practices [S2]: every scene should react to audio, and should go beyond colour flashes. The examples drive camera travel with `syn_BassTime` and
shake with Time × Hits. A dot jumps to a random position on each beat. Too much reactivity is "a nuisance", so offer a toggle.
The meta controls include a global **Reactivity** (overall sensitivity) and **Audio Speed** (rate of the time-based variables). The bank-level
**Random** action sets every control to a new random value and can be undone [S3]. Scripts register `onOffToOn` and
`onChange` handlers and can call `randomizeControl` [S4]. The documented example randomises the hue when bass is loud.

## 7. Others

- **Winamp AVS** (source [A1][A2][A3]):
  - Dynamic Movement has separate *Init*, *Frame*, *Beat* and *Point* code. The Beat code runs only on beat frames, so "pick a new random rotation on
    each beat" is one line of Beat code [A1].
  - *Custom BPM* has Fixed, **Skip** (only every Nth beat, 1..16, with "Skip First Beats") and **Invert** modes [A2]. This is the beat divider.
  - An *Effect List* can be enabled **On Beat** for a set number of frames [A3]. That turns a whole sub-chain on in bursts.
  - Colorfade has an "on beat random" mode.
- **ISF:** `audio` and `audioFFT` inputs arrive as images, one row per channel, with `MAX` setting the width. `MAX`=1 gives overall volume [I1].
- **KodeLife:** "Spectrum Split" (3 configurable bands) and a "Spectrum Full" texture [K1]. No smoothing is documented.
- **Shadertoy:** a 512×2 texture, row 0 the FFT and row 1 the waveform. This comes from community sources only [ST1]; no official page was found.
- **Research:** Hunt & Wanderley, "Mapping performer parameters to synthesis engines" (Organised Sound 7(2), 2002) is the standard reference
  for one-to-one, one-to-many and many-to-one mapping [H1]. Only the abstract was reachable; its claim is that mapping design shapes expressiveness.
  Graf, Opara & Barthet (AES 2021) found feature-driven visuals rated "significantly better at complementing the music" than a
  baseline. Users found the feature terminology hard to follow [GR1].

---

## Reaction targets: who offers what

| Target | MilkDrop/projectM | AVS | TD/Resolume/VDMX | Synesthesia | Geode `LfoTarget` |
|---|---|---|---|---|---|
| Zoom / scale | `zoom`, `zoomexp`, `sx/sy` | Movement | any param | scene | Zoom, Pulse, CamPush |
| Rotate / twist | `rot`, per-vertex `rot` | Dyn. Movement `r` | any | scene | Rotation, Twist, CamRoll |
| Warp / turbulence | `warp` | Dyn. Movement | any | scene | Warp, Turbulence, Ripple, Morph |
| Drift / pivot | `dx/dy`, `cx/cy` | Dyn. Shift | any | scene | DriftX/Y, Sway |
| Feedback decay / echo | `decay`, `echo_*` | Fadeout, Blitter Feedback | Feedback FX | scene | TrailLength, FluidFade |
| Hue shift | wave/border colour, comp shader | Colorfade (on-beat random) | palette animation [R1] | Meta hue, randomizable [S4] | ColorShift, Temperature, Saturation |
| Palette swap (discrete) | MD3 randomise colours [M1] | Color Map | palette dropdown + FFT [R1] | Random bank [S3] | PaletteMix (continuous only) |
| Brightness flash | `gamma`, `brighten` | Brightness | any | scene | Brightness, Intensity, Bloom (floored) |
| Invert / solarize / darken | booleans, snap on blend [M2] | Invert | effects | – | none |
| Mirror / kaleido fold | `echo_orient` | Mirror | effects | Meta Mirror [S3] | Tile |
| Pixelate / posterize | – | Mosaic, Color Reduction | effects | – | Pixelate, Posterize |
| Glitch / RGB split | – | Channel Shift | effects | – | Glitch, ChromaAb |
| Lens | – | Bump | effects | – | Fisheye, Vignette |
| Camera travel / orbit | – | – | 3D comp | `rayOrigin += syn_BassTime` [S2] | CamFly/Orbit/Yaw/Pitch/Fov/Shake |
| Particle / shape burst | custom shapes and waves | Dot Fountain | particles | scene | ParticleSize, FluidForce/Curl |
| Speed / time warp | `time` math | – | Speed CHOP, FFT `+/-` [R1] | syn_*Time, Audio Speed [S3] | Speed (+ Spin mode) |
| Look change (cut) | hard cut, soft blend [M2][P2] | random preset on beat | clip trigger, quantised [V4] | presets | auto-visuals random-on-beat, section staging |
| Overlay burst | MD3 hardcut 7 inject [M1] | Effect List On Beat N frames [A3] | bypass toggles [R1] | – | none |
| Waveform opacity | `wave_a`, alpha by volume (`milkdropfs.cpp:2810`) | – | – | – | – |

## Reaction shapes and modes

| Shape | What it does | Seen in | Geode today |
|---|---|---|---|
| Follow | target = f(level) | every tool | `ReactMode::Follow` + `ModCurve` |
| Asymmetric envelope | fast rise, slow fall | `_att` [P1], Lag CHOP [T3], Fall [R1][V3] | per-route attack/release |
| Overshoot / spring | bounce past the target | Lag CHOP overshoot [T3], Elastic/Bounce [R2] | **missing** |
| Peak-hold | hold a peak for a time, then fall | Resolume Fall (reported v6+) | **missing** |
| Impulse + decay | instant rise, exponential fall | `syn_OnBeat`, preset idioms [PR1] | event sources (0.22–2.5 s) |
| ADSR on a gate | delay, attack, peak, decay, sustain, release | Trigger CHOP [T2] | `AdsrEngine` (2 slots, threshold gate) |
| Adaptive threshold + refractory | threshold jumps after a hit and relaxes | presets [PR1], hard cut [M2], MD3 delays [M1], retrigger [T2][T4] | Step hysteresis 0.5/0.3; peak normalisation; **no min interval** |
| Step / accumulate | each edge adds a fixed amount | Count CHOP [T4], `syn_BeatTime` | `ReactMode::Step` (folded −1..1) |
| Integrate | phase += level·dt | syn_*Time [S1], Speed CHOP [T6], Resolume `+/-` | `ReactMode::Spin` (folded) |
| Sample-and-hold random | new random value per event, glided | `syn_RandomOnBeat`, AVS Beat code [A1], `rand()` latch [PR1] | LFO `Random` wave is time-based only; **no per-event S&H** |
| Toggle | flips state on each event | `syn_ToggleOnBeat`, Logic Toggle [T5] | **missing** (Step cycles through 4 states) |
| Divider / counter select | every Nth event; index into a list | AVS Custom BPM Skip [A2], Count CHOP loop/zigzag [T4] | **missing** |
| Sequencer | per-beat or per-step pattern of values | VDMX Step Sequencer [V1], Beat CHOP count [T11] | **missing** |
| Beat-locked oscillator | sine, triangle or ramp at beat and bar multiples | `syn_BPMSin2/4/8` [S1], Beat CHOP [T11] | BeatOsc, BarOsc (cosine ×1 only) |
| Crossfade between states | blend continuous values, snap discrete ones at midpoint | MilkDrop blend [M2], MD3 .milk2 patterns [M1] | `Transition` (Cut/Fade/Melt/Slide/Zoom) for scenes |
| Trend | sign of a slow slope | Presence [S1], Slope CHOP [T9] | Buildup (one-sided) |

## Mapping patterns that recur

- **Bass → scale, push and feedback zoom; highs → detail, sparkle, edge and chroma; mids → rotation and hue.** Every tool splits by band
  (3 bands in MilkDrop [P1], L/M/H in Resolume [R1], 4 bands plus the whole spectrum in Synesthesia [S1]). Giving each band a different role, and
  a different shape, removes most sameness.
- **Onsets and hits → discrete things:** a cut, a palette or position chosen at random, a step, a burst. **Sustained energy → speed** (an integrated
  clock). **Beat phase → periodic motion** that can anticipate the downbeat. **Sections and drops → global state:** palette, camera, scene or bank.
- **Smoothing by target type:**
  - Geometry can take a fast attack (~20 ms, MilkDrop's `_att`).
  - Luminance needs slow attack, slow release and a flash budget.
  - Camera and position want integration or a spring rather than a raw level, or they jitter.
  - Hue wants steps or sample-and-hold with a glide rather than follow, which only wobbles.
- **Phrase-level variation:**
  - Change *what reacts* every N bars or at each section: VDMX clock "every couple bars" [V1], Count → Select [T4], MD3 bin swaps [M1].
  - Re-roll random choices on a beat or a phrase, from a seed so the result can be reproduced (Beat CHOP Random Seed [T11]; MD3 stores blend randomness in the file [M1]).
  - Alternate with toggles and counters.
  - Hold a choice for a minimum time (MilkDrop hard-cut duration [P2]; MD3 per-recipe minimum delay [M1]).
- **Two-tier thresholds:** a normal trigger with a refractory period, plus a much higher "extreme" tier that may break the refractory or pick a special
  look (MD3 hardcut 4 and 6 [M1]).
- **Global sensitivity:** one master scale for reactivity and one for clock speed (Synesthesia Reactivity and Audio Speed [S3]). Geode's `reactAmount` covers the first.
- **Safety:**
  - WCAG 2.3.1 (Level A) allows at most 3 flashes in any 1 s. A general flash is an opposing pair of luminance changes of ≥10% where the darker
    state is < 0.80, over an area larger than about 25% of a 10° field (341×256 px at 1024×768). Saturated-red flashes count separately [W1].
  - The ITU-R BT.1702 broadcast definition is a 20 cd/m² change [W1]. The Harding FPA test that broadcasters used is cited on the WCAG page, but its own
    documentation was not reached.
  - Invert, solarize and full-frame brightness or strobe are the risky targets. Geometry and hue are the safe ones (Geode's `lib_react.glsl` header already says so).

---

## Why every Geode style looked the same (audit, before #145 and #148)

The per-frame chain is `Lfo` → `Adsr` → `Reactions` (the reaction matrix) → `MotionField` → `safety::apply` → `CameraRig`. Each stage
was sound on its own. The sameness came from the defaults that fed it:

- **One recipe for every style.** The old Kotlin `BuildStyleReactionsUseCase` gave every style the same *kinds* of route: kick → `CAM_PUSH`
  (a flat zoom), a band → `ZOOM`, and a hit → `MORPH`, `TWIST`, `WARP` or `RIPPLE`, plus `ROTATION` on styles that spin. The seed changed only the amounts and
  which hit target was picked, so every style pushed in on the kick and twisted on the hit.
- **The same answer for the whole song.** Every route was `Follow` or `Step` on `Beat` or `Kick`, with nothing keyed to bars, phrases or sections, so
  minute three reacted exactly like minute one.
- **Zoom counted twice.** `MotionField` breath also scales zoom with `bassRel`, so even styles whose DNA avoided zoom still pumped on the bass.
- **Colour, brightness and the camera's look were never beat targets,** although the research shows them to be the most varied answers (a hue jump, a bloom,
  a new angle).

Fixed by:
- **#145 (Jump mode, phrase sources):** `ReactMode::Jump` is the "Hold mode" in item 1 below. It sets a new seeded value on each hit. The PR also counts
  phrases (8 bars, or a section change), with `PhraseBeatA`/`PhraseBeatB`, `Backbeat` and `Phrase` as sources.
- **#148 (style personalities in C++):** `core/viz/ReactionDna` gives each style a beat personality (Pulse, Chroma, Flash, Twist, Warp, Ripple, Glitch,
  Swing, Lens, Spin, Morph) and a second personality from another group for alternate phrases. Layers are drawn so they do not repeat the beat. Zoom-and-twist
  is now one personality among eleven.

---

## Implications for Geode

What `core/viz/Reactions.{hpp,cpp}`, `Lfo`, `Adsr`, `VisualSafety` and `AutoVisualsController` already cover:

| Researched idea | Geode piece |
|---|---|
| Self-normalising levels (`bass/long_avg`) | `ReactSignals::normalised`: running peak with a 6 s decay → 0..1 (peak-based, not mean-based); `Swell` = level against a 20 s mean (the closest match to MilkDrop's ratio) |
| Fast and slow speed of each band | per-route attack/release one-pole (`Reactions.cpp` tick) = Lag CHOP up and down |
| Hits / OnBeat | Kick, Snare, Hat, Onset, Beat, Downbeat with exponential decay |
| syn_Time / Speed CHOP | `ReactMode::Spin` |
| syn_BeatTime / Count | `ReactMode::Step`; `onsetCount` and `sectionCount` (shader seeds via `uHitCount`, `uSectionCount`) |
| syn_BPMSin, Beat CHOP ramp | BeatOsc, BarOsc, gated by `pulseConfidence` (≈ `syn_BPMConfidence`) |
| syn_Intensity / FadeInOut | MacroEnergy / Arrival, Buildup, Drop |
| Trigger CHOP ADSR | `AdsrEngine` (gate threshold, retrigger, sustain-track) |
| Time-based S&H | `LfoWave::Random` (new value per LFO cycle) |
| Layered mapping (route → LFO rate or depth) | `Lfo1Rate…Lfo3Depth` chain targets |
| Hard cuts / random preset on beat, section changes | `AutoVisualsController` random-on-beat, section staging, viz playlist |
| Flash limit | `safety::FlashBudget` (3 edges per rolling second), luminance attack/release floors (0.08 s and 0.35 s) |
| Global reactivity | `SceneParams.reactAmount` scales every route |

What is missing, ordered by how much variety each would add for the cost:

1. **Hold mode: sample-and-hold random on each event.** *Done as `ReactMode::Jump` (#145).* On each rising edge, set the target to a seeded random value in −amount..amount, then glide
   with attack and release (`syn_RandomOnBeat`, AVS Beat code, the `rand()` latch idiom). Seed it from the event count and the route index so export
   matches the screen. Hue, drift, camera yaw and the kaleido pivot become "a new place each hit" instead of the same push each hit.
2. **Toggle mode.** Flip between 0 and amount on each edge (`syn_ToggleOnBeat`, Logic Toggle). Step cannot do this: an amount of 1 folds through
   0, 1, 0, −1, and an amount of 2 folds to 0 every time.
3. **Divider and refractory on each route.** "Every Nth event" plus "skip first N", and a minimum interval in seconds or beats (AVS Skip, Count CHOP,
   Trigger retrigger, MD3 per-recipe delays). This turns Beat into half-time or per-bar and stops strobing Step routes. Downbeat only partly covers it.
4. **Route banks rotated on a phrase.** *Partly done: the DNA answers the beat differently in alternate phrases (#148).* Two or three route sets per style, switched every N bars or on `Section`. When a bank switches, continuous outputs crossfade and discrete
   ones snap or dip through zero at the midpoint (the MilkDrop snap point). This is the "change what reacts every phrase" pattern. `sectionCount` already
   exists, but routes cannot read it. Cheaper first step: let Section or Downbeat re-roll *which target* a route drives from a short, style-approved list.
5. **Unfolded clocks for shaders.** Spin folds into a −1..1 triangle, which suits bounded parameters but cannot give an ever-advancing phase. Publish
   per-band `syn_*Time`-style clocks (bass, mid, high and whole-spectrum time, and beat time) in `lib_react.glsl`. Shaders could then drive travel and texture
   phase the way Synesthesia scenes do. Keep them fmod-wrapped with a large period.
6. **Sequencer source.** A per-style 8- or 16-step pattern indexed by beat within the bar or phrase (VDMX Step Sequencer, Beat CHOP count) with a
   glide parameter. It gives designed, groove-locked variation that the analyser cannot invent.
7. **Shape options:** spring or overshoot smoothing (Lag CHOP overshoot, Elastic and Bounce) for camera and scale; peak-hold before release;
   attack *shape* (ease) on events, which currently rise instantly.
8. **More beat oscillators and a Trend source.** Beat and bar oscillators at ×½, ×¼ and ×⅛ (`syn_BPMSin2/4/8`) or a ramp shape, plus a bipolar Trend
   source (slope of a 2–4 s average, like Presence or Slope CHOP).
9. **Partial look swaps at sections (MD3 mash-up by bins).** Keep the scene and swap only the palette, only the reaction DNA, or only the camera
   behaviour. Auto-visuals today changes the whole style.
10. **Two-tier event thresholds.** Drop and Arrival exist. A per-route "extreme" tier above the normal threshold could break through the refractory or switch to an
    alternate target (MD3 hardcuts 4 and 6).

Keep: luminance targets stay behind `FlashBudget` and the attack/release floors. Any new Toggle, Hold or Sequencer route aimed at
Brightness, Intensity or Bloom must go through the same floors, because a toggle on a luminance target is a strobe by construction.

---

## Sources

- [G1] Geiss, *MilkDrop preset authoring guide*: https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html (§3a per-frame vars, §3b per-vertex, §3d q vars, shader inputs, §i function reference)
- [P1] projectM v4.1.7, `src/libprojectM/Audio/Loudness.cpp:31-50`, `FrameAudioData.hpp`: https://github.com/projectM-visualizer/projectm/blob/v4.1.7/src/libprojectM/Audio/Loudness.cpp (read from local submodule `third_party/projectm`, tag v4.1.7)
- [P2] projectM v4.1.7, `src/libprojectM/ProjectM.cpp:116-120`, `TimeKeeper.cpp:52-55`, `src/api/include/projectM-4/parameters.h:56-158`: https://github.com/projectM-visualizer/projectm/tree/v4.1.7/src
- [PR1] projectM curated presets: https://github.com/projectM-visualizer/presets-cream-of-the-crop ("Geometric/Wire Trace/41.milk", "Geometric/Sphere Wild/suksma - crow bowalls2.milk", "Reaction/Whirlpools/suksma - staves.milk", "Particles/Points Trails/rarian rakista - Eyes through the ether.milk")
- [M1] MilkDrop3 README (v3.37): https://github.com/milkdrop2077/MilkDrop3/blob/main/README.md (lines 14-19, 34-72, 91-100, 196, 245)
- [M2] MilkDrop3 source (`code/` last updated 2023-04-17): https://github.com/milkdrop2077/MilkDrop3/tree/main/code/vis_milk2 (`milkdropfs.cpp:587-618, 727-739, 771-846, 2810, 4066-4080`; `plugin.cpp:940-946, 4705-4825, 6369-6420, 8735-8779`; `plugin.h:392-393`)
- [M3] Same hard-cut logic as [M2] `milkdropfs.cpp:825-846`, inherited from MilkDrop 2.
- [T1] TouchDesigner audioAnalysis palette: https://derivative.ca/UserGuide/Palette:audioAnalysis (`docs.derivative.ca/Audio_Analysis_CHOP` returned 404)
- [T2] Trigger CHOP: https://docs.derivative.ca/Trigger_CHOP
- [T3] Lag CHOP: https://docs.derivative.ca/Lag_CHOP
- [T4] Count CHOP: https://docs.derivative.ca/Count_CHOP
- [T5] Logic CHOP: https://docs.derivative.ca/Logic_CHOP
- [T6] Speed CHOP: https://docs.derivative.ca/Speed_CHOP
- [T7] Analyze CHOP: https://docs.derivative.ca/Analyze_CHOP
- [T8] Filter CHOP: https://docs.derivative.ca/Filter_CHOP
- [T9] Slope CHOP: https://docs.derivative.ca/Slope_CHOP
- [T10] Audio Spectrum CHOP: https://docs.derivative.ca/Audio_Spectrum_CHOP
- [T11] Beat CHOP: https://docs.derivative.ca/Beat_CHOP
- [R1] Resolume, Parameter Animation: https://resolume.com/support/en/parameter-animation (Timeline, BPM Sync, Audio Analysis, Animate Everything, Start Settings)
- [R2] Resolume, Envelopes: https://resolume.com/support/en/envelopes
- [R3] Resolume, Dashboard: https://resolume.com/support/en/dashboard
- [V1] VDMX plugins reference: https://docs.vidvox.net/vdmx/vdmx_plugins.html (Audio Analysis, Step Sequencer, LFO, Clock)
- [V2] VDMX, Audio Analysis Basics: https://vdmx.vidvox.net/tutorials/audio-analysis-basics
- [V3] VDMX, "Falling" audio level with Number FX: https://vdmx.vidvox.net/tutorials/creating-a-falling-audio-level-data-source-using-number-fx-chains-in-vdmx
- [V4] VDMX, triggering a movie from a data source: https://vdmx.vidvox.net/tutorials/using-audio-analysis-or-other-data-source-to-trigger-a-movie
- [S1] Synesthesia, Audio Reactive Uniforms: https://synesthesia.live/docs/ssf/audio_uniforms.html
- [S2] Synesthesia, Best Practices: https://synesthesia.live/docs/ssf/best_practices.html
- [S3] Synesthesia, Controls & Interface: https://synesthesia.live/docs/manual/controls.html
- [S4] Synesthesia, JavaScript Scripting: https://synesthesia.live/docs/ssf/script.html
- [A1] AVS source, Dynamic Movement examples: https://github.com/grandchild/vis_avs/blob/main/avs/vis_avs/e_dynamicmovement.h (example struct l.24-56)
- [A2] AVS source, Custom BPM: https://github.com/grandchild/vis_avs/blob/main/avs/vis_avs/e_custombpm.h
- [A3] AVS source, Effect List and Colorfade: https://github.com/grandchild/vis_avs/blob/main/avs/vis_avs/e_effectlist.h, `e_colorfade.h`
- [I1] ISF JSON reference (audio inputs): https://docs.isf.video/ref_json.html
- [K1] KodeLife built-in parameters: https://hexler.net/kodelife/manual/parameters-built-in
- [ST1] Shadertoy audio texture (community, unofficial): https://gist.github.com/soulthreads/2efe50da4be1fb5f7ab60ff14ca434b8
- [W1] W3C, Understanding SC 2.3.1: https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html
- [H1] Hunt & Wanderley 2002, Organised Sound 7(2) 97-108, doi:10.1017/S1355771802002030: https://idmil.org/?p=2858 (abstract only)
- [GR1] Graf, Opara, Barthet, "An Audio-Driven System For Real-Time Music Visualisation", AES 150 (2021): https://arxiv.org/abs/2106.10134
- Geode code read: `core/viz/Reactions.hpp`, `Reactions.cpp`, `Lfo.hpp`, `Lfo.cpp:165-182`, `Adsr.hpp`, `VisualSafety.hpp`, `app/src/main/assets/shaders/lib_react.glsl`, `app/src/main/java/dev/geode/ui/AutoVisualsController.kt`.
