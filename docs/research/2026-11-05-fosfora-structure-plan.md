# What we take from fosfora: signal flow, structure, plan

Status: **plan for review (2026-11-05)**. Nothing is built. The input to the domain-modelling and folder
scaffolding work the owner asked for; it ends in a new ADR, not in code.

Read: [kevinraymond/fosfora](https://github.com/kevinraymond/fosfora) @ `2e85d3b` (MIT/Apache-2.0; we
take ideas and shapes, not code). It is a desktop VJ engine in Rust (wgpu, egui) with Python for the
accuracy bench, diagrams and model training.

## 1. Fosfora in one page

```
capture callback ─► SPSC ring ─► audio thread: fixed 512-sample HOP on the SAMPLE clock
   HopAnalyzer::process_hop   (audio/hop.rs: the one chain, in the one correct order)
     DC strip → FFT ×3 res → loudness → stereo → HPSS → pitch → contrast → dMFCC → kick → key
     → pre-norm snapshot → normalize → beat → downbeat → structure → smooth → AudioFrame
   ─► bounded(4) drop-oldest queue      + 1-frame pulses latched as atomic counters
render thread, per frame:
   FeatureInterpolator(t)  (1.5-hop playhead, per-field Lerp/Hold, beat & bar phase PLLs)
   ─► BindingBus::evaluate  (source id → [transforms] → typed BindingTarget), once per frame
   ─► frame_graph::execute_and_composite
        per Layer: PassExecutor (effect passes, ping-pong HDR) → particles → its node chain
        → Compositor (20 blend modes) → master chain → PostProcessChain → present / sinks
offline: FileDriver runs the SAME hop chain faster than realtime; headless SceneRenderer runs the
SAME frame_graph, bindings and cueing code with the window removed.
```

The ideas that make that structure good, each with where it lives:

| # | Idea | Fosfora | Why it matters |
|---|---|---|---|
| F1 | **Analysis runs on a fixed hop of the sample clock, never the frame clock** | `audio/hop.rs`, `ANALYSIS_HOP = 512` | Same numbers at 30, 60 or 144 fps, live or offline. Export equals live *by construction* |
| F2 | **One canonical chain, driven by anything** | `HopAnalyzer` takes (hop, timestamp, config snapshot); no wall clock, no locks | Live ring, file, test signal and bench all run one code path |
| F3 | **One feature schema** | `audio/schema.rs::FEATURES[85]`: name, `NormPolicy`, `SmoothParams`, `DecayPolicy`, `InterpPolicy`, row *i* = field *i* | One place says what a value is called and how it is ranged, smoothed, decayed and interpolated |
| F4 | **The frame is a frozen ABI with a golden test** | `#[repr(C)] AudioFeatures` pinned at 340 B; `GOLDEN_HOPS` pins values at 1e-5; offline driver asserted equal | A refactor can't drift the sound silently; appending a field is a deliberate, visible change |
| F5 | **Render-side interpolation** | `audio/interp.rs` | Analysis rate and display rate decouple without stair-steps or phase pops |
| F6 | **Triggers survive by counting** | `PulseCounts` (atomic counters) | A beat is never lost to a dropped frame or a slow consumer |
| F7 | **One modulation bus** | `bindings/`: `Binding{source, transforms[], target}`, `TransformDef` (remap, smooth, invert, quantize, deadzone, curve, gate, scale, offset, clamp), `BindingTarget` enum (param, layer field, post FX, uniform, transport) | Every route, whatever its source, is evaluated once per frame in one place; base value and modulation kept apart (`LayerModulation`) |
| F8 | **Composition over a fat base class** | `LayerContent::{Effect, Media}`; `EffectLayer` owns its `PassExecutor`, uniforms, params; node graph `NodeKind::{Source, Effect, Feedback, ChainInput, Anchor, Output}` | Each thing has only the parts it uses; a new kind is a new variant, not 20 no-op hooks |
| F9 | **The frame is one function both paths call** | `gpu/frame_graph.rs` (pulled out of `App::render` after copies drifted) | Live, export and thumbnails cannot disagree |
| F10 | **Effects are data** | `.pfx` JSON: params (≤16, typed), passes, feedback, `rates`, particles, post | A style declares its own controls; the engine stays generic |
| F11 | **Rates are integrated** | `"rates"`, `RatePhase` (`phase += value·dt`) | `time × speed` strobes when speed is modulated; an integral never does |
| F12 | **Narrow traits at the edges** | `SignalSink` (UDP / JSONL / Vec), `FrameSink`, `SectionEstimator` (`tier()`: heuristic now, model later) | Live, dump and test share one emitter; a detector can be swapped for a model |
| F13 | **Measured, not tuned by ear** | `bench/` (Python): `--signal-dump` JSONL scored against Ballroom, GiantSteps, Harmonix; frozen manifests, holdout read once; CI fixture with floors | Detector constants ship with evidence; regressions fail CI |
| F14 | **Sources of truth are written down** | `ARCHITECTURE-NOTES.md` "Sources of truth", generated `docs/DIAGRAMS.md` | An agent or a person can find the one place a fact lives |

