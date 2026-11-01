# Modulation matrix: a patch bay that fits a phone

Research note and UX spec, 2026-11-01. The owner's decisions are in [`MOD_MATRIX_PLAN.md`](MOD_MATRIX_PLAN.md) §1;
where this note's research (§1–§5) recommends otherwise, the plan wins. §6 is the agreed UX. It follows [`REACTIVE_VISUALS.md`](REACTIVE_VISUALS.md), which already proposes one native
`ModMatrix`, tempo-synced LFOs and a built-in tempo tracker (its §5, §5.1, §5.2). This note does not repeat that work. It
covers what the owner asked for next: a Pro **patch bay** under Customize › Advanced, with cables, more modulators (6 LFOs,
4 ADSHR envelopes, 2 step sequencers), modulators that modulate modulators, several tempo sources, and MIDI. All of it has
to stay easy to use on a 6-inch phone screen.

The research was cut short when the session was handed off. Items marked **Not yet researched** were not checked on the web.
Items marked **(from memory)** come from prior knowledge and were not verified in this session. Check them before building
on them.

## 1. What the engine has today (read from the code)

| Piece | File | Today | Gap for the matrix |
|---|---|---|---|
| Reaction routes | `core/viz/Reactions.hpp` | 12 routes, 117 `ReactSource`s (#211 adds more), 5 modes (Follow, Step, Spin, Jump, Toggle), amount −2..2, curve, attack/release. The style DNA fills up to 9 of them. | Destinations are `LfoTarget` only. Sources are sound only. |
| LFOs | `core/viz/Lfo.hpp` | 3 slots, wave (sine, triangle, saw, square, random), `rateSeconds` 0.05–60 s, depth, polarity, curve, one target each. The chain targets are `Lfo1Rate…Lfo3Depth`. | The owner wants 6. There is no tempo sync, phase or retrigger. Each slot owns its target, so it is not a source. |
| Envelopes | `core/viz/Adsr.hpp` | 2 ADSRs gated by a band over a threshold, several targets each, able to offset the LFOs' rate and depth. | The owner wants 4, with Hold (ADSHR). The gate is a fixed band, not any signal. |
| Macros | `core/viz/Macros.hpp` | 6 knobs from −1 to 1 with their own 7-route engine. | They are not offered as sources yet. |
| Destinations | `LfoTarget` (≈ 54 entries, `Glitch` retired in #216) vs `SceneParams::fieldNames()` (`core/viz/ParamsFields.cpp`, 154 fields) | Only about a third of the parameters can be modulated. | "Every visual parameter" needs a destination table generated from the field list and the catalogue's ranges and `ParamScope`. |
| Frame order | `core/viz/RendererFrame.cpp` | ADSR → LFO (rate and depth from the envelopes) → macros → LFO apply → ADSR apply → reactions → macro reactions → … → `safety::apply`. | The order is fixed. A modulator cannot hear a later one. |
| Saved form | `engine/scenes/.../render/ReactionRouteCodec.kt` | Plain text `reactions/2`, pure Kotlin, forgiving decode. | Copy this style for a `modmatrix/1` file. |
| UI slot | `ui/CustomizeAdvanced.kt` | `AdvancedSection.MODULATION_MATRIX` is reserved (`available = false`). Advanced is already Pro-gated. | The page itself. |
| Tempo | `core/sound/Rhythm`: a phase-locked beat clock in every frame (`bpm`, `beatPhase`, `barPhase`, `phrasePhase`, `beatConf`); `MotionField` and the director already move on it. | The `Lfo.kt` KDoc ("no tempo sync on purpose") predates it and is stale. Keep its worry as a rule: a synced LFO never jumps (plan §3). |
| BPM tags | `core/library/Tags.hpp` (TagLib) | Reads title, artist and other tags. BPM is not read. | TagLib's property map has a unified `BPM` key (ID3 `TBPM`, MP4 `tmpo`, Vorbis `BPM`) **(from memory)**. |
| Network | `AndroidManifest.xml` | INTERNET is removed explicitly. | MIDI needs no network. BLE MIDI does need Bluetooth permissions (§5.3). |

ADR 0003 applies. Every modulator, the matrix, the tempo tracker and the MIDI clock maths belong in C++ (`core/viz`,
`core/sound`). Kotlin draws the UI, saves the patch and hands MIDI bytes to native.

## 2. How other tools patch (findings)

### 2.1 Touch patch environments

- **Moog Model 15 (iPad/iPhone).** You drag from a jack to make a cable, and every valid destination lights up while you
  drag. **Double-tapping the source and then the destination makes a cable without dragging.** A second finger can pan the
  panel during a long drag. Holding a jack and sliding slightly opens a colour arc to recolour the cable. The whole panel
  fits in portrait. ([MusicRadar review](https://www.musicradar.com/reviews/tech/moog-model-15-app-638933),
  [Sonicstate](https://www.sonicstate.com/news/2016/05/03/moog-release-full-modular-app/))
- **miRack (VCV Rack port, iPad and iPhone).** Its developer says touch suits cables better than a mouse. A tap on a port
  cycles through the cables on it, and you then drag or recolour the highlighted one. Touching a port no longer pulls a
  cable out by accident. ([MusicTech](https://www.musictech.net/news/mirack-modular-ipad-synth/),
  [The Beat Community, 2023 update](https://thebeatcommunity.com/2023/09/13/mifki-ltd-update-mirack/),
  [miRack on iPhone](https://mifki.com/blog/mirack-is-now-available-on-iphone))
- **VCV Rack 2.5.** You can stack several cables on one input, and their values are summed. A Cable colours menu edits the
  palette and can turn off automatic colour rotation.
  ([Rack 2.5 notes](https://vcvrack.com/news/2024-04-21-Rack-2.5), [CDM](https://cdm.link/2024/04/vcv-rack-2-5/)).
  The engine processes modules one sample at a time, so a feedback loop costs one sample, not one buffer.
  ([VCV forum](https://community.vcvrack.com/t/feedback-networks-in-vcv-rack/8190))
- **AUM (iOS).** Routing is a **matrix of tap targets**: you tap a cell to connect a source to a destination, and a thin
  line confirms it. MIDI has its own matrix, with sources on top and destinations down the side. In apeMatrix, a similar
  app, you set each connection's gain by tapping its node and dragging.
  ([Dubspot review](https://blog.dubspot.com/ios-music-app-review-aum-pt2),
  [Violet Recording](https://violetrecording.com/how-to-use-aum-on-ios/),
  [Sonicstate on apeMatrix](https://www.sonicstate.com/news/2018/06/11/apematrix-a-routing-app-for-ios/))
- **Drambo (iOS).** It has **no cables**: modules connect by their order in a rack, and a modulation knob appears next to
  a parameter only when you need it. Several modulators can drive one parameter without a mixer module. A user tip says a
  small triangle marks a parameter you can modulate, and a hold opens the options.
  ([Beepstreet](https://www.beepstreet.com/ios/drambo/),
  [Elektronauts thread, user-written](https://www.elektronauts.com/t/drambo-ios/126913?page=5))
- **Audulus 4.** You can pinch to zoom while dragging a connection, so a long cable never needs a second gesture. An iPhone
  version followed in 4.7. ([Sonicstate](https://sonicstate.com/news/2022/12/15/modular-synthesis-app-updated),
  [Synth Anatomy](https://synthanatomy.com/2025/12/audulus-4-major-step-forward-for-the-ios-and-macos-modular-synth-with-auv3.html))
- **Empress ZOIA (hardware).** The patch is an 8×5 grid of buttons, one page at a time. To connect, you **hold an output
  button and press an input button**, and the screen shows the parameter. This is a proof that cable-free patching works
  on a small grid. ([ZOIA manual](https://empresseffects.com/cdn/shop/files/ZOIA_Manual_1_dot_13.pdf),
  [Perfect Circuit](https://www.perfectcircuit.com/signal/empress-effects-zoia-euroburo))

### 2.2 Desktop modulation norms

- **Bitwig.** Each device has its own modulator slots. You arm a modulator's routing button, then drag on a target
  parameter to set how far it moves. A multi-target mode lets you pick many destinations, each with its own amount. A
  modulator can modulate another modulator.
  ([Bitwig user guide](https://www.bitwig.com/userguide/latest/the_unified_modulation_system/),
  [Admiral Bumblebee](https://admiralbumblebee.com/music/2017/06/23/Bitwig-Modulators.html))
- **Vital / Serum / SynthMaster / Ultra.** You drag a source onto a knob. A ring around the knob shows the range, and you
  drag a small handle to set the amount. The matrix tab lists every route with an amount slider and a remap curve. An
  **aux** source multiplies a route's amount.
  ([Vital forum](https://forum.vital.audio/t/controlling-lfos-opacity-with-a-macro/7057),
  [Attack Magazine on Serum](https://www.attackmagazine.com/technique/synth-secrets/modulating-serums-fx-beat-pulsing-plucks/),
  [Ultra mod matrix](https://ultra.audio/help/synth/matrix/matrix))
- **Ableton Live.** Its LFO, Shaper and Envelope Follower devices each map to several parameters through a Map button, and
  Macro Variations store and recall knob sets (Push can randomise them). Only secondary sources were found.
  ([Isotonik listing](https://isotonikstudios.com/product/neuraloot-by-sonus-dept),
  [Synthtopia on Variation Stepper](https://synthtopia.com/?p=123162))
- **Elektron.** LFO trig modes are Free, Trig (restart), Hold (sample on trig), One (single cycle) and Half (half cycle).
  The step sequencer has trig conditions (probability, A:B cycles, Fill), parameter locks and slides. Users report edge
  cases, such as a slide still happening when the next trig's condition fails. Only forum sources were found.
  ([Elektronauts: LFO](https://www.elektronauts.com/t/your-best-lfo-tips-on-the-digitone/164098/21),
  [Elektronauts: conditions and slides](https://www.elektronauts.com/t/conditional-trigs-parameter-slides/89094),
  [CDM: trig conditions](https://cdm.link/new-elektron-octatrack-os-trig-conditions-awesome/))

### 2.3 VJ and visual tools

- **TouchDesigner.** You drag a CHOP channel onto a parameter, and the parameter turns green to show it is driven. The
  Beat CHOP turns tapped beats into a running ramp or pulse. The LFO CHOP offers the standard waves.
  ([CHOP Export](https://docs.derivative.ca/CHOP_Export), [Beat CHOP](https://docs.derivative.ca/Beat_CHOP),
  [LFO CHOP](https://docs.derivative.ca/LFO_CHOP))
- **VDMX.** Audio Analysis, LFO and Step Sequencer plugins each **publish data sources**. Any slider, button or menu can
  take one from its inspector. This is the "everything is a source, everything is a destination" model.
  ([Data sources overview](https://vdmx.vidvox.net/tutorials/overview-of-available-data-sources-in-vdmx),
  [plugins](https://docs.vidvox.net/vdmx_plugins.html))
- **Resolume.** A parameter runs in one of several modes: FFT, BPM Sync (a number of beats, loop or ping-pong, in and out
  points) or Envelope (a drawn shape you can save as a preset). Resolume does not detect tempo, and users tap instead (see
  REACTIVE_VISUALS §2.1). A parameter's speed is a property, not a parameter, so dashboard knobs cannot drive it. Avoid that
  limit: make modulator settings real destinations.
  ([Parameter animation](https://resolume.com/support/en/6/parameter-animation),
  [forum on speed](https://resolume.com/forum/viewtopic.php?p=98735))
- **Magic Music Visuals.** Nearly every parameter takes audio or MIDI through a chain of **modifiers** (threshold,
  smoothing, expression). Expressions cannot keep state.
  ([Wikipedia](https://en.wikipedia.org/wiki/Magic_Music_Visuals), [forum](https://magicmusicvisuals.com/forums/viewtopic.php?p=7019))
- **Synesthesia.** It sends about 41 audio values over OSC, and its controls are MIDI-mappable. Its LFO assignment UI is
  **Not yet researched**. ([Troikatronix forum](https://community.troikatronix.com/post/49692))

### 2.4 Eurorack conventions

- There is **no standard for cable colours**. Community schemes group them as cold colours for gates and triggers, warm
  colours for CV, and hot colours for audio. A simpler scheme is red for audio, yellow for CV, blue for modulation and green
  for clocks and gates. LED cables show polarity, green for positive and red for negative.
  ([VCV forum](https://community.vcvrack.com/t/cable-color-convention/5302),
  [Cherry Audio forum](https://forums.cherryaudio.com/viewtopic.php?p=3948),
  [Perfect Circuit](https://www.perfectcircuit.com/lablab-audio-led-cable.html))
- Multiples (one output to many inputs) and attenuverters (scale by −1…+1 at the input) are standard practice in modular
  synths **(from memory; no source checked)**. In software, every cable can carry its own attenuverter (its amount), and an
  output can fan out freely. Stacking at an input sums, as VCV Rack does.

### 2.5 Accessibility

- **WCAG 2.2 SC 2.5.7 (AA): anything done by dragging must also work with a single pointer without dragging**, for example
  tap-then-tap or a text input. ([W3C Understanding 2.5.7](https://www.w3.org/WAI/WCAG22/Understanding/dragging-movements))
- On Android, `AccessibilityActionCompat` defines drag start, drop and cancel. In Compose, **custom accessibility actions**
  appear in TalkBack's actions menu, so "Connect to…", "Disconnect" and "Change amount" can be actions on each jack and each
  cable. Give every action a clear label.
  ([AccessibilityActionCompat](https://developer.android.com/reference/kotlin/androidx/core/view/accessibility/AccessibilityNodeInfoCompat.AccessibilityActionCompat),
  [Compose issue 156468846](https://issuetracker.google.com/issues/156468846))

## 3. Modulator design norms (what to build)

All modulators run once per rendered frame (control rate), in C++, with export stepping at dt = 1/fps, so live equals
export.

**LFO (×6).** Waves: sine, triangle, ramp up, ramp down, square (with pulse width), sample-and-hold random, smooth random.
Settings:
- Rate: **Free** (0.01–30 Hz, shown as "every 12 s" when slow) or **Synced** (1/32 … 8 bars, with triplets and dotted values;
  the list is in REACTIVE_VISUALS §5.1).
- Phase: 0–360°.
- Polarity: bipolar or unipolar.
- Restart: Free / Trig / Hold / One / Half, following Elektron. Trig, Hold, One and Half need a **Reset** input jack.
- Fade-in time.

Patchable inputs: Rate, Depth, Phase, Pulse width, Reset.

**Envelope ADSHR (×4).** Attack, Decay, Sustain, **Hold** (a minimum time the gate is held, so a one-frame drum hit still
reaches sustain), Release, and Amount. Attack and release curves can be linear or exponential. Options: retrigger from zero
or from the current level, and **Gate threshold**. The **Gate is an input jack** that any source can feed (kick, onset,
seq gate, LFO square, MIDI note). That replaces today's fixed `EnvBand`.

**Step sequencer (×2).**
- 1–16 steps, each with a value (−1..1) and a gate.
- Per-step **probability** (0–100 %) and an optional condition (1:2, 2:2, 1:4 … following Elektron).
- **Glide** per step or global (a slew to the next value).
- Length, direction (forward, back, ping-pong, random), and swing.
- Clock: synced division, or a Clock input jack (kick, onset, MIDI clock).
- Outputs: Value, Gate, End-of-cycle trigger.
- Randomness is seeded and saved, so an export replays it exactly.

**Utilities** ("other modulators"), offered as small modules:
- **S&H**: samples In on each Trigger.
- **Slew**: separate rise and fall times; this is today's route attack/release, moved into a module.
- **Quantize**: steps a value to N levels or to divisions.
- **Math**: A×B, A+B, min/max, invert, offset and scale; this is Serum's aux done as a module.
- **Compare/Logic**: A>B gives a gate; AND/OR of gates; a clock divider (every Nth).
- **Random**: a new value per beat, bar or trigger.
- **Constant**.

Keep the set small. Each one maps to a frequent request in REACTIVE_VISUALS §5 (scaled-by, every-Nth, not-faster-than).

**Cables.** A cable has these settings:
- Source output and destination input.
- **Amount −100…+100 %**, which acts as the attenuverter.
- Curve.
- The existing reaction **mode** (Follow, Step, Spin, Jump, Toggle) when the destination is a visual parameter.
- Optional smoothing.

Several cables into one destination are summed, then clamped to the parameter's range (VCV-style stacking). The
`safety::apply` flash clamp and the luminance smoothing floors (`kLuminanceMinAttack` 80 ms, `kLuminanceMinRelease` 350 ms)
always run last, whatever the patch.

**Cycles and order.** Modulators modulating modulators can form loops (LFO1 → LFO2 rate → LFO1 depth). Use one fixed rule
for this. A cable whose source is a modulator **reads that modulator's output from the previous frame**. Sound sources,
macros, MIDI and tempo read the current frame. This is the frame-rate version of VCV's one-sample feedback. Its benefits:
- Evaluation order does not matter, so it is deterministic and the same on export.
- Loops cannot hang.
- No graph sort is needed.
- The cost is one frame of lag (16 ms) on chained modulation, which no one will see.

Visual parameters used as **sources** also read the previous frame's final value.

**Budget.** Twelve modulators plus up to 64 cables add a few thousand float operations per frame, which is negligible. Cap
user cables at 32 per patch, beside the style DNA's routes.

## 4. Tempo sources

REACTIVE_VISUALS §5.2 already designs the built-in tracker. In summary:
- Onset strength at 100 Hz in `write()`.
- Autocorrelation through a comb filter bank with a log-Gaussian prior (Davies & Plumbley, Ellis).
- A causal beat score with a PLL.
- Hysteresis and a confidence value.
- ~~It runs only while `needsTempo()` is true.~~ Superseded: `Rhythm` always runs (plan §4).

BTrack's header confirms the same pipeline: a balanced autocorrelation of the smoothed onset function, a comb filter bank,
and a log-Gaussian transition weighting
([BTrack.h](https://code.soundsoftware.ac.uk/hg/btrack/file/tip/src/BTrack.h)).
Ellis's dynamic programming is offline, with a global tempo assumed
([Ellis 2007](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Ellis_2007.pdf)). Octave errors are
handled by the log-Gaussian prior centred near 120 BPM, plus the 80–160 BPM preference in REACTIVE_VISUALS. BTrack and aubio
are GPL-3, so write the tracker from the papers instead of copying their code.

**One tempo chip, one source at a time.** Priority is the user's choice. The default is **Auto**: MIDI clock if it is
running, else a typed or tapped tempo if one is locked, else the track tag, else detection.

| Source | How | Live / export |
|---|---|---|
| **Detect** | `core/sound/Rhythm`, always on. | Export runs the same tracker from the track's first sample. |
| **Tap** | At least 3 taps on the chip. Average the intervals, drop outliers, and set the downbeat on the last tap. | Live only; save the tapped BPM into the patch so an export uses it. |
| **Typed BPM** | Tap the number on the chip and type 40–240 BPM, then pick ×2 or ÷2. | Same in both. |
| **Track tag** | TagLib reads the `BPM` property (§1) into the track row. Use it as a strong prior for the detector (its phase still comes from detection), or as a fixed tempo. | Same in both. |
| **MIDI clock in** | 24 ppqn `0xF8`, with Start `0xFA`, Continue `0xFB` and Stop `0xFC`; Song Position `0xF2` sets the bar ([Wikipedia](https://en.wikipedia.org/wiki/MIDI_beat_clock)). Smooth the BPM by averaging the last 24 tick intervals ([rill-patchbay](https://docs.rs/crate/rill-patchbay/0.5.0-beta.7/source/src/midi_clock.rs)), and lock phase to the ticks. | Live only. Record the clock into a Take if exports must match. |
| **MIDI clock out** | Send `0xF8` 24 times per beat from whichever tempo is active, using timestamped `MidiInputPort.send` so jitter stays low **(from memory)**. Start/Stop follow playback. | Live only. |

**Half/double.** The chip always offers ÷2 and ×2. The tracker never switches octave on its own once it is locked (the
hysteresis in §5.2).

## 5. Android MIDI, offline

### 5.1 API

- `android.media.midi` (`MidiManager`) works on API 23+, so it covers minSdk 26. Apps can check for the
  `android.software.midi` feature; declare it `required="false"`. The **virtual (inter-app) transport is always on**, so a
  DAW on the same phone can clock MuzViz with no cable. USB needs the device's USB mode set to MIDI when the phone is the
  peripheral. BLE MIDI is "always enabled" on devices with BLE. Android 13 adds MIDI 2.0 over USB.
  ([AOSP MIDI](https://source.android.com/docs/core/audio/midi))
- **Native side:** the NDK `AMidi` API (`AMidi/AMidi.h`, link `amidi`, NDK r20b+). Kotlin finds and opens the device, then
  `AMidiDevice_fromJava()` hands it to C++. `AMidiOutputPort_receive()` is **non-blocking**, so the GL thread or a small
  MIDI thread can poll it each frame. Clock parsing and the tempo PLL then live in `core/` as ADR 0003 asks. The guide's
  sample omits the `numBytesReceived` argument; follow the header.
  ([NDK MIDI guide](https://developer.android.com/ndk/guides/audio/midi),
  [reference](https://d.android.com/ndk/reference/group/midi))

### 5.2 Permissions and the offline promise

- None of this needs INTERNET. The manifest's INTERNET removal and the CI check stay as they are.
- **USB and virtual MIDI:** no runtime permission **(from memory)**.
- **BLE MIDI:** opening a device uses `MidiManager.openBluetoothDevice`. Finding it needs `BLUETOOTH_SCAN` and
  `BLUETOOTH_CONNECT` on Android 12+. These are runtime permissions. `BLUETOOTH_SCAN` can carry
  `usesPermissionFlags="neverForLocation"`, which is true for this app.
  ([Bluetooth permissions](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions))
- On API 26–30, scanning needs location permission **(from memory)**. **Companion Device Manager** can pair a BLE device
  without location permission on API 26+, filtered on the BLE-MIDI service UUID. The docs mention CDM only in general terms;
  the MIDI-specific flow is **Not yet researched**.
- Ask for Bluetooth only in context, through `ui/PermissionRequest.kt`, when the user taps "Add Bluetooth MIDI device".
  Never ask at first run.
- **Exposing MuzViz as a virtual MIDI device:** a `MidiDeviceService` lets other apps send clock and CC to it, or receive
  its clock **(from memory)**. That is a new manifest entry point, so AGENTS.md must be updated in the same PR.

### 5.3 MIDI as sources and destinations

- **In:** CC 0–127 per channel (learn by moving a knob), note gate, velocity and pitch, pitch bend, and clock (tempo plus
  beat/bar phase). These appear as a "MIDI" source group, which is empty until a device or learn adds entries.
- **Out:** any source or modulator output can be sent as a CC, so the visuals can drive lights or a synth. Clock out also
  goes here. Rate-limit CC out to changes of at least 1 step and at most 100 Hz per CC.
- MIDI is live input. Exports cannot hear it unless a Take records it. Say so in the UI.

## 6. Agreed UX (phone portrait first)

### 6.1 Principles

1. **One new place.** Everything lives in Customize › Advanced › **Modulation**. No other page changes: no "Modulate…"
   long-press on sliders, no new Tempo card. Sound response and Reactions keep their pages.
2. **Every gesture has a tap-only route** (WCAG 2.2 SC 2.5.7): tap a jack, then tap a jack. Dragging is a shortcut.
3. **Modulators exist only when used.** A new patch has no modules; "+ Add" creates one. Six LFOs is a ceiling.
4. **Plain words and units.** "Wave 1 · 1 bar", not "LFO1 rate 0.5". Units come from `render/scene/ParamFormat`.
5. **The knob keeps its value** (plan D3). Modulation is drawn around it, never written into it.
6. **Watch it move.** Every source has a live meter; every modulated jack shows its range and the moving value.

### 6.2 Entry and the locked state

- The Advanced list gains a **Modulation** card (`AdvancedSection.MODULATION_MATRIX`, `available = true`), last in the
  list, with its own icon (`Icons.Filled.Cable`, falling back to `AccountTree`; not Reactions' `Bolt`). Summary: "Patch
  any sound, wave or control into any other." When the active patch has user cables, the summary becomes "5 cables ·
  Breathe" (count and patch name) and the bypass state shows as "Off" if bypassed.
- **Free user:** the card shows the lock like every Advanced card (`ProGated`); a tap opens the paywall
  (`PaywallNavigator.request`), never the page. TalkBack reads "Modulation, Pro, locked. Double-tap to see Pro."
- **Trial:** full access. When the trial ends while the page is open, the page closes back to Advanced and the active patch
  is bypassed (saved, not deleted), so buying Pro brings it back exactly.
- Back from the page returns to the Advanced list (the existing "back closes the Advanced card first" rule).

### 6.3 Page anatomy

```
┌──────────────────────────────────────┐
│ ← Modulation        [Bypass ●] [⋯]   │  pinned toolbar (as in Customize)
│ ♩ 124 BPM · Auto ●   [Tap]  [÷2][×2] │  tempo bar (§6.6)
│ Patch: Breathe ▾          5/32 cables │  patch row: opens the library (§6.7)
├──────────────────────────────────────┤
│  Patch  │  Routes  │  Modules         │  tabs (PrimaryTabRow)
├──────────────────────────────────────┤
│                                      │
│           tab content                │
│                                      │
└──────────────────────────────────────┘
```

- **Toolbar:** back, title, **Bypass** switch (on = the matrix runs; off dims every cable and meter and shows
  "Bypassed" on the patch row), overflow `⋯`: Undo, Redo, Clear patch (confirm), Help.
- **Undo/Redo:** every patch edit (connect, disconnect, amount, module setting, add or remove module) is one step;
  50 steps; lost when the page closes. A snackbar after a delete offers "Undo".
- **Cable counter** "5/32": turns amber at 28, and at 32 the connect actions disable with "32 cables is the limit.
  Remove one to add another."
- The selected tab is saved by name across rotation and process death (`rememberSaveable`), Patch on the first visit,
  then whichever was last used.

### 6.4 Patch tab (the bay)

A two-column bay, not a free canvas (UX research §6.2B).

- **Left column, outputs**, collapsible groups in this order: Modulators (each module's outputs), Macros, Sound (the
  `ReactSource` groups: Levels, Drums, Events, Melody, Rhythm, Harmony, Form, Texture, EQ), Tempo, MIDI (only with a
  device), Visual params (previous frame's value).
- **Right column, inputs**, collapsible: Visual params by Customize page (Motion, Shape, Colour, FX, Camera, then this
  style's own by `ParamScope`), Macros, then each module's inputs (Rate, Depth, Phase, Pulse width, Reset, Gate, Clock …).
- **Groups:** Sound starts collapsed to its 12 most used sources with "Show all 117"; other groups start expanded if
  they hold a patched jack, collapsed otherwise. Jacks the current style does not read are hidden; a "Show hidden (9)"
  row at the end of the group reveals them dimmed, and a cable to one is drawn dotted with "Not used by this style".
- **Jack row** (56 dp tall, the jack itself a 24 dp circle in a 48 dp touch target): label, live meter bar under it
  (outputs) or the modulated-range bar (inputs: base marker, shaded range, moving dot), and a badge with the cable count.
- **Cables** are drawn on a `Canvas` between the columns as cubic curves with a slight sag, 3 dp, coloured by **source
  family**: Sound warm orange, Drums/events pink, LFO blue, Envelope green, Sequencer purple, Utility teal, Macro
  yellow, MIDI cyan, Visual param grey. **Negative amounts are dashed**, so colour is never the only cue. Thickness
  follows |amount| (2–5 dp). When a cable's other end is scrolled off, it ends in an edge stub with a count ("3 ↓");
  tapping the stub scrolls the other column to it.
- **Cable visibility** (overflow, saved): All, Selected jack only, None. It switches itself to "Selected" above 8 cables
  and says so once in a snackbar.

**Gestures**

| Gesture | Result |
|---|---|
| Tap an output | Arms it: it lights in its family colour, every input glows (self-loops are allowed, plan D4) and the outputs dim. A hint bar reads "Now tap where it goes". Tap it again or tap empty space to cancel. |
| Tap an input while armed | Connects at **+25 %**, haptic `CONFIRM`, the new cable animates in (200 ms, none with reduced motion) and the amount sheet opens. |
| Tap an input (not armed) | Arms it the other way round: valid outputs glow. Then tap an output. |
| Drag from a jack | Same as arming; a cable follows the finger; releasing over a valid jack connects; holding within 48 dp of a column's top or bottom edge auto-scrolls that column. Release elsewhere cancels. |
| Tap a jack that has cables (not armed) | Selects its first cable (highlighted, others fade to 30 %); each further tap cycles to the next cable, then back to none (miRack). |
| Tap a selected cable's jack again, or a cable on the canvas | Opens the **amount sheet** for that cable. |
| Long-press a jack | Menu: Connect to… (opens the picker list, the tap-only route for long distances), Show cables, Disconnect all. |
| Pinch | Text and row height scale 85–130 % (saved). No free pan: the columns scroll vertically. |

**Amount sheet** (modal bottom sheet, 50 % height): "Kick → Zoom", a large bipolar slider −100…+100 % with a centre
detent and haptic tick at 0, typed value on tap of the number, Curve (Linear, Exponential, Log, Smooth), Smoothing
(0–2 s), Mode (Follow, Step, Spin, Jump, Toggle; shown only when the destination is a visual param), a live preview of
the destination's range bar, and Mute / Delete. Changes apply live; the sheet has no OK.

### 6.5 Routes tab (the list, also the TalkBack path)

- One row per cable: `[source chip + meter] ──amount bar──▶ [destination chip + range]`, a family-colour stripe, and a
  muted or "not used by this style" tag when relevant. Rows are grouped by destination's Customize page.
- Tap a row: it expands in place to the amount sheet's controls (same component). Swipe left: Delete (with Undo);
  swipe right: Mute. Both are also in the row's menu and its TalkBack actions.
- **"+ Cable"** (extended FAB): a two-step full-height picker. Step 1 *What should move?* (inputs, searchable, grouped
  as in the bay). Step 2 *By what?* (outputs, searchable, with each source's live meter, "Recently used" first).
  Connects at +25 % and scrolls to the new row, expanded.
- Read-only sections at the end, collapsed: "From Reactions" (the 12 routes, with "Edit in Reactions") and "From Sound
  response" (its LFO and envelope targets), so the whole picture of what moves is in one list.

### 6.6 Tempo bar

- Reads `♩ 124 BPM · Auto ●`. The dot is green when `locked`, grey and the number dimmed while not; `beatConf` below
  0.3 shows "listening…" in place of the number. The ♩ pulses on each beat (off with reduced motion).
- Tap the chip: a menu of sources: **Auto** (detect), **Typed** (opens a number field, 40–240), **Track tag** (shown only
  when the playing track has one: "Track tag · 128"), **Tap**, **MIDI** (only with a MIDI device, E4).
- **Tap** button: 56 dp; each tap haptic `CLOCK_TICK`; after 3 taps the BPM appears; the source switches to Tap.
  "Tap 2 more" helper under it after the first tap. A pause over 2 s starts a new count.
- **÷2 / ×2** fold the current tempo; disabled on Auto while unlocked.
- Before E3 merges, the bar shows the detected tempo only (Auto), with no menu, tap or fold.

### 6.7 Patches library

Opened from the patch row. A full-screen page in the panel's back stack.

- Chips: All, Mine, Starters, each folder. A list of cards: name, cable count, module icons, modified date; the active
  one ticked.
- Tap a patch: load it as the active patch. If the active patch has unsaved edits, a dialog: "Save changes to Breathe?"
  Save / Discard / Cancel.
- Card menu (also long-press and TalkBack actions): Rename, Duplicate, Move to folder, Share (FileProvider, as presets),
  Delete (confirm; starters cannot be deleted).
- Bottom bar: "Save" (overwrites the active one, disabled for starters), "Save as…" (name + folder), "New empty".
- The active patch's name gets "•" when it has unsaved edits. Edits always run live; saving is only about the library.
- Opening a shared `.mvzpatch` file imports it into "Imported" and asks before making it active.
- **Starters** (built in, read-only, duplicable): *Breathe* (Wave 1 sine 0.1 Hz → Zoom +10 %), *Kick punch* (Kick →
  Envelope 1 gate; Envelope 1 A 5 ms H 40 ms R 300 ms → Zoom +25 %, Bloom +15 %), *Bar sweep* (Wave 1 synced 4 bars →
  Hue +50 %), *Groove steps* (Sequencer 1, 8 steps, clock 1/8 → Camera yaw, Jump mode), *Chain* (Wave 2 8 bars →
  Wave 1 depth; Wave 1 → Warp).

### 6.8 Modules tab

- A vertical list of module cards (not a pager: easier to scan and to reach with TalkBack), then "+ Add module" with
  counts: "LFO 2/6 · Envelope 1/4 · Sequencer 0/2 · Utility 3/8". A full type is disabled with "All 6 in use".
- **Card header:** name ("Wave 2", renamable), a live mini-scope (40 dp, last 2 s of output; static with reduced motion),
  its output jacks and input jacks as chips with cable counts (tap = arm, as in the bay), and a menu: Rename, Duplicate,
  Reset, Remove (confirm when it has cables: "Remove Wave 2 and its 3 cables?").
- **LFO:** wave picker (icons with labels), Free/Synced segmented button, rate as Hz slider or division chips (1/32 … 8
  bars, plus · dotted and T triplet toggles) with the resolved time ("= 1.94 s at 124 BPM"), phase, polarity, restart,
  pulse width (square only), fade-in.
- **ADSHR:** the shape drawn as an editable graph (drag each corner; the tap route is the five numeric fields under it),
  curve per segment, retrigger mode, gate source chip and threshold slider with the gate's live meter.
- **Sequencer:** 8 steps visible, a second page for 9–16; tap a step for a sheet with value, gate, probability, condition
  and glide; drag vertically across steps to draw values; length, direction, swing, clock (division or Clock input).
  The playing step is outlined.
- **Utilities:** compact cards with their two or three settings.
- **Empty state:** "No modules yet. Add an LFO, envelope or sequencer, or load a starter." with "+ Add module" and
  "Browse starters" buttons.

### 6.9 Other states

| State | What the user sees |
|---|---|
| New patch, nothing patched | Patch tab: both columns with collapsed groups and a centred hint "Tap a source on the left, then a control on the right." Routes tab: "No cables yet" with "+ Cable" and "Browse starters". |
| Bypass off | Cables at 30 % and grey, meters still move, a "Bypassed" chip on the patch row and the Advanced card. |
| Cable into a hidden jack (style does not use it) | Dotted cable, row tag "Not used by this style". It is kept and works again on a style that reads it. |
| Loop (a modulator feeding itself through others) | Allowed (plan D4). The cable closing the loop shows a small ↻ badge; the sheet explains "Reads last frame's value". |
| Cable cap reached | Counter red, connect actions disabled with the message in §6.3. |
| Source not live (MIDI device unplugged) | The MIDI group shows "No device", its cables dashed grey with "Waiting for MIDI". |
| Tempo unlocked | Synced modules keep their last tempo; the tempo bar shows "listening…". |
| Export | Exports use the active patch (bypass respected). MIDI and Tap sources are only heard through a take; the Render page notes "Live MIDI is not in exports" when a MIDI cable exists. |
| Corrupt or newer patch file | Library card "Can't open (made by a newer version)"; the forgiving codec loads what it can and lists what it dropped. |

### 6.10 Accessibility

- **TalkBack.** The bay canvas is hidden (`invisibleToUser`); each jack row is a node: "Kick, output, 2 cables" with
  custom actions "Connect to…" (opens the picker), "Show cables", "Disconnect all". The Routes list is the full path:
  each row reads "Kick to Zoom, plus 25 percent, follow" and is an adjustable node (swipe up or down = ±5 %), with
  actions "Mute", "Delete", "Edit". Module controls are standard sliders and buttons with units in their state
  descriptions ("Rate, 1 bar, 1.94 seconds").
- **Dragging is never required** (2.5.7): tap-tap, the picker and numeric fields cover every drag (cables, ADSHR
  corners, sequencer drawing).
- **Targets** at least 48 × 48 dp; jacks keep 8 dp between touch areas.
- **Colour is never the only cue:** dashed = negative, dotted = unused by style, labels on every chip; the palette
  passes 3:1 against the black-glass surface for non-text and 4.5:1 for text (`GeodeTokens`).
- **Font scale up to 200 %:** jack rows grow, columns stay two; at 175 % and above the bay shows a banner "Routes is
  easier at this text size" with a button to switch.
- **Reduced motion** (system animator scale 0 or the app's reduced-motion setting): no cable animations, no beat pulse,
  static scopes and meters updated at 4 Hz.
- **Haptics** via `ui/theme` haptics only, and off when the system turns them off.
- Tested by a Compose UI test that builds, edits and deletes a cable through semantics actions only (as
  `ReactivityTabTest`), plus a journey that opens the page, loads a starter and checks the counter.

### 6.11 Performance

- Meters and scopes read one JNI float snapshot per frame, polled at most 30 Hz from a single `produceState`, and only
  while the page is visible; the bay redraws cables in one `Canvas` from that snapshot, so jack rows do not recompose
  per frame.

## 7. Engine shape

Superseded by the plan: the registry is plan §2, modulators and routes §3, tempo §4, MIDI §5.

## 8. Staged PR plan

Superseded by plan §6 (E0 registry, E1 modulators, E2 UI, E3 tempo, E4 MIDI, E5 Bluetooth MIDI).

## 9. Not yet researched

- Synesthesia's and Magic Music Visuals' current modulation UIs (LFO assignment, envelope editors).
- Official Vital, Serum and Ableton documentation; only forums and secondary pages were read.
- The exact Bitwig and VCV behaviours for cable opacity and tension settings.
- Companion Device Manager for BLE MIDI. The exact permissions for USB MIDI on Android 14–16. `MidiUmpDeviceService` for
  MIDI 2.0.
- Media3 `MediaMetadata` and MediaStore exposure of BPM (not found; TagLib is the planned path).
- madmom and other real-time trackers' measured latency on phones. On-device CPU cost of §5.2 (to be measured).
- Usability studies of patching on phones. None were found; the recommendations above come from app design, not user
  testing.
