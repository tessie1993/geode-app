# core/viz/frame

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). [ADR 0008](../../../docs/adr/0008-frame-pacing-heat-and-rest.md) sets the pacing, heat and rest rules.

**Here now:** `Pacer.hpp`, the frame clock every surface shares. It decides which vsyncs a surface draws on (a whole-number divide of the measured panel rate) and owns the one dt clamp, `clampDt`, which `Renderer::beginFrame` uses too. Android keeps the Choreographer callback (`render/FramePacer.kt`). It is reached through `geode_pacer_*` and tested by `core/tests/pacer_test.cpp`. It paces to the effective cap, `min(surface cap, thermal cap)` (`effectiveCapFps`), and records it in the state so `FramePacer` re-asks `Surface.setFrameRate` when the thermal cap moves.

**Here now:** `Thermal.hpp`, the process's one thermal state (`ThermalState::process()`, #440). It holds PowerManager's readings, the once-a-second cadence (`claimSample`), the NaN rule (a missed window keeps the last real headroom for 10 s), the dwell and the settled tier; every `Renderer`'s `ThermalGovernor` (`core/viz/ThermalGovernor`) reads it and adds only what that renderer alone sees (its frame times, an export's pin to Full). The tier ladder `thermalTierInfo` is the owner's fps-first order: fps to 30, then render scale, then march scale, then post FX. Android keeps only the reads (`render/ThermalGovernor.kt`). It is reached through `geode_thermal_*` and tested by `core/tests/thermal_test.cpp`.

**Owns:** the one frame function every surface runs (screen, PiP, second screen, wallpaper, dream, Studio preview, export, thumbnails), and the pieces it is built from.
- `FrameCtx`: this frame's effective params (from `core/signal`), the sound frame and source snapshot, camera pose, touch, flow, palette LUT, knobs. Replaces the ~20 `Scene` setters (`setFlow`, `setPaletteLut`, `setTouchField`, `setCamera`, …; `core/viz/Scene.hpp:22-62`) and the `SceneHost` callbacks (`:65-76`).
- `Pass`: `prepare(FrameCtx)`, `draw(inputs, target)`. Trail, Liquid, Composite, UiBlur, and the export post-passes (watermark, edge glow, loop seam, which are Kotlin GL today) all implement it.
- `LayerStack`: entries of scene + params + blend + mix. Replaces the three ad-hoc slots (layer, outgoing, born; `Renderer.hpp:333-336`, `:314-318`).
- `FrameGraph::run(FrameCtx)`: a fixed, readable pass list. `Renderer` keeps the scene cache, `ScenePrep`/`ProgramStager` and the targets, and calls it.

**Moves out of `Renderer` (as of `cf7e88f`):** the sound engine (to `core/sound`), the modulation chain (to `core/signal`), Liquid's state and born scenes (`stepLiquid`, `drawLiquid`, `RendererFrame.cpp`) into a `LiquidStage` pass, quality decisions folded into params (`p.rippleOverlayEnabled = false`) into pass toggles.

**Keep as they are:** `ScenePrep`, `ProgramStager`, `ProgramBinaryCache`, `GlCaps`/`GlProber`, `VisualSafety` (as the last step before drawing), the catalogue text table.

**Shader contract** (shaders stay in `app/src/main/assets/shaders`): a generated params uniform block from `core/signal/params` and a generated sound block from `core/sound/schema`, pasted by the prelude every style includes; include-once in `core/viz/ShaderSource.cpp`, exposed through the C ABI so `GlUtil` (Kotlin) and `tools/check_shaders.py` stop re-implementing the resolver. Family libraries (`lib_<family>`, `lib_neon`, `lib_relief`, `lib_zoom`, `lib_camera`) stay, on top of the prelude.

**fosfora:** `gpu/frame_prep.rs`, `gpu/frame_graph.rs` (`execute_and_composite`, "extracted … the headless scene renderer would have been a third copy"), `gpu/layer.rs` (`Layer`, `LayerStack`), `gpu/uniforms.rs` (append-only, layout-guarded).

**Test that proves it:** the thumbnail render test (`StyleThumbnailRenderTest`) and an export of a fixed project produce the same frames before and after each move (pixel diff within tolerance).
