# core/output

Part of the target in [ADR 0006](../../docs/adr/0006-fosfora-aligned-engine.md). **Landed:** `FrameClock` (frame n's pts, programme time and samples heard, each from n alone), `LoopPlan` (`LoopSpec`, the seam weight and stash budget, `LoopExtendPlan`), `EncodePlan` (frame size, the fps retry, the muxer byte budget and hard stop) and `Chapters` (spans from mix clips and markers), behind `geode_api.h`'s export section and tested by `core/tests/output_test.cpp`. `VideoExporter`, `LoopRender`, `LoopExtend`, `MuxerBudget`, `ExportAspect.of`, `fallbackExportFps`, `ChapterMarkers` and `OffscreenSceneRenderer` call them (`app/src/main/cpp/geode_export_jni.cpp`). The driver itself (pre-roll, the frozen `RenderJob`) is still to come.

**Also here:** `EncoderLadder.hpp` (#607), the frame rate and bit rate an export asks the video encoder for, rung by rung, over each candidate codec (`geode_encoder_attempts`; Kotlin passes in only each codec's `MediaCodecInfo.VideoCapabilities` ceiling and iterates the list). Test: `core/tests/encoder_ladder_test.cpp`.

**Still to come:**

**Owns:** the offline driver: how a render steps the same engine the screen runs, faster than real time.
- A frame clock from samples: for frame `n` at `fps`, deliver samples up to `llround((n + 1) · rate / fps)` and render at that sample position, with dt passed in rather than worked out from wall time (`RendererFrame.cpp:50-51` clamps and guesses it today; the first frame is always 1/60 s).
- Pre-roll: start the analysis some seconds before the range, listening without drawing, so a part render or a Studio segment does not begin with a cold tempo, key and form.
- A frozen `RenderJob`: look, routes, hand ownership, macros, director pin, note response and the timeline, snapshotted once at start. Today a render reads live state per segment and per frame (`ui/ExportController.kt:791-794, 843`) and process-wide globals mid-render.

**Kotlin keeps:** `export/VideoExporter`, `LoopRender`, `StudioExporter`, `EncoderSurface`, MediaCodec/Muxer/Transformer and MediaStore. They drive this; they stop resolving looks and keyframes themselves.

**fosfora:** `headless/driver.rs`, `headless/scene_renderer.rs` (calls the app's own `frame_prep` and `frame_graph`), `headless/loop_driver.rs` (phase-locked loop renders), `headless/schedule.rs`.

**Test that proves it:** the host golden test from `core/sound/hop` run through this driver at 24, 30 and 60 fps gives the same sound frames at the same sample positions as the live path.
