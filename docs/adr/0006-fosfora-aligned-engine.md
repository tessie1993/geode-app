---
status: proposed
---

# A fosfora-shaped engine: hop chain, schema, binding bus, frame graph

[ADR 0003](0003-engine-internals-in-cpp.md) decided which language engine code is written in. It did not decide where the boundaries inside the engine run, and the code grew without them. On 2026-11-05 the owner asked for the architecture of [kevinraymond/fosfora](https://github.com/kevinraymond/fosfora) (studied in `docs/research/fosfora/`, which arrives with [#300](https://github.com/tessie1993/geode-app/pull/300)). This ADR proposes that shape for `core/` and the Kotlin around it. It adds to ADR 0003; it does not replace it.

## What is wrong today (as of `cf7e88f`)

- **Analysis rides the render loop.** Each `core/viz/Renderer` owns a `sound::SoundEngine`, built on the first audio write (`core/viz/Renderer.cpp:74-81`) and stepped once per drawn frame with the frame's dt (`core/viz/RendererFrame.cpp:47-64`). Only `Rhythm` runs on a sample hop (`core/sound/SoundEngine.cpp:161-177`). Features depend on frame rate, every surface runs its own analysis, and silence has four definitions.
- **Modulation is a fixed chain of mutators on one struct.** Director → notes → macros → LFO → ADSR → reactions → macro routes → director routes → motion field → safety, each `SceneParams → SceneParams` with its own clamps (`core/viz/RendererFrame.cpp:178-197`, `core/viz/Lfo.cpp:54-60`). Nothing keeps the user's value apart from this frame's modulation.
- **The look has no owner.** Kotlin controllers, process-wide native globals (`macros::global`, `director::global`, `hand::global`) and per-renderer setters all write parts of it.
- **Tables are kept in sync by hand.** The sound frame (`SoundFrame.h`, `SoundFrameLayout.kt`, `lib_sound.glsl`, `ReactSource`), the params (`Params.hpp`, `ParamsFields.cpp`, `SceneParams.kt`, `SceneParamsCodec.kt`, `AnimatableParams.kt`, slider literals) and the uniform names.
- **Live, preview and export resolve the look differently** (`ui/StudioPreviewController.kt:236-250` vs `ui/ExportController.kt:840-845`).

Release 52 and earlier ran analysis on its own 16 ms loop, apart from drawing (`AnalysisEngine.kt` at `db3167f8^`). That was closer to this target. It was replaced on 2026-10-31 (`e3b5fc51`, `db3167f8`).

## Decision (proposed)

The engine is four native layers with one-way dependencies, under one C ABI, and the Kotlin shell talks to it through one typed command channel.

```
core/audio/ingest  ─► core/sound (hop, schema, interp)  ─► core/signal (params, bindings, modulators, timeline)  ─► core/viz (frame graph, passes, scenes)  ─► core/output
        ▲                                                                                                                                                    │
        └──────────────────────────────── core/api: geode_api.h (handles + one command channel), JNI marshals only ◄───────────────────────────────────────┘
```

| Layer | Owns | fosfora counterpart |
|---|---|---|
| `core/audio/ingest` | One PCM ring per source, epoch and rate stamped together, int16 → float | `audio/capture.rs`, `audio/decimate.rs` |
| `core/sound/hop` | Every detector, run in one order on a fixed sample hop, on its own thread | `audio/hop.rs`, `audio/mod.rs` (`audio_thread`) |
| `core/sound/schema` | One feature table that generates the C struct, Kotlin constants, GLSL defines and the source list, with each field's normalisation and interpolation policy | `audio/schema.rs`, `audio/features.rs` |
| `core/sound/interp` | Renderers read the frame at a presentation time between two hops | `audio/interp.rs`, `audio/sync_delay.rs` |
| `core/signal/params` | One param schema; a store with base values and this frame's modulation; one clamp | `params/types.rs`, `params/store.rs` |
| `core/signal/bindings` | Routes: source → transforms → target, weight, combine rule | `bindings/` |
| `core/signal/modulators` | LFO, ADSR, macros, director, note response, motion field, touch as sources and route producers, never struct mutators | `bindings/lfo.rs`, `bindings/sources.rs` |
| `core/signal/timeline` | Style switches, keyframes and take params on programme time, shared by preview and export | `headless/schedule.rs` |
| `core/viz/frame` | `FrameCtx`, `Pass`, `LayerStack`, one frame function for every surface | `gpu/frame_prep.rs`, `gpu/frame_graph.rs`, `gpu/layer.rs` |
| `core/output` | The offline driver: frame clock from samples, pre-roll, post-pass sinks | `headless/` |

Kotlin (see [`docs/architecture/kotlin-target.md`](../architecture/kotlin-target.md)): the UI writes a `Look` through one repository, which sends typed commands to each renderer. The engine never reads Kotlin prefs.

What we deliberately do not copy from fosfora: its god `App` (`app.rs`), its three naming schemes for features, and last-binding-wins on a target (`params/store.rs`). Geode sums weighted modulation and clamps once.

## Order

Each step is its own PR with the test named in its folder's README. The skeleton PR adds only these READMEs. The first code step is the golden test (`core/sound/hop/README.md`), because every later step must keep its numbers or change them on purpose.

## Consequences

- ADR 0001's "`:engine:*` and the native core stay as they are" no longer holds: the split this ADR describes happens inside `core/` and `:engine:*`.
- ADR 0003's classification table stays; its "Shell" rows for `audio/*` move partly to `core/audio/ingest` (the ring, the int16 conversion, the capture level meters).
- Export becomes a driver of the same chain, so "live equals export" is proven by a test, not by frame-locking.
