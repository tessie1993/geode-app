# core/output

Part of the target in [ADR 0006](../../docs/adr/0006-fosfora-aligned-engine.md). **Landed:** `FrameClock` (frame n's pts, programme time and samples heard, each from n alone), `LoopPlan` (`LoopSpec`, the seam weight and stash budget, `LoopExtendPlan`), `EncodePlan` (frame size, the fps retry, the muxer byte budget and hard stop) and `Chapters` (spans from mix clips and markers), behind `geode_api.h`'s export section and tested by `core/tests/output_test.cpp`. `VideoExporter`, `LoopRender`, `LoopExtend`, `MuxerBudget`, `ExportAspect.of`, `fallbackExportFps`, and `ChapterMarkers` call them (`app/src/main/cpp/geode_export_jni.cpp`). `PreRoll` (#999 F1, F2, F4: the engine hears up to 8 s before the range, or the track's opening and then a track boundary, before frame 0; `ExportFrame` decodes it and ends it, failing a render whose track gave no audio; tested by `core/tests/export_preroll_test.cpp`). `ExportFrame` (`geode_export_job_*`, `core/api/geode_export_job_api.h`): the prepare order and one call per frame (the audio to the frame's exact end, then the draw at its exact time); tested by `core/tests/export_frame_test.cpp`. The rest of the driver (the frozen `RenderJob`) is still to come.

**Here now:** `EncoderLadder.hpp` (#607), the frame rate and bit rate an export asks the video encoder for, rung by rung, over each candidate codec (`geode_encoder_attempts`; Kotlin passes in only each codec's `MediaCodecInfo.VideoCapabilities` ceiling and iterates the list). Test: `core/tests/encoder_ladder_test.cpp`.

**Here now:** `EncodeSink` (the encoder ladder's try-loop, the drain, samples held until the muxer starts, the end of stream) behind a codec seam, `android/MediaCodecSink` (AMediaCodec and AMediaMuxer, Android only), and `RecordSession`, Record's encoder thread on them (`geode_record_session_*`). Test: `core/tests/encode_sink_test.cpp`.

**Owns:** the offline driver: how a render steps the same engine the screen runs, faster than real time.
- A frame clock from samples: for frame `n` at `fps`, deliver samples up to `llround((n + 1) · rate / fps)` and render at that sample position, with dt passed in rather than worked out from wall time (`RendererFrame.cpp:50-51` clamps and guesses it today; the first frame is always 1/60 s).
- Pre-roll (landed, `PreRoll.hpp`): the analysis starts before the range, listening without drawing, so no render begins on a cold engine.
- A frozen `RenderJob`: look, routes, hand ownership, macros, director pin, note response and the timeline, snapshotted once at start. Today a render reads live state per segment and per frame (`ui/ExportController.kt:791-794, 843`) and process-wide globals mid-render.

**Kotlin keeps:** `export/VideoExporter`, `LoopRender` (which only starts, polls and cancels the native loop job, `LoopRender.hpp`), `ExportScene` (the renderer, the track's descriptor and the look handed over), `EncoderSurface`, MediaCodec/Muxer and MediaStore. They drive this; they stop resolving looks and keyframes themselves.

**fosfora:** `headless/driver.rs`, `headless/scene_renderer.rs` (calls the app's own `frame_prep` and `frame_graph`), `headless/loop_driver.rs` (phase-locked loop renders), `headless/schedule.rs`.

**Test that proves it:** the host golden test from `core/sound/hop` run through this driver at 24, 30 and 60 fps gives the same sound frames at the same sample positions as the live path.
