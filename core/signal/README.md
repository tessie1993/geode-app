# core/signal (skeleton, empty)

Part of the target in [ADR 0006](../../docs/adr/0006-fosfora-aligned-engine.md). The layer between sound and pictures: what the user set, what moves it, and the one place both meet. Nothing here yet.

| Folder | Owns | fosfora |
|---|---|---|
| [`params/`](params/README.md) | The param schema and the store: base value + this frame's modulation, one clamp | `params/types.rs`, `params/store.rs` |
| [`bindings/`](bindings/README.md) | Routes: source → transforms → target, weight, combine rule; the bus that evaluates them | `bindings/` |
| [`modulators/`](modulators/README.md) | Everything that produces a source value or a route: LFO, ADSR, macros, director, note response, motion field, touch | `bindings/lfo.rs`, `bindings/sources.rs` |
| [`timeline/`](timeline/README.md) | Programme-time sources: style switches, keyframes, take params | `headless/schedule.rs` |

Per frame, in this order:

```
sources.snapshot(soundFrame, touch, timeline@t)      // computed once, shared by every route
store.clearModulation()
for route in bus.routes: store.modulate(route.target, route.combine, transforms(route.source))
params = store.effective()                           // base ⊕ Σ modulation, then one clamp by schema range
params = safety::apply(params)                       // the flash and motion limits, last
```

Replaces the chain in `core/viz/RendererFrame.cpp:178-197` (as of `cf7e88f`): director → notes → macros → LFO → ADSR → reactions → macro routes → director routes → motion field → safety, ten `SceneParams → SceneParams` mutators, each clamping with its own literals.

Depends on `core/sound` (the frame) only. `core/viz` depends on it; it never depends on `core/viz`.