What we do **not** copy: the 5,000-line `main.rs` and `app.rs` (their god object; we have `PlayerSession`),
OSC, MIDI, web and NDI I/O (Geode is offline by design), the egui node canvas (a phone is not a patch bay;
`MOD_MATRIX_UX.md` already decided our UI), and the feature count for its own sake.

## 2. Geode today, against those ideas

| # | Geode now | Gap |
|---|---|---|
| F1 | `SoundEngine::write` runs the filter bank per sample and `Rhythm` on a 10 ms sample hop, **but** `SoundEngine::step(dt)` runs the Hann → FFT → descriptors → drums → events → melody → `musical()` path **once per render frame** on the newest window (`core/sound/SoundEngine.cpp:200`) | Everything FFT-derived depends on the frame rate. Export "equals live" only because `TrackAudio` is stepped at 1/fps; a 30 fps export and a 60 fps screen hear different FFT cadences |
| F2 | `SoundEngine` is close (no wall clock), but `Emotion` lives in `viz::Director`, `Silence` is free functions, `LiveSignal.hpp` re-reads the frame its own way | No single "here is a hop → here is a frame" object |
| F3 | Three hand-kept lists: `SoundFrame.h` (108 scalars, kinds only in a comment), `ReactSource` + `kSourceTable` + `kSourceInfo` (117, `Reactions.cpp`), `lib_sound.glsl` (114 `#define SND_*`, checked by `glslcheck`); 7 scalars have no `ReactSource` (rms, pitch, beatCount, barCount, phraseIndex, scale, sectionBars) | Names and policies drift; smoothing is inline per stage (`Stages.hpp` instances in `State`), consumer ranging in `kSourceTable` Kinds |
| F4 | `GeodeSoundFrame` is a C struct, 352 floats, no version; `sound_test`/`musical_test` assert behaviours, not a pinned vector | Nothing stops a refactor drifting the numbers |
| F5 | None needed today (analysis is per frame), needed the moment F1 lands | — |
| F6 | `beatCount`, `barCount`, `uSoundCounts` exist | Mostly there; make it the rule for every trigger |
| F7 | Six modulators composed in a fixed chain in `Renderer::resolveParams` (`RendererFrame.cpp:120-213`): director → notes → ADSR → LFO → macros → `reactions_` → `macroReactions_` → `director_.applyRoutes` → `MotionField` → `safety::apply`. Routes are `Route{source, LfoTarget, ReactMode, amount, curve, a/r}` ×12; LFO has its own 9-entry `ModSource`; targets are `LfoTarget` (~57) applied by a switch with per-target clamps (`Lfo.cpp:54`) | Same idea as fosfora's bus, spread over five engines with three source enums. `MOD_MATRIX_PLAN.md` (E0 registry of jacks) is the agreed fix and fits F7 exactly |
| F8 | `Scene` (`core/viz/Scene.hpp:20`) has 8 pure virtuals and ~20 no-op hooks (`acceptPcm`, `setFlow`, `setTouchField`, `setCamera`, `setMilk*`, `isFluid`, `isWater`, `velocityTexture` …); `SceneRegistry::create` is an if-chain | Fat interface; capability questions asked by type flags |
| F9 | Live and export already share `Renderer::render` (good). Its body is a hardcoded call list (`RendererFrame.cpp:408-434`); `TrailPass`, `CompositePass`, `UiBlurPass`, `LiquidPass` share no interface (`NoteBloomPass` was removed with the note blooms) | Order lives in one function body; adding a pass or a layer means editing it |
| F10 | `Catalogue.cpp` (one C++ table: ids, kinds, families, scopes, ranges); every style receives the same 165-field `SceneParams` and picks what it reads | Styles cannot declare their own controls; Customize shows global params |
| F11 | Not checked yet: audit the shaders and `MotionField` for `uTime * speed` on modulated speeds | — |
| F12 | No sink or estimator seams; detectors are concrete members | Fine until we want a dump or a model (sound-engine-v2 §6 wants models) |
| F13 | Host unit tests in CI, research docs; no dataset scoring | Rhythm, Harmony and Form constants are tuned by ear |
| F14 | `AGENTS.md` is the map; no "sources of truth" list | Cheap to add |

