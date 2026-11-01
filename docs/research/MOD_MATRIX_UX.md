# Modulation matrix: a patch bay that fits a phone

Research note, 2026-11-01. It follows [`REACTIVE_VISUALS.md`](REACTIVE_VISUALS.md), which already proposes one native
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
| Reaction routes | `core/viz/Reactions.hpp` | 12 routes, 72 `ReactSource`s, 5 modes (Follow, Step, Spin, Jump, Toggle), amount −2..2, curve, attack/release. The style DNA fills up to 9 of them. | Destinations are `LfoTarget` only. Sources are sound only. |
| LFOs | `core/viz/Lfo.hpp` | 3 slots, wave (sine, triangle, saw, square, random), `rateSeconds` 0.05–60 s, depth, polarity, curve, one target each. The chain targets are `Lfo1Rate…Lfo3Depth`. | The owner wants 6. There is no tempo sync, phase or retrigger. Each slot owns its target, so it is not a source. |
| Envelopes | `core/viz/Adsr.hpp` | 2 ADSRs gated by a band over a threshold, several targets each, able to offset the LFOs' rate and depth. | The owner wants 4, with Hold (ADSHR). The gate is a fixed band, not any signal. |
| Macros | `core/viz/Macros.hpp` | 6 knobs from −1 to 1 with their own 7-route engine. | They are not offered as sources yet. |
| Destinations | `LfoTarget` (≈ 54 entries) vs `SceneParams::fieldNames()` (`core/viz/ParamsFields.cpp`, ≈ 150 fields) | Only about a third of the parameters can be modulated. | "Every visual parameter" needs a destination table generated from the field list and the catalogue's ranges and `ParamScope`. |
| Frame order | `core/viz/RendererFrame.cpp` | ADSR → LFO (rate and depth from the envelopes) → macros → LFO apply → ADSR apply → reactions → macro reactions → … → `safety::apply`. | The order is fixed. A modulator cannot hear a later one. |
| Saved form | `engine/scenes/.../render/ReactionRouteCodec.kt` | Plain text `reactions/2`, pure Kotlin, forgiving decode. | Copy this style for a `modmatrix/1` file. |
| UI slot | `ui/CustomizeAdvanced.kt` | `AdvancedSection.MODULATION_MATRIX` is reserved (`available = false`). Advanced is already Pro-gated. | The page itself. |
| Tempo | none | `Lfo.kt` KDoc says there is no tempo sync on purpose, because "a wrong estimate drags the whole look". | Keep that worry as a rule: tempo is opt-in, and a synced LFO must never jump (REACTIVE_VISUALS §5.1). |
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
- It runs **only while `needsTempo()`** is true.

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
| **Detect** | `core/sound` tracker, on only when needed. | Export runs the same tracker from the track's first sample. |
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

## 6. Recommendation for MuzViz (phone portrait)

### 6.1 Principles

1. **Start from the thing you want to move.** The main entry is a long press on any Customize slider, then "Modulate…"
   (REACTIVE_VISUALS §2.4). It drops a cable and the slider shows a ring arc of its modulation range, as Vital and Bitwig do.
   The patch bay is where cables are reviewed and where clusters are built. Most users never need to open it.
2. **Every gesture has a tap-only route** (WCAG 2.5.7): tap a jack, then tap a jack, the Model 15 double-tap pattern. Drag
   is a shortcut, not the only way.
3. **Modulators exist only when used.** The bay starts empty, with "+ Add" tiles. Six LFOs is a ceiling, not six cards on
   screen.
4. **Plain words and units.** Write "Wave 1 · 1 bar", not "LFO1 rate 0.5". Use the unit table in REACTIVE_VISUALS §6.
5. **Tempo is off until something asks for it.** No tempo chip, no tracker CPU, no confusion.

### 6.2 Screens

**A. Cables (the default page of the matrix).** This is a list, and it is also the TalkBack path.
- Each row reads `[source chip] ── amount bar ──▶ [destination chip]`, with a live meter on the source and the cable's
  colour as a stripe.
- Tap a row to expand curve, mode, smoothing and "scaled by". Swipe or use the menu to mute or delete.
- Sections group the rows: "Your cables", "Style cables" (DNA, greyed, with "Reset to style"), and "Macro cables" (read
  only).
