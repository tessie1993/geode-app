# core/signal

Part of the target in [ADR 0006](../../docs/adr/0006-fosfora-aligned-engine.md). The layer between sound and pictures: what the user set, what moves it, and the one place both meet. Built so far: `params/` (the schema, the param state machine, the store), `events/` and `bindings/EventRoutes`, host-tested by `core/tests/signal_test.cpp`; nothing calls them yet (the resolver below moves the existing chain unchanged).

## The scene params resolver (`Resolver.hpp`, `Resolver.cpp`, #344)

The per-frame chain, moved out of the GL renderer unchanged (`core/viz/RendererFrame.cpp`, `Renderer::resolveParams`, which is now a thin caller):

```
param fade → Director → NoteResponse → macros → LFO → ADSR
  → reaction routes → macro routes → director routes → MotionField → safety::apply
```

- **In, once a frame:** the requested `SceneParams`, the sound frame, dt, one `EngineSettings` value (macro knobs, the director's amount and seed, the Note & key switch, the hand mask, reduced motion, the note entropy), a `StyleContext` (style id, shader family or not, spin rate, "style switched").
- **Out:** the final params (before the renderer's device masks), the director's style moments, the camera rig's music moves and the note uniforms (`noteUniforms()`).
- **It owns** the stages' state (fade and morph timer, `Director`, `NoteResponse`, the LFO and envelope engines, the user and macro reaction engines, `MotionField`), the LFO, envelope and route configs, and the reset on a new surface.
- **Single-threaded, GL-free:** no mutex, no atomics, no GL header. The renderer keeps its lock and atomics, latches `EngineSettings` from the process globals once a frame (the same copy re-tunes its sound engine) and hands the resolver copies. The director takes the hand mask as an input; nothing in the chain reads a global.
- **Stays in the renderer:** the camera rig, the thermal and post-FX masks, Liquid's hot policy, the post pass's integrators.
- **Tested** as a chain by `core/tests/resolver_test.cpp` (CI "Native host tests"): order, reduced motion once, settings pinned against the globals, 600 bit-identical frames, safety last.

The stages themselves still live in `core/viz` (GL-free files); the folders below are where ADR 0006 rebuilds them behind this interface. Pinning an export's settings at prepare is #344's second PR.

| Folder | Owns | fosfora |
|---|---|---|
| [`params/`](params/README.md) | The param schema and the store: base value + this frame's modulation, one clamp | `params/types.rs`, `params/store.rs` |
| [`events/`](events/README.md) | Events: the moments (kick, downbeat, drop, key change, style switch) raised once a frame into a queue, and the bus that hands them out in a fixed order | `audio/features.rs` (onsets), `bindings/sources.rs` |
| [`bindings/`](bindings/README.md) | Routes: source → transforms → target, weight, combine rule; the bus that evaluates them | `bindings/` |
| [`modulators/`](modulators/README.md) | Everything that produces a source value or a route: LFO, ADSR, macros, director, note response, motion field, touch | `bindings/lfo.rs`, `bindings/sources.rs` |
| [`timeline/`](timeline/README.md) | Programme-time sources: style switches, keyframes, take params | `headless/schedule.rs` |

Per frame, in this order:

```
store.advance(dt)                                    // glides and trials move on (params/ParamState)
sources.snapshot(soundFrame, touch, timeline@t)      // computed once, shared by every route
events.detect(soundFrame) → bus.dispatch()           // event routes change bases or start pulses
store.clearModulation()
eventRoutes.modulate(dt)                             // live pulses
for route in bus.routes: store.modulate(route.target, route.combine, transforms(route.source))
params = store.effective()                           // base ⊕ Σ modulation, then one clamp by schema range
params = safety::apply(params)                       // the flash and motion limits, last
```

Replaces the chain in `Resolver::step` (moved there from `core/viz/RendererFrame.cpp:178-197` as of `cf7e88f`): director → notes → macros → LFO → ADSR → reactions → macro routes → director routes → motion field → safety, ten `SceneParams → SceneParams` mutators, each clamping with its own literals.

Target: depends on `core/sound` (the frame) only; `core/viz` depends on it, never the reverse. Today `Resolver` still includes the GL-free stage headers in `core/viz`, and never a GL one.
