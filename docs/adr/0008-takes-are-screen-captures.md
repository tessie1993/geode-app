---
status: accepted
---

# Takes are screen captures of the live visual

Amends the Record parts of [ADR 0005](0005-wave-1-restructures-before-release.md) ("a take becomes a visual clip on track time") and [ADR 0007](0007-studio-drops-video-editing.md) ("a stopped take still becomes a project and renders to MP4 in the background"). Spec #455 § Record, ticket #456, decided by the owner on 2026-11-05 (session board #433): the screen-capture take is wave 1 and ships before launch.

## Context

Until now a take was look automation. The recorder sampled the style, params and MilkDrop preset on song time (`data/PerformanceTake`), and stopping it rendered that timeline offscreen through `ExportController.renderProject`. That render left out what made the performance: the auto-director's choices, the reactions as they played, the camera, touch, Note & key reactions rolled for that play, and the seed. So the MP4 could differ from what the performer saw, and a take took as long to render as an export.

## Decision

A take encodes the live stage itself.

- **Picture.** The in-app stage renderer (`geode_viz_set_take_stage`; wallpaper, dream and cast never are) copies each finished frame into an AVC encoder's input surface (`core/viz/TakeSurface`): an EGL window surface sharing the screen's context, filled by a framebuffer blit with the screen as the read surface. The copy is taken after the composite and the flash limiter's probe and before the UI blur, so the take is exactly the visual on screen and never the glass sheet, the mini player or anything Compose draws. Nothing renders twice, and no engine state steps twice.
- **Size and rate.** Screen size, at the display's rate up to 60 fps. An encoder that refuses the screen's size gets 3/4 of it, then 3/4 again, on the 16-pixel grid (a software encoder caps the long side near 2048).
- **Sound: what the performer heard.** A second, record-only tap at the end of the playback chain (`audio/TakeAudioTap`, after the user's DSP: EQ, ReplayGain, crossfeed, the crossfade gain) feeds the take. The analysis tap stays first and the chain stays integer PCM, as [`AUDIO_CHAIN.md`](../AUDIO_CHAIN.md) requires; the new tap feeds an encoder, never a sound engine.
- **One clock.** Both tracks are on BOOTTIME from the moment the take began: a frame carries the engine frame time it was rendered for, a sound block the time the tap wrote it. The record tap sits in the same chain as the analysis tap, so the MP4 pairs each frame with the sound that frame reacted to. Sound follows on sample by sample; a pause or a stall is filled with silence, and the first block after it lands exactly at its own time. The rules are `core/output/TakeClock`, pinned by `core/tests/take_clock_test.cpp`.
- **All native.** Encoders (AMediaCodec), muxer (AMediaMuxer) and the take's encoder thread are `core/output/TakeSession`, behind `geode_take_*`. Kotlin opens the scratch file, starts and ends the take, and copies the finished MP4 to the save target (`data/TakeCapture`).
- **Trial limits.** A trial take is limited exactly like a trial render (`FeatureGate.renderLimits`, owner's decision of 2026-11-05): it stops at one minute and carries the full-frame wordmark. Native decides both: `TakeClock` keeps no frame or sound past the cap and says when it is reached, and `TakeSurface` tiles the wordmark into the take's frames only, never the screen. The shell passes the gate's limits and rasterises the tile with the export's own `WatermarkOverlay`, which owns the font.
- **Unchanged.** Record stays Pro (and the trial). A stopped take still becomes a project (`data/TakeStore`), and its video is that project's render, which is how My videos lists it. The first stop still asks where takes go (`RecordSaveSheet`); nothing renders after the stop, the video is only copied there.

## Consequences

- The take is the performance: director, reactions, camera, touch and seed are in it because they were on screen.
- Dismissing the save sheet no longer leaves an unrendered take: the video already exists, so it goes to the Videos library this once and the next stop asks again.
- Mic and other-app takes (#458) keep that input's audio: the capture pump hands it to the take (`geode_take_write_input`), and a take begun while the mic or another app's audio owns the sound hears only that source (`TakeSource`, fixed at begin, so the player starting under it is not mixed in). Their choices sit on wall time and their project has no song lane, which is what hides "Render again" for them.
- Pause, seek and track change follow Settings › Behaviour › Record (#459), decided natively by `core/output/TakeRules`: by default pause holds the capture (picture and sound stop and the take's clock with them, so the file has no gap), a seek is a hard cut, and a new song ends and saves the take; "Keep rolling" records through all three, and "Seek ends" holds on pause and ends on any seek. The rule is read when a take begins.
- A track take also keeps its choices on song time (#457): `core/signal/timeline/TakeTimeline` records the style, preset and every param from the song position where each changed, with the play's seed, and keeps them as `takes/<takeId>.choices`. My videos' "Render again" opens Render with the take chosen, so the export replays those choices offline (switches through `StyleSchedule`, params per frame from the timeline, the director on the take's seed) at whatever format is picked. It is not a re-encode of the capture. Touches are not in the choices yet. The older look automation stays beside them for the project's take clip and for takes recorded before them.
- The capture costs one blit and one encode per frame while recording. A device frame-time check of a take at screen size on a mid-range phone belongs in `docs/DEVICE_CHECKS.md`.
