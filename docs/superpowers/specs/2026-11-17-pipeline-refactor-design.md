# Pipeline refactor: design and roadmap

Status: proposed, awaiting the owner's approval. No code has been changed.
Basis: seven read-only investigations of the source at `f313faf` (analysis, structure, parameters, shaders, performance, Kotlin shell, layout).
Every runtime figure here is an estimate from reading code; none is a device measurement.
Sections 1, 2 and 7 state what the code was at that commit; where main has moved since, the line says so, and counts and line numbers are not kept current.
`AGENTS.md`, `CLAUDE.md` and `docs/agents/*` were not used as sources of fact; section 7 lists where they contradict the code.

## 1. Why the app feels slow and late

- The APK published for testing is a debug build (`.github/workflows/ship-apk.yml`, the `:app:assembleDebug` step), which slows the Kotlin screens and start-up.
- A style averaging under 50 fps for 2.5 s was capped at 30 fps for 45 s up to 12 min (`ThermalGovernor`, `frame/Thermal.hpp`); both are gone, and the pacer divides by the fps cap only (`core/viz/frame/Pacer.hpp`).
- Scenes rendered at 1.25x or 1.4x per axis on screens under 2200 px or 1600 px; that is gone, and the scene target is the surface's pixels times the user's render scale (`Renderer::applyRenderScale`, `core/viz/Renderer.cpp`).
- 260 of 321 style programs march rays, 102 steps by default, with secondary marches per pixel.
- The engine listens only up to the sample the speaker plays at frame start (`core/sound/interp/SpeakerSync.hpp`).
- The frame was not told when it will be shown; it is now: the vsync callback's expected presentation time reaches the frame (`core/stage/android/VsyncThread.cpp`) and the swap (`StageThread::presentAt`).
- Route smoothing is stacked on detection: 50 ms default attack, 80 ms floor on luminance (`ReactRoute::attackSeconds` and `kLuminanceMinAttack`, `core/viz/Reactions.hpp`).
- A trigger lasts one frame, so a hit's strength depends on the frame rate.
- A 60 fps cap on a 90 Hz panel draws 45 fps (`Pacer::divisorFor`, `core/viz/frame/Pacer.hpp`).
- Analysis and the parameter chain ran on the drawing thread, once per surface; the parameter chain still does (`core/viz/RendererFrame.cpp`), the analysis now runs on its own thread per ring (`core/sound/hop/AnalysisThread`).
- Not causes: parameter-struct copies, uniform lookups and locks total tens of microseconds per frame.

## 2. Why a change touches so many files

- One native function is written by hand in five places; 538 `external fun`, 102 added in five days.
- One modulatable parameter touched 24 files (commit `7e85267`).
- Sources are described by 10 separate lists and destinations by 8.
- Five modulator mechanisms each have their own engine, save format and API.
- Slot counts are fixed: 3 waves, 2 envelopes, 16 routes, 16 knobs.
- A wave may steer only a later wave; a route may steer no wave; nothing can form a circle.
- Each target has a hidden scale in a switch (the target switch in `core/viz/Lfo.cpp`).
- A style cannot own a parameter.
- 272 of 273 styles read sound values directly, beside the reaction matrix.
- `MotionField::apply` is a second fixed sound-to-parameter table (`core/viz/MotionField.cpp`).
- A shared shader feature is edited into 60 to 120 style files.
- Host tests are 148 hand-written compile lines in one workflow file.
- `core/viz` holds 186 top-level files; only 18 of 116 units touch the graphics API.
- `core/signal` and `core/viz` include each other.

## 3. Target

### 3.1 Pipeline and threads

- Sound in: the player's tap, the mic and capture write the native ring, as today.
- Analysis thread: one per ring, shared by every surface, woken by samples arriving.
- Modulation thread: one per surface, woken by events only, never by a timer.
- Events are: a new analysis result, a trigger, an edit, and "a frame is wanted for time T".
- Renderer: reads the latest published state and draws; it computes no parameter.
- Export runs the same analysis and modulation code inline, offline, so it matches live.
- The safety limit stays a fixed last step that no route can address.

### 3.2 One board, one module shape

- Slot: one numbered value on a shared board, with a stable text id.
- Every source and every destination is a slot, including each modulator's own settings.
- Module: declares the slots it reads and writes, and one function that writes in place.
- Analysis stages, waves, envelopes, knobs, routes and shader inputs are all modules.
- Route: source, destination, depth, curve, mode, smoothing, optional depth-scaling source.
- Trigger: a separate kind from a value, stamped in audio sample time.
- Each destination keeps its base value apart from its modulation.
- Counts are open-ended, with a safety ceiling.

