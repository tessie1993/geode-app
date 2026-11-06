# core/viz/frame

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). [ADR 0008](../../../docs/adr/0008-frame-pacing-heat-and-rest.md) sets the pacing, heat and rest rules.

**Here now:** `Pacer.hpp`, the frame clock every surface shares. It decides which vsyncs a surface draws on (a whole-number divide of the measured panel rate) and owns the one dt clamp, `clampDt`, which `Renderer::beginFrame` uses too. Android keeps the Choreographer callback (`render/FramePacer.kt`). It is reached through `geode_pacer_*` and tested by `core/tests/pacer_test.cpp`. `Pass.hpp` (the interface below, with `Surface`, `FrameCtx`, `PassInputs`, `PassTarget`) and its first pass, `EdgeGlowPass` (the wallpaper's edge look, once Kotlin GL; pure maths in `EdgeGlowMath.hpp`, pinned by `core/tests/edge_glow_test.cpp`, #439). `Renderer` still calls it directly, after the composite; there is no `FrameGraph` yet.

**Owns:** the one frame function every surface runs (screen, PiP, second screen, wallpaper, dream, Studio preview, export, thumbnails), and the pieces it is built from.
- `FrameCtx`: this frame's effective params (from `core/signal`), the sound frame and source snapshot, camera pose, touch, flow, palette LUT, knobs. Replaces the ~20 `Scene` setters (`setFlow`, `setPaletteLut`, `setTouchField`, `setCamera`, …; `core/viz/Scene.hpp:22-62`) and the `SceneHost` callbacks (`:65-76`).
- `Pass`: `prepare(FrameCtx)`, `draw(inputs, target)`. Trail, Liquid, Composite, UiBlur, and the post-passes (watermark and loop seam, still Kotlin GL; edge glow, native since #439) all implement it.
- `LayerStack`: entries of scene + params + blend + mix. Replaces the three ad-hoc slots (layer, outgoing, born; `Renderer.hpp:333-336`, `:314-318`).
- `FrameGraph::run(FrameCtx)`: a fixed, readable pass list. `Renderer` keeps the scene cache, `ScenePrep`/`ProgramStager` and the targets, and calls it.

**Moves out of `Renderer` (as of `cf7e88f`):** the sound engine (to `core/sound`), the modulation chain (to `core/signal`), Liquid's state and born scenes (`stepLiquid`, `drawLiquid`, `RendererFrame.cpp`) into a `LiquidStage` pass, quality decisions folded into params (`p.rippleOverlayEnabled = false`) into pass toggles.

**Keep as they are:** `ScenePrep`, `ProgramStager`, `ProgramBinaryCache`, `GlCaps`/`GlProber`, `VisualSafety` (as the last step before drawing), the catalogue text table.

**Shader contract** (shaders stay in `app/src/main/assets/shaders`): a generated params uniform block from `core/signal/params` and a generated sound block from `core/sound/schema`, pasted by the prelude every style includes; include-once in `core/viz/ShaderSource.cpp`, exposed through the C ABI so `GlUtil` (Kotlin) and `tools/check_shaders.py` stop re-implementing the resolver. Family libraries (`lib_<family>`, `lib_neon`, `lib_relief`, `lib_zoom`, `lib_camera`) stay, on top of the prelude.

**fosfora:** `gpu/frame_prep.rs`, `gpu/frame_graph.rs` (`execute_and_composite`, "extracted … the headless scene renderer would have been a third copy"), `gpu/layer.rs` (`Layer`, `LayerStack`), `gpu/uniforms.rs` (append-only, layout-guarded).

**Test that proves it:** the thumbnail render test (`StyleThumbnailRenderTest`) and an export of a fixed project produce the same frames before and after each move (pixel diff within tolerance).
