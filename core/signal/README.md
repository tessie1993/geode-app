# core/signal

The layer between sound and pictures: what the user set, what moves it, and the one place both meet.

## The scene params resolver (`Resolver.hpp`, `Resolver.cpp`, #344)

The per-frame chain, moved out of the GL renderer unchanged (`core/viz/RendererFrame.cpp`, `Renderer::resolveParams`, which is now a thin caller):

```
param fade → Director → NoteResponse → macros → LFO → ADSR
  → reaction routes → macro routes → director routes → MotionField → safety::apply
```

- **In, once a frame:** the requested `SceneParams`, the sound frame, dt, one `EngineSettings` value (macro knobs, the director's amount and seed, the Note & key switch, the hand mask, reduced motion, the note entropy), a `StyleContext` (style id, shader family or not, spin rate, "style switched").
- **Out:** the final params (before the renderer's device masks), the director's style moments, the camera rig's music moves and the note uniforms (`noteUniforms()`).
- **It owns** the stages' state (fade and morph timer, `Director`, `NoteResponse`, the LFO and envelope engines, the user and macro reaction engines, `MotionField`), the LFO, envelope and route configs, and the reset on a new surface.
- **Single-threaded, GL-free:** no mutex, no atomics, no GL header. The renderer keeps its lock and atomics, latches one `EngineSettings` a frame through its `SettingsLatch` (the same copy re-tunes its sound engine) and hands the resolver copies. The director takes the hand mask as an input; nothing in the chain reads a global.
- **Stays in the renderer:** the camera rig, the thermal and post-FX masks, Liquid's hot policy, the post pass's integrators.
- **Tested** as a chain by `core/tests/resolver_test.cpp` (CI "Native host tests"): order, reduced motion once, settings pinned against the globals, an export's latch and director rule, 600 bit-identical frames, safety last.

## Engine settings (`EngineSettings.hpp`, `EngineSettings.cpp`, #350)

- **`EngineSettings`**: what the chain reads from outside, once a frame (macro knobs, director amount and seed, Note & key switch, hand mask, reduced motion, note entropy).
- **`SettingsLatch`** (one per renderer): live, `latch()` reads the process globals (`globalSettings()`) and draws a fresh note entropy whenever the play seed changes. An export calls `pinForExport(entitled, durationMs)` once at prepare (`geode_viz_pin_engine_settings`); from then on `latch()` returns that copy, so the live macros, Note & key switch, hand mask and director never reach the render. Reduced motion is the renderer's own and passes through either way.
- **An export's director** is `director::forExport`: off with the user's switch off; full throughout for Pro and the trial; for free, full only when the whole export fits the free taste (`director::kFreeTasteMs`, 60 s), else off. Never the live amount, always the play's seed. The note entropy is 0, so an export is deterministic.
- **Kotlin** forwards only the entitlement and the length; the pin hands back its hand mask for the shell's route merge.

The stages themselves live in `core/viz` (GL-free files). [`timeline/`](timeline/README.md) holds the programme-time sources an export reads: keyframes and speed ramps.