- "+ Cable" opens a two-step picker: *What should move?* then *By what?*, with search and the recommended source first.

**B. Bay (the patch view, toggled from the toolbar).** This is a two-column patch bay rather than a free 2D canvas, because
a phone has no room for module panels.
- **Left column, outputs.** Grouped and collapsible: Sound (72, in the picker's groups, collapsed to the most useful ~12
  with "Show all"), Tempo, Macros, Modulators (each one's outputs), MIDI, Visual params.
- **Right column, inputs.** Visual params by tab (Motion, Shape, Colour, FX, Camera, style-specific by `ParamScope`), then
  modulator inputs (Rate, Depth, Phase, Reset, Gate, Clock …).
- **Cables are drawn between the columns** as curves with slight sag. When a jack's partner is scrolled off screen, the cable
  ends in an edge stub with a count badge ("3 ↓"), and tapping the stub scrolls there.
- **Tap-tap patching.** Tap an output: it lights, valid inputs glow, and the other columns dim. Then tap an input. Dragging
  from a jack does the same. While dragging, holding near the edge auto-scrolls the other column (Audulus/Model 15 lesson).
- **Tap a jack with cables** to cycle through them (miRack), and edit the highlighted cable's amount in a bottom sheet.
- **Cable visibility:** All / Selected jack only / None. The default switches to "Selected" above 8 cables.
- **Colour by source family:** Sound warm (orange/red), Drums/events pink, LFO blue, Envelope green, Sequencer purple,
  Macro yellow, MIDI cyan, Visual param grey. Negative amounts are dashed, so colour is never the only cue.

**C. Modulators (a horizontal pager of small modules).**
- Each card is one module: title ("Wave 2"), a live mini-scope, its main settings, and its jacks (output dots at the
  bottom, input dots on the left) that connect exactly as in the Bay.
- The LFO card has the wave picker, Free/Synced, rate as Hz or division with the resolved time, phase and Restart.
- The ADSHR card draws the shape so you can drag its corners, with numeric fields as the tap alternative, and a Gate input.
- The Sequencer card has 8 steps visible (paged to 16). Tap a step for value, probability and glide. There are Length and
  Clock controls.
- "+ Add module" lists LFO (6), Envelope (4), Sequencer (2) and the utilities, and shows how many slots are free.

**Toolbar (pinned, as in Customize):** Cables | Bay | Modulators, the tempo chip (only when needed: `124 BPM ● Detect`), a
**Bypass all** switch (an A/B check), Undo, and Patches (save, load, the starter patches).

### 6.3 TalkBack

- Each jack is a node, for example "Kick output, 2 cables". Its custom actions are "Connect to…", which opens the picker
  list, "Show cables" and "Disconnect all".
- Each cable row is a node with its state ("Kick to Zoom, plus 35 percent"), adjustable as a slider with custom actions
  "Mute" and "Delete".
- The Bay canvas itself is hidden from TalkBack (`invisibleToUser`), because the list carries the same content.
- Add a `ReactivityTabTest`-style Compose UI test that builds a cable only through semantics actions.

### 6.4 Default patch and starters

- **Default:** no user cables, no modulators, and tempo off. The style's DNA cables are listed as "Style cables", so the
  first visit is not blank and shows what already reacts.
- **Starter patches**, in the same pattern as `macros::starters()`. Each is a single tap and shows what it added:
  1. *Breathe*: Wave 1, sine, free 0.1 Hz → Zoom +10 %.
  2. *Kick punch*: Kick → Envelope 1 gate; Envelope 1 (A 5 ms, H 40 ms, R 300 ms) → Zoom +25 % and Bloom +15 %.
  3. *Bar sweep*: Wave 1, synced 4 bars → Hue +50 %. This turns tempo on, and the chip appears.
  4. *Groove steps*: Sequencer 1, 8 steps, clocked by 1/8 → Camera turn, in Jump mode.
  5. *Chain*: Wave 2 (8 bars) → Wave 1 Depth; Wave 1 → Warp, as a first taste of modulating a modulator.

### 6.5 Keeping it un-overwhelming

- Three layers. **Long-press "Modulate…"** on a slider, then **Cables list**, then **Bay and Modulators**. Each layer is
  reachable from the previous one with one descriptive button.
- Collapse the source list to about 12 "most used" sources by default (from REACTIVE_VISUALS §3); "Show all 72" is one tap.
- Show a live meter beside every source and a ring on every modulated slider. People learn by watching it move.
- Validate as you patch. Inputs that would do nothing for this style are hidden, not just greyed. The amount defaults to a
  gentle 25 %.
- A free user sees the Advanced card with a lock, as today. The long-press "Modulate…" can offer one free cable per style
  as a taster, if `FreeTier` wants it. That is the owner's call.

## 7. Engine shape (C++ first)

- `core/viz/mod/`:
  - `Modulators.{hpp,cpp}`: `LfoBank` (6), `EnvelopeBank` (4, ADSHR), `StepSeq` (2), `Utilities`.
  - `ModMatrix.{hpp,cpp}`: the cables, the previous-frame output table, `tick(frame, dt, tempo)`, `apply(SceneParams)`,
    `needsTempo()`.
  - Today's `LfoEngine` and `AdsrEngine` become thin wrappers, or are migrated.
- **Destination table.** Generate it from `SceneParams::fieldNames()` plus the catalogue's ranges and `ParamScope`, so
  "every visual parameter" stays true as fields are added. Keep the `LfoTarget` ordinals as aliases for old saves.
- **Order per frame:**
  1. Read the sound frame, tempo and MIDI.
  2. Tick modulators (inputs from the previous frame's outputs, or the current frame's sound).
  3. Sum cables into parameter offsets.
  4. Apply macros, then DNA routes (unchanged, or as "style cables").
  5. `MotionField`, then `safety::apply`, then `CameraRig`.
- `core/sound/Tempo.{hpp,cpp}`: the tracker (REACTIVE_VISUALS §5.2), plus `TempoSource` arbitration (detect, tap, typed,
  tag, MIDI) and a MIDI clock PLL.
- `core/midi/` (new): byte parsing, clock, CC learn tables. JNI only passes bytes and timestamps.
- **Kotlin:** a `ModPatchCodec` (plain text `modmatrix/1`, written in the same style as `ReactionRouteCodec`), the UI, the
  device picker and permissions.
- **Live UI meters:** read one snapshot array of modulator and source values per frame through JNI, at most 30 Hz.
- **Tests:**
  - `core/tests/modmatrix_test.cpp`: cycle determinism, live vs 1/fps equality, clamp and safety still last, seeded
    sequencer probability replay.
  - A JVM test of the codec.
  - A click-track tempo test (REACTIVE_VISUALS §8).

## 8. Staged PR plan

Each PR is shippable on its own and touches `AGENTS.md` when structure changes (the arch-map gate).

1. **Destination table.** Destinations generated from `fieldNames()` and the catalogue; `LfoTarget` kept as aliases; a test
   that every field with a range is a destination. No UI.
2. **`ModMatrix` core.** Cables, previous-frame rule, summing and clamps. Today's 3 LFOs, 2 ADSRs and 12 routes are
   expressed as cables underneath, with a saved-format migration. The look must not change; add a golden-frame test.
3. **New modulators.** 6 LFOs (shapes, phase, restart modes, Free only), 4 ADSHR with a Gate input, 2 step sequencers
   clocked by triggers, and the utilities. C++ tests.
4. **Cables page and long-press "Modulate…".** Pro gate, slider rings, starter patches 1, 2, 4 and 5, and TalkBack actions
   with a Compose test.
5. **Bay and Modulators pages.** Tap-tap and drag, cable colours and visibility, edge stubs, mini-scopes.
6. **Tempo.** The tracker in `core/sound` (gated by `needsTempo`), tap, typed, BPM tag via TagLib, the tempo chip, Synced
   LFO and sequencer rates, and starter 3. Export uses detect, typed or tag.
7. **MIDI in.** `MidiManager` device list (USB and virtual first), AMidi polling, clock in, CC and note sources with learn.
8. **MIDI out and BLE.** Clock out and CC out. Then BLE devices with in-context Bluetooth permission (CDM if it proves
   viable), and optionally a `MidiDeviceService`.

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
