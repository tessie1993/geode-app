# Customize macros: research

Research for six high-level "macro" controls in front of the ~150 per-style Customize
parameters. Gathered 2026-10-30 from vendor manuals, docs and source. Anything not read in a
primary source is marked **unconfirmed**.

## Summary

Recommended six macros (all bipolar, centre = the style as authored). Candidate names kept except
Speed -> **Motion** (covers drift and camera too), Complexity -> **Detail**, Space/Camera -> **Space**:

1. **Energy**: how hard the picture hits the music (audio drive, reaction strength, bloom, flash, shake).
2. **Motion**: how fast things move (speed, rotation, drift, camera fly speed, breath rate).
3. **Detail**: how busy the frame is (density, symmetry/kaleido folds, tile, warp, particle count).
4. **Colour**: palette temperature and intensity (hue shift, saturation, contrast, palette spread).
5. **Space**: depth and camera (zoom, FOV, orbit radius, trails/decay, vignette).
6. **Chaos**: how unruly it gets (glitch, chromatic aberration, grain, fluid curl/splats, morph).

Key design rules:
- A macro is a **relative offset** around the style's stored values; centre (0) changes nothing.
- Each mapping has its own min/max (amount) and may be inverted; macros never write the detail values.
- Detail sliders show the macro's effect (a ghost/ring) but keep their own value, so "reset macros" is lossless.
- Store the six macro values in presets and templates; double-tap resets to centre; one undo step per gesture.
- Keep six. Ableton shows 8 by default and allows 16 only behind a +/- control; Synesthesia's
  global "Meta" bank is ~15 controls in six groups. Randomize macros (with per-macro lock/exclude) is cheap and proven.

## Decisions (2026-10-30, with the user)

The research leans toward an offset layer. These choices replace it where they differ; the code is
`engine/scenes/.../render/scene/Macros.kt` and `.../render/EventRecipe.kt`.

- **The six:** Energy, Speed, Complexity, **Mood** (dark and moody to bright and euphoric, in place of
  Colour), Space, Chaos. Energy, Speed, Mood and Space are bipolar with the authored look at the
  centre. Complexity and Chaos start at zero, because every parameter they drive already sits at
  its clean end by default.
- **Macros write the real parameters, and the sliders move.** A macro keeps no value of its own. Its
  position is read back from the parameters it drives. So presets, templates, backups, undo, A/B,
  randomize, export and the wallpaper all work unchanged, and old presets need no migration. The
  cost: there is no lossless "reset macros". Undo covers it instead.
- **Proportional, not additive.** Moving a macro from position `f` to `t` moves each target's own
  knob position `k`. Going up, `k + (1 - k)(t - f)/(1 - f)`. Going down, `k * t / f`. This keeps the
  balance between the targets, both ends reach every target's ends, and the read-back is exactly `t`.
  A drag is applied to the parameters the gesture started from.
- **One owner per parameter.** No parameter belongs to two macros, so one knob never drags another.
  A bipolar target with a flat half (vignette, grain, bloom in Mood) is written but never read.
- **Scope.** A macro only touches what `ParamScope` says the style on screen can show.
- **Free for everyone.** Pro stays about styles and export quality.
- **Events are recipes over the reaction matrix.** Each recipe is a set of `ReactionRoute`s, so it
  runs natively, matches in exports and is saved with the routes. Cooldown, quantize and the
  per-event blink are not implemented. Switching style on a drop stays with Scene intelligence (Auto).

| Macro | Drives (low ← default → high) |
|---|---|
| Energy | reaction strength 0.2–1.8, audio drive 0.4–2, motion amount 0.15–1, intensity 0.7–1.2, fluid splat force, glow from loudness, water ripple strength |
| Speed | speed 0.3–2.5, camera fly 0.1–1.4, spin drift, colour cycle speed, dive speed, stirrer speed, water wave speed, cymatics wave flow |
| Complexity | domain warp →0.7, ripple →0.5, morph →0.6, twist →0.5, tile →2.5, trail warp →0.5, fluid curl →48, cymatics field scale →6 |
| Mood | brightness 0.65–1.2, saturation 0.5–1.35, temperature −0.5–0.4, gamma 0.8–1.15, contrast 1.15–0.95; vignette, grain (dark side), bloom (bright side) |
| Space | zoom 1.6–0.7, field of view 0.75–1.4, swing 0.1–0.9, trail length 0.3–0.85, water depth 0.3–0.95 |
| Chaos | turbulence →1.2, sway →0.7, handheld shake →0.5, chromatic aberration →0.6, glitch →0.15, fisheye →0.4 |