### 3.3 Motions

- An event carries how a value moves from a moment on, not a sampled number.
- The number is worked out only for the moment a frame will be shown.
- The modulation thread publishes each parameter with its direction of travel.
- The renderer places it at the display moment with one multiply-add per value.

### 3.4 Compile on change

- When routes or the look change, modules are sorted into one flat list.
- A circle is cut at one link, which reads the previous value; the cut is stable and shown in the editor.
- Anything no shader input, route or director reads is dropped, back to the analysis stage.
- Tempo, key, sections and loudness always run, because they need history.
- A destination is recomputed only if a source changed or moves in time.

### 3.5 Shaders

- A style declares its inputs and passes in a header: name, type, range, default, label, luminance class.
- The engine generates uniform declarations, upload, Customize rows and route targets from that.
- Spectrum, waveform, EQ and chroma stay available as data textures.
- Every other sound read becomes a declared input with a default route the user can see and edit.

### 3.6 Kotlin

- Native owns the look, knobs, palettes and modulation as documents.
- Kotlin sends small commands and reads one published state block per surface.
- Kotlin holds no sample, buffer, parameter logic, undo history or frame loop.
- Two accepted exceptions, both platform limits: capture of another app, and the output-mix capture.

### 3.7 Layout of `core/`

```
util/      shared helpers                         includes nothing
audio/     ingest, dsp, output, clock             includes util
sound/     analysis; hop/ thread; interp/         includes util, audio/ingest
shader/    catalogue/, source/, program/          includes util
signal/    params/, events/, mod/, routes/,
           director/, look/, Resolver             includes sound, util
viz/       frame drawing only                     includes signal, shader, sound
stage/ output/ cast/   surfaces                   include viz and below
api/       C interface, one header per module     included by no engine file
```

- One direction only: a folder may include what is above it in this list, never below.
- `tools/core-include-gate.sh` enforces it, with an allow-list that may only shrink.
- Namespaces are left alone in every move.

## 4. Roadmap

Each step is one pull request that leaves CI green. Later phases depend on earlier ones only where stated.

### Phase 0: measure and tell the truth

- 0.1 Publish a release-like build for testing.
- 0.2 Add timing sections and per-pass timers to the frame, with a debug readout of tier, paced fps and render size.
- 0.3 Measure on the owner's phone: build flags, the tier log line, a system trace, a slow-motion click video.
- 0.4 Correct the documents listed in section 7.
- 0.5 Add a timing score to the sound bench, so lateness is a number CI checks.

### Phase 1: lateness and frame rate, without restructuring

- 1.1 Lead the listen point by the display delay and the fixed detection delay, for the player only. Needs 0.3 to size the lead.
- 1.2 Take the expected presentation time from the vsync callback.
- 1.3 Make route smoothing independent of frame rate.
- 1.4 Make the transient detector the hit path, with low, mid and high hits.
- 1.5 Shed resolution before frame rate on a frame-time shortfall. Owner decision D2.
- 1.6 Remove or gate the extra-pixel rendering. Owner decision D3.
- 1.7 Scale secondary marches by quality tier.
- 1.8 Blur behind sheets from the image before composite.
- 1.9 Fix pacing on 90 Hz panels.

### Phase 2: seams, no file moves

- 2.1 Include-direction gate, recording today's violations.
- 2.2 Shared source lists for host tests, or one host build target.
- 2.3 Engine headers include the sound frame header instead of the C interface header.
- 2.4 Split the C interface header by module, with an umbrella.
- 2.5 Widen the Kotlin gate to `cast/`, `audio/`, the tap, and the three controllers; count per area.

### Phase 3: moves the redesign builds on

- 3.1 Audio clock into `audio/clock/` (6 files).
- 3.2 Parameters into `signal/params/` (about 25).
- 3.3 Modulation, routes and director into `signal/` (about 40).
- 3.4 Shader catalogue, source and programs into `shader/` (about 25).
- 3.5 Cast rules into `cast/`; look documents into `signal/look/`.

### Phase 4: the redesign

- 4.1 Analysis thread, one per ring. Needs 3.1. Check: the sound golden test, live equals export.
- 4.2 Triggers as their own kind in sample time. Needs 4.1 and 3.3.
- 4.3 Slot registry and compiled routes behind the existing `Resolver` interface. Needs 3.2 and 3.3.
- 4.3 first reproduces today's output, held by `resolver_test`, before any new freedom is switched on.
- 4.4 Importer for saved looks: names, hidden scales, old order.
- 4.5 Modulation thread with motions. Needs 4.1 to 4.3.
- 4.6 Style headers and generated uniforms. Needs 3.4.
- 4.7 Direct sound reads moved behind default routes, one family per pull request.
- 4.8 Director, note response and motion field become visible sources and routes.
- 4.9 One ordered pass list in the renderer; passes created on first use.

