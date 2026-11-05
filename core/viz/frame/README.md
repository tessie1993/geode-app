# core/viz/frame (skeleton)

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). Only Cast's frame clock and HLS timing are here so far.

**Owns:** the one frame function every surface runs (screen, PiP, second screen, wallpaper, dream, Studio preview, export, thumbnails), and the pieces it is built from.
- Cast's frame timing, as the one surface that streams its frames: the fixed-rate frame clock that stamps each frame's PTS, and the HLS timing of the stream those frames become (each segment's media duration read from its fragment, the stream's fixed `EXT-X-TARGETDURATION`, the playlist text). Sockets and byte cutting stay in Kotlin's `cast/`.
- `FrameCtx`: this frame's effective params (from `core/signal`), the sound frame and source snapshot, camera pose, touch, flow, palette LUT, knobs. Replaces the ~20 `Scene` setters (`setFlow`, `setPaletteLut`, `setTouchField`, `setCamera`, …; `core/viz/Scene.hpp:22-62`) and the `SceneHost` callbacks (`:65-76`).
- `Pass`: `prepare(FrameCtx)`, `draw(inputs, target)`. Trail, Liquid, Composite, UiBlur, and the export post-passes (watermark, edge glow, loop seam, which are Kotlin GL today) all implement it.
- `LayerStack`: entries of scene + params + blend + mix. Replaces the three ad-hoc slots (layer, outgoing, born; `Renderer.hpp:333-336`, `:314-318`).
- `FrameGraph::run(FrameCtx)`: a fixed, readable pass list. `Renderer` keeps the scene cache, `ScenePrep`/`ProgramStager` and the targets, and calls it.

**Moves out of `Renderer` (as of `cf7e88f`):** the sound engine (to `core/sound`), the modulation chain (to `core/signal`), Liquid's state and born scenes (`stepLiquid`, `drawLiquid`, `RendererFrame.cpp`) into a `LiquidStage` pass, quality decisions folded into params (`p.rippleOverlayEnabled = false`) into pass toggles.

**Keep as they are:** `ScenePrep`, `ProgramStager`, `ProgramBinaryCache`, `GlCaps`/`GlProber`, `VisualSafety` (as the last step before drawing), the catalogue text table.

**Shader contract** (shaders stay in `app/src/main/assets/shaders`): a generated params uniform block from `core/signal/params` and a generated sound block from `core/sound/schema`, pasted by the prelude every style includes; include-once in `core/viz/ShaderSource.cpp`, exposed through the C ABI so `GlUtil` (Kotlin) and `tools/check_shaders.py` stop re-implementing the resolver. Family libraries (`lib_<family>`, `lib_neon`, `lib_relief`, `lib_zoom`, `lib_camera`) stay, on top of the prelude.

**fosfora:** `gpu/frame_prep.rs`, `gpu/frame_graph.rs` (`execute_and_composite`, "extracted … the headless scene renderer would have been a third copy"), `gpu/layer.rs` (`Layer`, `LayerStack`), `gpu/uniforms.rs` (append-only, layout-guarded).

**Here now:** `FixedRateClock.hpp`, the fixed-rate frame clock Cast paces by (#572; `core/tests/cast_clock_test.cpp`), and `HlsSegments.hpp`, Cast's HLS segment durations from the fragments' media time and the playlist text (#656; `core/tests/hls_segments_test.cpp`).

**Test that proves it:** the thumbnail render test (`StyleThumbnailRenderTest`) and an export of a fixed project produce the same frames before and after each move (pixel diff within tolerance).