## Revision (2026-10-30, later): knobs like Serum 2

The user then asked for the Macros screen to show twist knobs, not sliders, with the title under
each. They wanted a macro editor where the standard macros can be changed or a new set made,
"like macros in Serum 2", and a folder of macro presets. The user also decided:

- **Knobs, Serum-style.** Each knob has any number of destinations. Each destination has its own
  amount: how far a full turn moves it, as a share of its slider range, and negative runs it the
  other way. Serum macros also offset destinations by their own amounts
  ([Serum 2 manual](https://xferrecords.com/web-manual/serum-2/welcome)). The knob's value is now
  stored with the set, not read back. Turning still writes the params, so the sliders follow.
  This replaces the curve model above. The six standard knobs keep the same destinations, with
  the "top" values turned into amounts.
- **Sounds per knob.** A knob can list sounds (Kick, Drop, …) that turn it by themselves. They
  are compiled into reaction routes on the knob's destinations, so they run natively and the same
  way in exports.
- **Any number of knobs** in a set. One active set for all styles; presets do not carry it.
- **Editor:** name, icon, bipolar, destinations (pick from a list, or "record current look"),
  amounts and sounds. **Folder:** save in folders, load, delete, share as text, and paste a shared
  set. The editor and folder are Pro (`ProFeature.ADVANCED_CUSTOMIZATION`); the standard knobs
  are free.
- **Not done:** cooldown and quantize for sounds, `geode://` links for sets, macro sets in
  Settings › Backup, and undo of the knob positions (undo restores the params; the knob stays).

## Findings per tool

### Ableton Live: Rack Macro Controls
- One macro "can address any number of parameters" in the Rack; mapped via Map mode and listed in
  a Mapping Browser. https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/
- Per-mapping **Min/Max** sliders; setting Min above Max inverts the mapping (also "Invert Range"
  in the context menu). Same URL.
- **Absolute**: a mapped parameter "will appear disabled", handing all control to the macro. The
  macro sweeps the parameter between Min and Max. Same URL.
- No curve per mapping (linear Min..Max). **Unconfirmed** that there is no curve option; none is documented.
- Multi-target macros show a generic name and a 0 to 127 scale unless all targets share unit and range. Same URL.
- 8 knobs by default, up to 16 (Live 11 "doubled to 16"), shown or hidden two at a time.
  https://www.ableton.com/en/release-notes/live-11/ ,
  https://help.ableton.com/hc/en-us/articles/360019140859-Live-11-Technical-FAQ
- **Rand** button randomizes mapped macros; per-macro "Exclude Macro from Randomization"; Volume
  macros in Instrument Rack presets are excluded by default. Manual URL above.
- **Macro Variations**: New stores the current macro state as a snapshot; Launch recalls; per-macro
  "Exclude Macro From Variations". Manual URL above.
- Rack presets carry macro mappings (implied by the Volume-exclusion rule above); that the macro
  *values* are restored with a preset is **unconfirmed** from the manual text.

### Resolume Arena: Dashboard
- Dashboard dials sit atop Clip, Layer, Group and Composition panels; you can link "multiple
  parameters to the same dashboard dial", by drag or via the parameter's animation menu.
  https://resolume.com/support/en/dashboard
- Per link: **In/Out point** (limit the parameter's range), **Invert**, and **Dial Range** (which
  part of the dial's travel drives this parameter, e.g. out 0.33 = first third). Dial Range lets one
  dial run parameters in sequence. Same URL.
- Dial takes the parameter's name; rename by double-click. Same URL.
- Whether linked sliders visibly follow, and whether dashboards save into effect presets: **unconfirmed**.

### TouchDesigner: custom parameters
- Components get custom parameters via "Customize..." (Component Editor); each has `normMin`/`normMax`
  (slider range), `min`/`max` with clamps, and `default`. https://docs.derivative.ca/Custom_Parameters
- Internal parameters follow a custom parameter through **Expression** (one-way) or **Bind**, which
  "links parameters together in bi-directional connection". Mode shown as a coloured box
  (grey constant, blue expression, green export, purple bind). https://docs.derivative.ca/Parameter_Mode
- Implication: TD macros are hand-written expressions, so any curve or offset is possible; there is no built-in amount UI.

### MilkDrop / projectM: per-frame variables
- Knob-like per-frame vars: `zoom` (1 = none), `rot` (0 = none), `warp` (0 none, 1 normal, 2 major),
  `decay` (1 = no fade, 0.98 recommended), `cx/cy`, `dx/dy`, `sx/sy`, `echo_zoom`, `echo_alpha`,
  `gamma`, wave and border colours, motion vectors.
  https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html
- Defaults in projectM source: `zoom 1.0`, `rot 0.0`, `warpAmount 1.0`, `warpAnimSpeed 1.0`,
  `decay 0.98`, `gammaAdj 2.0`, `videoEchoZoom 2.0`, `videoEchoAlpha 0.0`.
  https://github.com/projectM-visualizer/projectm/blob/master/src/libprojectM/MilkdropPreset/PresetState.hpp
- Audio vars `bass/mid/treb` where "1 is normal"; `*_att` are smoothed. The guide's idiom centres
  reaction on 1: `zoom = zoom + 0.1*(vol - 1)`. That is a **relative, bipolar** offset around the
  preset's value, the model we recommend. Preset authoring guide above.
- MilkDrop exposes no user macros; all of this is per-preset code. Its vars are the de facto
  "macro vocabulary" for music visualizers: zoom, rotation, warp, decay (trails), echo, colour.

### Synesthesia: Meta controls and scene controls
- **Meta controls** act on the whole output: Master, Invert; Colour (Brightness, Gamma, Contrast,
  Hue, Saturation); Mirror (vertical, horizontal); Audio (**Reactivity**: at 0 the scene is static;
  **Audio Speed**: rate of time-based variables); Limit Colors; Alpha.
  https://app.synesthesia.live/docs/manual/controls.html
- Master brightness is **not** saved in presets; scene controls are. Same URL.
- Each bank has **Lock**, **Default** (reset), **Random**, and **Undo** (reverts the last Random or
  Default, Scene and Meta banks only). Same URL.
- Scene controls are author-defined (slider, knob, toggle, bang, xy, color, dropdown), each with
  `MIN`/`MAX`/`DEFAULT`, `UI_GROUP`, and optional `smooth` variants. There is no standard name set.
  https://app.synesthesia.live/docs/ssf/json.html
- Audio uniforms split into Level, Hits, Presence, Time (band clocks that speed up when loud), Beat,
  BPM and `syn_Intensity`. https://app.synesthesia.live/docs/ssf/audio_uniforms.html

### Magic Music Visuals: Globals
- A project-wide Globals panel; "Global Parameters ... can be used to control all scenes".
  https://magicmusicvisuals.com/screenshots
- Per-link modifiers (Scale etc.) come from forum posts only; the User's Guide returned 403.
  **Unconfirmed**. https://magicmusicvisuals.com/downloads/Magic_UsersGuide.html

### Native Instruments Massive X: Macros
- 16 macros; 1 to 8 also on the Play view. 1:1 mapping (drag onto knob: macro controls the full
  range) or as a **modulation** (drag onto mod slot, drag vertically for amount). A coloured segment
  "around the current knob position" shows the amount, so the macro is a **relative offset** from the
  knob's own value. https://docs.native-instruments.com/online-guides/massive-x-manual/en/macros
- Bipolar knobs run -100% to +100% with centre 0%; double-click a mod slot to set max or reset to zero.
  https://native-instruments.com/ni-tech-manuals/massive-x-manual/en/control-elements
- Mute-all-connections toggle for A/B; macros rename on double-click. Macros page above.
- Assignments live "in the loaded preset"; that macro *positions* save with it is **unconfirmed**.

### Xfer Serum / Arturia Pigments
- Serum: macros route through the mod matrix with a signed amount (drag up from centre = positive,
  down = negative). Source is Output's support article, not Xfer. **Unconfirmed** for Serum 2.
  https://support.output.com/en/articles/10297870
- Pigments: macros M1 to M4 are mod sources assigned "the same way as for any other mod source".
  https://www.arturia.com/api/download/products/pigments/manual/pigments_Manual_7_0_1_EN.pdf
  (page not read in full; **unconfirmed** detail).

### Elektron performance macros
- Analog Four: Performance mode, knobs with several destinations each and per-destination signed
  depth, relative to the sound's values. All of this comes from forum posts; the official manual was
  not retrieved. **Unconfirmed**.
  https://www.elektronauts.com/t/a4-performance-mode-simple-question/3562
- Teenage Engineering: no performance-macro doc found. **Unconfirmed / not researched further.**

### UX guidance
- NN/g progressive disclosure: initially "show users only a few of the most important options";
  more than two disclosure levels "typically have low usability"; labels must set expectations.
  https://www.nngroup.com/articles/progressive-disclosure/

## Design implications

**Absolute vs relative.** Ableton and Resolume (1:1) are absolute: the macro owns the parameter and
the detail control goes dead. That is wrong for us. Users tune detail sliders, then want a macro on
top. Massive X modulation, Serum mod amounts, Elektron depth and MilkDrop's `zoom + k*(x-1)` idiom
are all **relative offsets** around a base. Recommend:
`effective = clamp(base + Σ macro_i × amount_ij × span_j)`, with `macro_i ∈ [-1, 1]`, `amount_ij`
signed (negative = inverted), and `span_j` the param's usable range. For multiplicative params
(zoom, speed) offset in log space so +1 doubles and -1 halves.

**Bipolar, centre neutral.** With relative offsets, 0 must mean "the style as authored". Use a
centre-detented bipolar slider showing fill from the centre (Massive X bipolar knobs run -100..+100
with 0 centre). Double-tap resets to 0 (Massive X resets mod amount by double-click; Synesthesia has
Default per bank). Provide a "Reset macros" action and a Random with **Undo**, as Synesthesia does.

**Visible movement.** Do not write macro output into the detail values. Draw it as a ghost marker or
coloured arc on each affected slider (Massive X's segment around the knob). Detail sliders stay live
and editable. Ableton's "appears disabled" model is the one to avoid.

**Storage.** Save the six macro values with presets, templates and backups (Synesthesia saves scene
controls in presets; Ableton snapshots macro states as Variations). Keep each style's mapping table in
code, not in user data, so it can be retuned without migrating presets. Exception to consider:
Synesthesia keeps Master brightness out of presets; a "performance" macro you ride live may also want
to stay out of presets. Decide per macro. Smoothing: Synesthesia's `smooth` controls ease over
~`1/PARAMS` frames; ease macro changes over ~100 to 200 ms so a drag never jumps the picture.

**How many.** Ableton defaults to 8 visible, 16 behind +/-; Massive X puts 8 of 16 on Play.
Synesthesia's Meta bank is about 15 controls in six groups. Six fits one phone row of two by three, and
leaves room for one disclosure level (NN/g: two levels max): Macros, then "All parameters".

**Randomize.** Ableton Rand randomizes mapped macros with per-macro exclude; Synesthesia Random has
Lock and Undo. A "Surprise me" on six bipolar macros, drawing from about ±0.6 rather than the full
range, gives safe variety without breaking the style.

**Labelling.** Ableton multi-target macros fall back to a generic name and 0 to 127. Our macros
need fixed, human names plus a one-line hint and signed percent readout (e.g. "Energy +35%").

## Recommended mapping table

Amounts are fractions of each param's usable span at macro = ±1 (sign = direction). "log" means
multiplicative (×2 at +1, ×0.5 at -1). Rough starting values to tune per style.

| Macro | +1 means | Params (direction, amount) |
|---|---|---|
| **Energy** | punchier, louder look | audio drive +0.5; reaction strength +0.5; band gains (all) +0.3; bloom +0.35; brightness +0.15; contrast +0.15; camera shake +0.3 (only above 0); motion breath depth +0.3; fluid splats on beat +0.3 |
| **Motion** | faster | speed log ×2; rotation speed log ×2; motion drift +0.4; camera fly speed log ×2; camera orbit speed log ×1.7; motion breath rate +0.3; fluid curl rate +0.2. At -1: near-frozen (also lowers trails decay 0.2 so stills stay crisp) |
| **Detail** | busier, finer | density +0.5; particle count/density +0.5; particle size -0.3; kaleido/symmetry folds +0.4 (stepped, snap to integers); tile count +0.4 (stepped); warp +0.3; ripple frequency +0.3; twist +0.2 |
| **Colour** | warmer, richer | hue shift +0.15 (a gentle rotation, not a full wheel); saturation +0.4; contrast +0.2; palette spread +0.3; at -1 desaturated, cooler and lower contrast, toward monochrome |
| **Space** | deeper, wider | zoom log ×0.6 (pull back); camera FOV +0.3; camera orbit radius +0.3; trails/decay +0.4 (longer echoes); vignette -0.2; MilkDrop-style echo alpha +0.2. At -1: close, flat, tight vignette |
| **Chaos** | unruly | glitch +0.5; chromatic aberration +0.4; grain +0.3; warp +0.3; morph amount +0.3; fluid curl +0.4; fluid pressure -0.2; twist +0.3; motion drift jitter +0.2. At -1: everything below base clamps to 0 (clean, stable) |

Rules for the table:
- Each param may be driven by several macros (warp: Detail and Chaos). Offsets sum, then clamp.
- Params with safety limits (flash rate, shake, strobe-like bloom) clamp to `safety::apply` after
  macros, never before. Energy must not raise photosensitive-risk params past the safety caps.
- Only map params a style actually uses; a macro with no live targets in a style should show as
  disabled, not silently do nothing.

## Sources

- Ableton Live manual, Racks: https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/
- Ableton Live 11 release notes: https://www.ableton.com/en/release-notes/live-11/
- Ableton Live 11 Technical FAQ: https://help.ableton.com/hc/en-us/articles/360019140859-Live-11-Technical-FAQ
- Resolume Dashboard: https://resolume.com/support/en/dashboard
- TouchDesigner Custom Parameters: https://docs.derivative.ca/Custom_Parameters
- TouchDesigner Parameter Mode: https://docs.derivative.ca/Parameter_Mode
- MilkDrop preset authoring guide: https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html
- projectM PresetState.hpp: https://github.com/projectM-visualizer/projectm/blob/master/src/libprojectM/MilkdropPreset/PresetState.hpp
- Synesthesia controls: https://app.synesthesia.live/docs/manual/controls.html
- Synesthesia SSF JSON: https://app.synesthesia.live/docs/ssf/json.html
- Synesthesia audio uniforms: https://app.synesthesia.live/docs/ssf/audio_uniforms.html
- Magic Music Visuals screenshots page: https://magicmusicvisuals.com/screenshots
- Magic User's Guide (403 at fetch time): https://magicmusicvisuals.com/downloads/Magic_UsersGuide.html
- Massive X Macros: https://docs.native-instruments.com/online-guides/massive-x-manual/en/macros
- Massive X Control Elements: https://native-instruments.com/ni-tech-manuals/massive-x-manual/en/control-elements
- Output support on Serum macros (secondary): https://support.output.com/en/articles/10297870
- Arturia Pigments manual: https://www.arturia.com/api/download/products/pigments/manual/pigments_Manual_7_0_1_EN.pdf
- Elektronauts A4 performance mode (secondary): https://www.elektronauts.com/t/a4-performance-mode-simple-question/3562
- NN/g Progressive Disclosure: https://www.nngroup.com/articles/progressive-disclosure/

## Event reactions

Second pass (2026-10-30): how tools bind music **events** to visual **actions**, and which recipes
pay off in music videos. Same rules apply: primary sources cited, gaps marked **unconfirmed**. The
timings in the recipes are our design suggestions, not quoted values.

### Findings per tool

**Synesthesia.** Audio reaches shaders as uniform families: Level (smoothed 0..1 per band), Hits
("spikes" on isolated transients, e.g. `syn_BassHits`), Presence (slow band activity), Time (band
clocks that run faster when loud), Beat (`syn_OnBeat`, `syn_ToggleOnBeat`, `syn_RandomOnBeat`,
`syn_BeatTime`), BPM (`syn_BPM`, `syn_BPMSin`/`Tri`) and song shape (`syn_Intensity`, `syn_FadeInOut`).
https://app.synesthesia.live/docs/ssf/audio_uniforms.html
The authoring guide's examples: `syn_BassHits` drives "a small canvas shake", `syn_HighHits` colour
flashes, `syn_BassTime` advances the camera, `syn_BassPresence` slow colour shifts,
`syn_RandomOnBeat` re-randomizes a position each beat, `syn_BPMSin4` drifts colour with the tempo.
It also asks for subtle amounts and a toggle to switch reactivity off.
https://app.synesthesia.live/docs/ssf/best_practices.html
The global Reactivity slider scales all of it, and at 0 the scene is static.
https://app.synesthesia.live/docs/manual/controls.html
In short: **hits → punch, band time → travel, presence → slow colour, beat → step/random.**

**Resolume.** Any parameter can be driven two ways:
- **FFT:** pick L/M/H or an exact band, then set **Gain**, **Fall** (how fast the value drops after
  a peak) and direction (> or <). The +/- buttons let audio drive *speed* instead of position.
  No attack control is documented.
- **BPM Sync:** a timeline measured in beats and phase-locked to the beat.

https://resolume.com/support/en/parameter-animation
Discrete actions: **Beat Snap** delays clip starts to "the next beat, the next bar, in 2 bars, in
4 bars"; **Auto Pilot** plays the next, a random or a set clip when one ends.
https://resolume.com/support/en/clips
Tempo comes from Tap, Resync and Nudge, or MIDI clock / Link. Automatic BPM detection is not on
that page (**unconfirmed**). https://resolume.com/support/en/bpm

**TouchDesigner.** The `audioAnalysis` palette component outputs Low/Mid/High, spectral centroid,
partial density and **Kick**, **Snare** and **Rythm** detection channels, each with a threshold.
https://derivative.ca/UserGuide/Palette:audioAnalysis
The **Trigger CHOP** turns a threshold crossing into a full envelope (Delay, Attack with linear or
eased shape, Peak hold, Decay to Sustain, Release), with **Re-Trigger Delay** (minimum gap between
triggers) and **Multi Trigger** (overlaps add, are ignored during attack, or restart).
https://docs.derivative.ca/Trigger_CHOP . It is the cleanest public model of our Follow mode.

**MilkDrop / projectM.** Presets read `bass/mid/treb` (1 = normal) and the damped `*_att`, and
preset code centres each reaction on 1.
https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html
The host switches presets on events. All defaults come from parameters.h, linked below.
- **Hard cut:** a beat whose "volume difference" passes `hard_cut_sensitivity` (default 2.0) cuts
  to a new preset, but only after `hard_cut_duration` (default 20 s). Hard cuts are off by default.
- **Soft cut:** otherwise a 3 s blend after `preset_duration` (30 s).
- `beat_sensitivity` defaults to 1.0.

https://github.com/projectM-visualizer/projectm/blob/master/src/api/include/projectM-4/parameters.h
Lesson: **a style switch on a big hit needs a minimum hold time**, or it reads as noise.

**Magic Music Visuals.** Modifiers shape any input: **Peak** passes rises through and smooths only
falls (a VU meter, release only); **Trigger (Integer)** counts up by 1 on each threshold crossing;
**Trigger (Random)** outputs a new random value per trigger. These descriptions come from search snippets of the User's Guide, which returned 403 to direct
fetch, so the wording is **unconfirmed**.
https://magicmusicvisuals.com/downloads/Magic_UsersGuide.html ,
https://magicmusicvisuals.com/forums/viewtopic.php?t=2963
They match our modes: Peak is **Follow**, Trigger (Integer) with Wrap is **Step**, and
Trigger (Random) is a random nudge.

**Ableton Max for Live Envelope Follower.** Gain; **Rise** "smooths the attack"; **Fall** "smooths
the release"; Delay (time or tempo-synced); up to eight mapped parameters. **Modulation** mode
offsets around the base value, which stays editable; **Remote Control** sets it outright between
Min and Max. https://www.ableton.com/en/live-manual/12/max-for-live-devices/
Same lesson as for macros: modulate around the user's value, don't take it over.

**Neural Frames.** Stems (hihats, bass, kick, drums, vocals, toms, snare, other) drive Strength,
pan, zoom and rotations; the **Amplitude** "is added to the target's original value". Suggested
starter: kick or snare → Strength. No attack or threshold settings documented.
https://help.neuralframes.com/en/articles/10552816-what-are-modulations-and-audioreactivity

**banger.show** (music-video tool, closest documented peer to Specterr): band picked on a frequency
graph (Bass for punches, Mids for vocals, Treble for sparkle), Floor/Ceiling thresholds, a smoothing
window, Intensity, Blend Amount, Invert, and Linear / Multiply / Power response.
https://banger.show/guides/audio-reactivity

**Kaiber, Specterr.** No primary documentation of event or envelope settings found. **Unconfirmed.**

**Flash safety.** WCAG 2.3.1 allows no more than three general or red flashes in any one-second
period, unless the flashes are under the flash thresholds or the area limit.
https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html
Any recipe that touches brightness, bloom, palette or style must respect this; the engine's
`safety::apply` is the place to enforce it.

### Patterns across tools

- Every tool offers **continuous follow** (an envelope on a band) and **discrete triggers** (a
  threshold crossing becomes a step, random value or cut).
- Few expose attack: Resolume has only Fall, and Magic's Peak only release. TouchDesigner and
  Ableton expose both. A fast attack with a tunable release is the common core.
- Continuous follows are usually a **relative amount around the base**: Ableton's Modulation mode,
  Neural Frames' Amplitude, MilkDrop centring on 1.
- Discrete actions are **rate-limited**: the Trigger CHOP's Re-Trigger Delay, projectM's hard-cut
  minimum, Resolume's Beat Snap to beat or bar. Our discrete actions need a cooldown and quantization.
- There is **one global reactivity scaler**: Synesthesia's Reactivity, Resolume's master audio gain.

### Recommended recipes (continuous, Follow mode)

Amount is a fraction of the target's usable span at full strength. A / R is attack / release.

| Recipe | When → then | Amount | A / R |
|---|---|---|---|
| **Kick punch** | kick → zoom in + camera push | +0.15 zoom, +0.2 push | 10–20 ms / 200–250 ms |
| **Snare crack** | snare → chroma aberration + glitch | +0.3 / +0.2 | 5 ms / 120 ms |
| **Hi-hat shimmer** | hi-hat → particle size + bloom sparkle | +0.15 / +0.1 | 5 ms / 80 ms |
| **Sub weight** | sub bass → warp + fluid splat force | +0.25 / +0.3 | 40 ms / 400 ms |
| **Loudness glow** | loudness → brightness + bloom | +0.1 / +0.2 | 100 ms / 600 ms |
| **Swell breathe** | swell → camera FOV widen + vignette ease | +0.2 / −0.15 | 300 ms / 1.2 s |
| **Build-up tension** | build-up → rotation speed + shake + grain, rising | +0.3 / +0.15 / +0.2 | 1 s / 300 ms (falls away at the drop) |
| **Flux texture** | spectral flux → ripple / twist | +0.2 | 20 ms / 300 ms |
| **Stereo sway** | pan → camera turn (signed); width → symmetry spread | ±0.15 / +0.2 | 150 ms / 500 ms |
| **Bar wave drift** | bar wave → hue shift | ±0.05 hue (slow swing) | n/a (already smooth) |

### Recommended discrete actions (Step and one-shots)

| Recipe | When → then | Suggested setting | Guard |
|---|---|---|---|
| **Beat step** | beat → rotation steps a fixed angle (Step) | 15–45° per beat, eased over ~120 ms | none needed |
| **Downbeat flip** | downbeat → mirror/kaleido folds +1, wrapping (Step) | cycle 2 → 4 → 6 folds | quantize to bar |
| **Section palette** | section change → next palette, crossfaded | 1–2 s fade | at least 8 s apart |
| **Drop switch** | drop → style switch (or hard cut) | cut on the drop, then a 0.5 s settle | hold at least 20 s (projectM's hard-cut default) |
| **Drop burst** | drop → one-shot bloom + shake + splat | +0.4 bloom, decaying over 600 ms | flash cap |
| **Hit nudge** | onset → random nudge of ±0.1 on one of Detail, Chaos or Colour | decays back over 2–4 s | at most 1 per beat |
| **Pitch hue** | pitch change → hue step | ±0.03 to 0.08 per step | at least 250 ms apart |
| **Timbre turn** | timbre jump → camera turn or new orbit target | 20–40° over 600 ms | at least 4 s apart |
| **Tempo drive** | tempo rise or build-up → global speed multiplier | ×1.0 → ×1.5 over the build | resets at section change |

Rules for discrete actions:
- Each one gets a **cooldown** and an optional **quantize to beat/bar**.
- Brightness, bloom, palette and style changes go through the flash cap (at most 3 per second,
  WCAG 2.3.1).
- Step actions ease rather than jump unless the user picks "Cut".

### UX for the Events list

- **One row per reaction, read as a sentence:** "When Kick → Zoom punch". Use music words (Kick,
  Drop, New section), not uniform names; Synesthesia's `syn_BassHits` is for scene authors.
- **Row controls:** an on/off switch and one **Strength** slider. Make Strength bipolar where it
  makes sense; Resolume and banger both offer Invert.
- **Advanced disclosure:** attack, release, threshold, cooldown and quantize. That keeps to two
  disclosure levels (NN/g).
- **Preview flash:** the event chip blinks when the event fires on the playing track, so users can
  see whether the detector catches their kick before they blame the target. Resolume shows a live
  spectrum under the band selector, and banger a frequency graph, for the same reason.
- **Recipes as presets:** "Add reaction" lists the recipes above first and "Custom" last. A new
  user starts with three on: Kick punch, Section palette, Loudness glow.
- **Global control:** a **Reactivity** slider above the list (0 = static), like Synesthesia's
  meta control, plus a per-row mute for A/B, like Massive X's mute-all-connections.
- **Storage:** save the event list with the preset or template. Keep a style's built-in reactions
  separate, like the style DNA, so "Reset reactions" returns to the style's own.
- **Conflicts:** two reactions on one target add up. Show the summed effect as a ghost on the
  target slider, as for macros.

### Sources (event reactions)

- Synesthesia audio uniforms: https://app.synesthesia.live/docs/ssf/audio_uniforms.html
- Synesthesia SSF best practices: https://app.synesthesia.live/docs/ssf/best_practices.html
- Synesthesia controls: https://app.synesthesia.live/docs/manual/controls.html
- Resolume Parameter Animation: https://resolume.com/support/en/parameter-animation
- Resolume Clips (Auto Pilot, Beat Snap): https://resolume.com/support/en/clips
- Resolume BPM: https://resolume.com/support/en/bpm
- TouchDesigner audioAnalysis palette: https://derivative.ca/UserGuide/Palette:audioAnalysis
- TouchDesigner Trigger CHOP: https://docs.derivative.ca/Trigger_CHOP
- projectM API parameters.h: https://github.com/projectM-visualizer/projectm/blob/master/src/api/include/projectM-4/parameters.h
- MilkDrop preset authoring: https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html
- Magic User's Guide (403 on fetch): https://magicmusicvisuals.com/downloads/Magic_UsersGuide.html
- Magic forum, beat triggers: https://magicmusicvisuals.com/forums/viewtopic.php?t=2963
- Ableton Live 12 Max for Live devices: https://www.ableton.com/en/live-manual/12/max-for-live-devices/
- Neural Frames modulations: https://help.neuralframes.com/en/articles/10552816-what-are-modulations-and-audioreactivity
- banger.show audio reactivity: https://banger.show/guides/audio-reactivity
- WCAG 2.2 SC 2.3.1: https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html