### Phase 5: Kotlin

- 5.1 Native look document with commands, undo and rolls. Unblocks 5.2 to 5.4.
- 5.2 One look latch for every surface.
- 5.3 Native Auto stepper.
- 5.4 Published state block; delete the polling tickers.
- 5.5 Export audio, then export video, then loop extend onto the native encoder.
- 5.6 Cast threads. Owner decision D8.
- 5.7 Template codec.
- 5.8 Generate the bridge declarations and constants from the C interface.
- 5.9 Shell structure: one UI state per ViewModel, stores into the dependency graph, split `PlayerSession`.

### Phase 6: analysis quality

- 6.1 Real-input transforms; pitch, harmony, key and emotion at lower rates.
- 6.2 Onset strength on a band bank.
- 6.3 Retire unread outputs. Owner decision D7.
- 6.4 Add: bass pitch, sustained versus percussive energy, downbeat confidence, chord root, per-band stereo, a level-history texture.

## 5. Decisions for the owner

- D1 Evaluation trigger. The parameter investigation proposed evaluating on each 10 ms analysis result. This design instead uses events plus "a frame is wanted", per the owner's "no clock". Confirm.
- D2 May a frame-time shortfall lower resolution before frame rate?
- D3 May the extra-pixel rendering go, for a slightly softer picture?
- D4 May the engine read ahead of the speaker for the player? Mic, capture and cast cannot.
- D5 Must old looks match bit for bit, or is "equal except where a limit is hit" acceptable?
- D6 Moving sound reads behind routes cannot be pixel-identical on luminance targets, because of the safety smoothing. Accept?
- D7 May unread sources be retired, kept as ids that read a constant?
- D8 Cast container: a native writer, a different stream format, or the platform writer as a named exception.
- D9 Does undo history move to native, so it also covers Auto and knob edits?
- D10 Do knobs, the hand mask and the director stay app-wide or move into the look?
- D11 Layout: stage folders with a gate (proposed), a minimal regroup, or one library per stage.
- D12 Shader assets: sub-folders now, or with the style headers.
- D13 Is a whole-track pass ahead of time allowed for local files?
- D14 The tap stays a 56-line Kotlin class with one native call per block. An `external` override costs more than it saves. Accept?
- D15 Mentions of other projects in 11 tracked files, against the clean-room rule: remove?
- D16 The vendored transitions file: check its licence against the clean-room rule.

## 6. Not verified

- Time per pass on any device.
- Whether the frame-rate cap fires on the owner's phone.
- The depth of the player's output buffer, which bounds the listen lead.
- The decision records; their files are not in the tree and the history is shallow.
- That audio processors run on the player's playback thread; this rests on source comments.

## 7. Documents that contradict the code

As found at `f313faf`; most have since been corrected (`AGENTS.md` › Edit log).

- `AGENTS.md` lists a module `:engine:audio-android`; it is not in `settings.gradle.kts`, and `PcmTap` is in `:app`.
- `AGENTS.md` says `:engine:audio-core` holds the ring handle, frame layout and tuning; it holds one file.
- `AGENTS.md` says the render ETA and range clamp are Kotlin; both are native.
- `AGENTS.md` names the base knobs Glow, Heat, Bend, Flow; the code has Shape, Motion, Colour, Depth.
- `AGENTS.md` omits `android.yml` and `tools/jni-parity-gate.sh`.
- `docs/agents/native-core.md` says no JNI calls the stage interface; 30 do.
- `docs/agents/native-core.md` says the sound engine steps once per drawn frame; it runs per 10 ms of samples.
- `docs/agents/data-flow.md` contradicts itself on whether the mic uses a Kotlin thread.
- `docs/agents/large-files.md` lists a file that does not exist and several wrong sizes.
- `docs/agents/engine-kotlin-audit.md` lists about 20 classes twice, as pending and as done.
- `docs/agents/kotlin-logic-map.md` and `engine-deep-scan.md` describe Kotlin rendering files that no longer exist.
- `docs/agents/modules.md` lists a package that does not exist.
- `GLOSSARY.md:209,216` cite two files under `core/signal` that do not exist.
- `core/viz/frame/README.md` says four passes implement the pass interface; none do.