## 3. The target shape

### Signal flow

```
PcmTap / capture ─► SampleRing ─► SoundDriver (GL thread, inside write): fixed hop on the sample clock
    HopAnalyzer::processHop(hop, sampleTime, tuning)       the one chain, one order
      filter bank → spectrum (multi-res, sound-engine-v2) → levels/descriptors → drums/onsets
      → Rhythm → Harmony → Form → Emotion → normalize/smooth per the schema → SoundFrame
    ─► FrameRing(4) + trigger counters
render frame (live: vsync; export: frame n / fps):
    FeatureInterpolator(playhead) ─► SoundFrame@t
    ─► Modulation: Director → NoteResponse → Sources (LFO, ADSR, macros, sound features, touch)
                  → Bus.evaluate(routes: source → transforms → target) → MotionField → safety::apply
    ─► SceneParams ─► FrameGraph: scene pass → trail → liquid → composite → UI blur
offline / tests / bench: FileDriver feeds the SAME HopAnalyzer; the SAME FrameGraph renders export
```

### Folder scaffold for `core/` (names to be settled in the domain-modelling session)

```
core/
  api/                 the C ABI, one *_api.cpp per domain (unchanged boundary, ADR 0003)
  sound/
    schema/            FeatureSchema.inc (X-macro: name, kind, norm, smooth, decay, interp), SoundFrame.h
    analysis/          HopAnalyzer and its stages: Fft, FilterBank, Spectrum, Drums, Rhythm, Harmony, Form, Emotion
    drive/             LiveDriver (the ring), FileDriver (offline), FeatureInterpolator, trigger counters
    Silence.hpp
  modulation/          Source, Transform, Target, Bus (the matrix registry), Lfo, Adsr, Macros, Reactions, ReactionDna, MotionField
  direction/           Director, NoteResponse, StyleSchedule (the decisions about the look)
  viz/
    scene/             Scene (slim) + capability interfaces, SceneRegistry, Catalogue
    scenes/            shader/, milkdrop/, sim/ (Fluid, Silk, Life, Acid, Myco, Cymatics, Water, CurlFlow)
    pass/              Pass interface: Trail, Composite, Liquid, UiBlur; FrameGraph
    camera/            CameraRig, EndlessZoom
    safety/            VisualSafety
    gl/                Program, ProgramStager, ProgramBinaryCache, Framebuffer, Texture, Quad, GlCaps, GlProber, ThermalGovernor
    Renderer.*         owns the above, runs FrameGraph
  studio/  audio/dsp/  library/  util/
  tests/               + tests/golden/ (pinned vectors)
tools/bench/           Python: sound_dump JSONL scored against tempo, key and section datasets
```

## 4. Plan, one PR each, CI green at every step

Order: lock the behaviour first, then move, then change. Every step updates `AGENTS.md` in the same PR
(the arch-map gate requires it).

| Step | What | Behaviour | Test that proves it |
|---|---|---|---|
| **0** | Domain model: `domain-modeling` + `grilling` session on this file; new GLOSSARY words (hop, feature, feature schema, route, source, target, transform, pass, frame graph, layer); **ADR 0006** "Hop analysis, one schema, one modulation bus, a frame graph" | none | review |
| **1** | **Golden vector.** Host `FileDriver` (WAV or synthetic signal → `SoundEngine`) + `sound_dump` JSONL; `golden_test.cpp` pins N frames at 1e-5 at a fixed dt | none | new golden test in `android.yml` |
| **2** | **Folder scaffold.** Pure `git mv` into §3's tree, CMake and includes only | none | every host test + golden unchanged |
| **3** | **Feature schema.** `FeatureSchema.inc` X-macro generates the `SoundFrame` field order, `ReactSource`, `kSourceTable`, saved names; `tools/` generates `lib_sound.glsl` defines and the Kotlin `SoundFrameLayout`, CI checks them; add the 7 missing sources; `GEODE_SOUND_FRAME_VERSION` | none (+7 sources) | golden unchanged; `static_assert` layout; generator check |
| **4** | **HopAnalyzer.** Move the FFT path from `step(dt)` to the hop inside `write()`; `step()` becomes `FeatureInterpolator`, using each field's interp policy; `Emotion` joins the chain | **changes**: frame-rate independent | golden re-pinned once, on purpose; new test: same song at 30 and 60 fps gives the same frames; device check of the FFT cost (hop size vs sound-engine-v2's FFT speedup is the open question) |
| **5** | **Slim `Scene`.** Capability interfaces (`PcmConsumer`, `TouchReceiver`, `CameraUser`, `FlowSource`, `MilkdropControl`) replace the no-op hooks; `SceneRegistry` becomes a table | none | host tests; emulator journeys |
| **6** | **Pass + FrameGraph.** One `Pass` interface (inputs, output, `enabled(p)`, `draw`); `Renderer::render` runs a list; offscreen uses the same list | none | `glslcheck` renders before and after match; thumbnails workflow |
| **7** | **Modulation bus.** `MOD_MATRIX_PLAN.md` E0 registry as the one bus: every engine registers sources and targets; `Route` = source → transforms → target; base value and modulation kept apart; `safety::apply` stays last | none for saved routes (codec maps old ids) | `viz_math_test`, route codec tests, `ReactivityTabTest` |
| **8** | **Bench.** `tools/bench/` scores `sound_dump` against Ballroom/GiantSteps (fetched outside the repo); a small fixture with floors in CI | none | the bench itself |
| later | Rates audit (F11); per-style control manifests beside the shader (F10); a layer stack with blend modes (F8); generated diagrams (F14) | — | — |

Steps 1–3 and 5–6 change no pixels and no numbers; 4 is the one deliberate behaviour change and is
gated by the golden and the 30/60 fps test. 7 is the mod matrix work already agreed, reframed as the bus.

## 5. Open questions for the owner

1. Hop size for the FFT path: keep Rhythm's 10 ms (100 Hz, more FFTs than today's ~60/s) or a coarser
   hop (~21 ms) with Rhythm on its own 10 ms? **Decided: 10 ms** (the owner's call on #324, which closed
   the 1/60 s variant #308). One clock for everything, madmom's 100 fps; the FFT cost (~1.7× at 60 fps)
   still wants the device check of step 4. `step()` holds the newest hop (triggers as their maximum,
   phases moved on to the frame's sample) rather than interpolating, so every frame rate reads exactly
   the same values.
2. Step 2 (moves) before step 3, or schema first so the moves carry the generated files? Moves first is
   the recommendation: the later diffs then read cleanly.
3. Per-style controls (F10) change Customize for users; is that wanted, or do global params stay?
