# Kotlin logic map: `app/` (2026-11-12)

Scanned commit: `37d2df7a1934896e261d2ca6670386ba3517b1ca` (`origin/main` on 2026-11-12; the Graphify index is built from the same commit).
Scope: every Kotlin file under `app/src/main/java/dev/geode/` (360 files, 67,796 lines). `engine/` has its own map and port (branch `ccr-0a1f1dcf-87b4sp-engine-to-cpp`) and is not repeated here.
This is a map. Nothing was ported, built or run.

All paths below are relative to `app/src/main/java/dev/geode/` unless they start with `core/` or `engine/`.

## How it was read (be aware of the limits)

- **Read in full (code view, comments stripped):** `export/` (the 9 files in the table), `cast/CastStream`, `CastEncoder`, `CastMirror`, `LiveSegments`, `WebRtcLink`, `record/RecordEncoder`, `wallpaper/` (all 11), `audio/AudioCapturePump`, `MicCapture`, `PlaybackCapture`, `AiffExtractor`, `PlayoutSink`, `OboeAudioOutput`, `audio/dsp/*`, `viz/*`, `playback/PlaybackEngine`, `SleepTimer`, `AutoDirectorFeed`, `ui/ExportController`, `RecordController`, `AutoVisualsController`, `StageRun`, `EnginePlumbing`, `ModulationController`, `VisualSettingsController`, `data/PaletteStore`, `pro/AutoDirectorPolicy`, `editor/Keyframes`, `widget/WidgetVisual` (render part).
- **Outline plus signal grep only** (declarations, every `GeodeNative.` call, every thread, dispatcher, `MediaCodec`, EGL, maths call): the rest of `ui/` (117 files), `data/`, `playback/` (other 18), `cast/` (other 7), `billing/`, `di/`, `model/`, `pro/`, `tile/`, `widget/` (other 4), `editor/` (other 4), `ui/theme`, `ui/tour`, `ui/pro`, `ui/cast`, `ui/wallpaper`. A rule hidden in a Compose lambda with none of those signals can have been missed.
- **Engine-logic line counts are estimates**: the summed line ranges of the functions named. They rank files; they are not exact.
- Graphify was used for callers of the render drivers (`EncoderSurface`, `VisualizerRenderer`, `OffscreenSceneRenderer`, `FramePacer`). It missed one use (`FramePacer` in `wallpaper/VisualizerWallpaperService.kt:46`), so constructor sites were also grepped.
- `AGENTS.md` names `StudioExporter`, `ProjectComposition`, `GlTransitionEffect` and `TrackAudio` under `export/`. None exists in `app/` at this commit (`export/` has 18 files). `TrackAudio` lives in `engine/`.

## Summary

Engine logic left in `app/`: about 3,700 of 67,796 lines (5 %). Almost all of it is in five packages, and almost all of it is a loop: a frame loop, an encoder drain loop, a decode loop or a capture loop.

| Package | Files | Lines | Engine-logic lines (est.) | Verdict |
|---|---|---|---|---|
| `export` | 18 | 4,293 | 1,550 | **Engine.** Two frame loops, four codec loops, three audio interleaves |
| `cast` | 12 | 2,594 | 480 | **Engine + network shell.** Own render thread, own sound feed, own look push |
| `ui` | 123 | 33,598 | 520 | Shell, with 6 mixed controllers |
| `wallpaper` | 11 | 1,648 | 355 | **Mixed.** Two Kotlin actuators of one native run rule; own sound feed choice |
| `audio` | 12 | 1,368 | 325 | Mixed. Capture loops, a second AIFF parser |
| `record` | 1 | 268 | 127 | **Engine.** Encoder drain loop |
| `playback` | 21 | 2,686 | 60 | Shell, three small tickers |
| `data` | 66 | 9,721 | 50 | Shell (stores). One param helper |
| `audio/dsp` | 2 | 218 | 45 | Media3 glue; one format rule |
| `editor` | 5 | 888 | 44 | Dormant model (video editing was dropped) |
| `widget` | 5 | 1,227 | 30 | Shell; one drawn "glow" picture |
| `pro` | 8 | 544 | 12 | Shell; one rule that also exists natively |
| `viz` | 2 | 138 | 5 | Thin |
| root, `billing`, `data/backup`, `data/prefs`, `di`, `model`, `tile`, `ui/cast`, `ui/pro`, `ui/theme`, `ui/tour`, `ui/wallpaper` | 74 | 8,605 | 0 | Shell |

## Per-file table (engine logic, mixed, duplicate)

Risk: H = A/V sync, pacing or saved output can change; M = behaviour edge cases; L = mechanical. "Saved data" says whether any stored file or pref changes shape.

### Frame and encode drivers

| File (lines) | What it decides in Kotlin | Native owner | Kotlin left | Blocker and thinnest hand-over | Risk | Saved data |
|---|---|---|---|---|---|---|
| `export/VideoExporter.kt` (632) | The export frame loop `encodeInto` 350–537: per frame render, PTS, swap, encoder drain, audio interleave, progress (434–470); drain to end 471–500; encoder ladder loop 551–567; `videoFormat` 539–548; `AudioFeed` 585–629; bit-rate and ratio tables 77–97. Runs on `Dispatchers.Default` (187–188) | New `core/output/ExportJob` over the existing `core/viz/frame` clocks and `core/output` ladder | MediaStore row and `IS_PENDING` (211–257), the output fd, progress callback, cancel | `MediaCodec`/`MediaMuxer` have NDK twins (`AMediaCodec_createInputSurface`, `AMediaMuxer_new(int fd, …)`, see Research). `mediandk` is not linked today. Hand over: an fd from `ContentResolver.openFileDescriptor` | H | None (same MP4) |
| `export/LoopRender.kt` (753) | Second frame loop `drawFrames` 430–464; `renderStops`/`renderStop` 305–423; encoder probing `negotiate`/`accepts` 512–550; `StopWriter` drain and mux 590–712; `SeamStash` 713–741 | Same `ExportJob`, with `core/viz/frame/LoopSeam` (exists) | Same as above | Same | H | None |
| `export/LoopExtend.kt` (642) | Remux of the repeated loop: `mux` 236–, `LoopReader.copyInto` 479–498 (PTS offset per repeat), `AudioReel.writeUpTo` 571–608, `joinable`/`sameAudioShape` 334–358. Plan maths is already native (75, 81, 92) | `core/output` (extend the plan to do the copy) | fd in, fd out | `AMediaExtractor` + `AMediaMuxer` | M | None |
| `export/EncoderSurface.kt` (90) | Owns an EGL14 context and window surface on the encoder `Surface` (31–64), PTS 70, swap 74. Callers: `VideoExporter.kt:398`, `LoopRender.kt:386`, `CastStream.kt:191` | Native EGL in `core/viz/frame` on the `ANativeWindow` | Nothing (file goes) | None once the encoder is native; before that, `ANativeWindow_fromSurface` | M | None |
| `cast/CastStream.kt` (464) | Thread `cast-render` 127–130 runs `renderLoop` 189–242: own EGL, native frame clock with `LockSupport.parkNanos` 205–209, tee draw 211, fallback choice 212–228, a second `VisualizerRenderer` driven by hand (246, 253, 222). Own look push `syncLook` 263–284 and `readSavedLook` 151–170 on executor `cast-look` 99–100. `AudioPump` 296–410: own `SoundCursor` 366, resampler 376–384, hold/evict/pad shuffling 317–333 | New `core/output/CastJob` (clock `core/viz/frame/FixedRateClock` exists) | Start/stop, the encoder surface, LAN address pick 433–462 | Same NDK media APIs | H | None |
| `cast/CastEncoder.kt` (253) | Thread `cast-encoder` 87–91; `drainLoop` 103–114; hold until both formats 146–166; `CodecPcmFeed` 215–253 | `core/output` encode sink (shared with Record) | Media3 `FragmentedMp4Muxer` 46–50 until a native fragmented writer exists | `AMediaMuxer`'s output formats on the reference page do not list fragmented MP4 (not verified further). Hand over: encoded samples to the Kotlin muxer, one call per sample | M | None |
| `cast/WebRtcLink.kt` (145) | `OpusFeed` 82–140: thread `cast-opus` 101, drain loop 113–125 | Same encode sink | Handle | `AMediaCodec` | M | None |
| `cast/LiveSegments.kt` (190) | MP4 box walk `drainBoxes` 76–, `onBox` 90–, `boxSize` 180, window trim 125. Rewrite and playlist are native (121, 123, 163) | `core/viz/frame/HlsSegments` (exists): take the byte stream whole | Byte hand-over | None | L | None |
| `cast/CastMirror.kt` (100) | Process-wide copy of the stage look, milk path and stage flags; `directorMoment` 76–79; scope on `Dispatchers.Default` 81 | The one native look latch (below) | Nothing | None | M | None |
| `ui/RecordController.kt` (314) | Opens and closes the record tee on the stage GL thread through `queueEvent` 191–213; 250 ms ticker 139–145; recorder refcount 86–94 | `core/output/Recorder` | UI state, Pro gate, save sheet | None | L | None |
| `wallpaper/VisualizerWallpaperService.kt` (472) | Own GL view 126–134, own `VisualizerRenderer` 141, own `FramePacer` 45–46. Acts on the native run mode in `syncRunState` 210–269 (pacer start/stop, settle timer 249–252). `currentLook` 286–300 picks lock vs home look. `onZoomChanged` 321–327 holds a zoom rule (`COVER_ZOOM` 470). Touch to NDC 395–416. `restoreLook` 190–195. `applyFps` 184–187 | One native surface driver: `core/viz/SurfaceRun` (rule exists) plus the frame driver from the `engine/` port | `WallpaperService.Engine` callbacks, the `Surface`, the condition flags (`WallpaperRunState.kt`) | `WallpaperService` is Java only. Hand over: `Surface`, size, visibility, flags | H | None |
| `wallpaper/VisualizerDreamService.kt` (352) | `syncRunState` 243–307 is a near copy of the wallpaper's; `settling` 284–298; `showDark` 313–320; look 137–139 | Same surface driver | `DreamService` shell, `DreamOverlay`, `DreamCover` | `DreamService` is Java only | M | None |
| `ui/EnginePlumbing.kt` (181) | Builds the live `LookSnapshot` 49–70 and pushes it to the renderer from Compose 118–120; layers 136–140; touch smear 141–147; warm transition via `queueEvent` 155; fps cap 161; `vizApply` 172–179 | Native look latch | A single "state changed" hand-over | None | M | None |
| `ui/AutoVisualsController.kt` (305) | Auto's decisions: `holding` 84, `advanceVizPlaylist` 143–150, `stepVizPlaylist` 158–168, `onDirectorMoment` 221–232, `advanceRandomMode` 234–241, `randomStepNow` 247–282 (builds the choice list in Kotlin), `applyVizEntry` 285–304. Timer and pick are native | `core/viz/AutoTimer` grows into an Auto stepper | Playlist storage, the Pro allow-list as a bit set | None | M | None |
| `ui/ExportController.kt` (579) | The render job: `startExport` 115–178, `startBatch` 213–260, `renderOneTap` 381–416, `startLoopRender` 456–512, `finishLoopRender` 514–555, progress split 576–577 | `core/output/ExportJob` for the job steps; batch stays a UI queue | Gates, UI state, service start | None | M | None |

### Sound feed and audio

| File (lines) | What it decides in Kotlin | Native owner | Kotlin left | Blocker and thinnest hand-over | Risk | Saved data |
|---|---|---|---|---|---|---|
| `export/AudioTranscoder.kt` (510) | Two decode → downmix/resample → AAC loops: `transcodeAiff` 81–240, `transcode` 242–500; PTS 174/357; channel clamp 89/278/301 | New `core/audio/ingest/Transcode` on the native track reader | fd in, fd out | `AMediaExtractor_setDataSourceFd`, `AMediaCodec` | H | None |
| `export/LoudnessMeter.kt` (346) | Decode loop `drain` 138–230 on `Dispatchers.IO` 88. The meter is native (285–340) | Same native track reader + `core/audio/dsp/Loudness` | fd, result | Same | M | None |
| `audio/AiffExtractor.kt` (268) | A Kotlin AIFF header parser: `parseHeaders` 88–, `parseComm` 217–, `parseExtendedFloat80` 247–265, seek map 170–180. Native has `core/audio/ingest/AiffReader` | `AiffReader` (one parser) | The Media3 `Extractor` wrapper | Media3 `Extractor` is a Java interface. Hand over: header bytes in, format out | L | None |
| `audio/AudioCapturePump.kt` (121), `MicCapture.kt` (98), `PlaybackCapture.kt` (82) | Worker thread per capture (`AudioCapturePump.kt:60`), blocking `AudioRecord.read` loop 64–90; mic rate and encoding ladder `MicCapture.kt:38–66`, source pick 76–87 | Mic: the linked Oboe library as an input stream. Other apps' audio: stays | Permission, `MediaProjection` | Playback capture needs `AudioRecord` with `AudioPlaybackCaptureConfiguration` and a `MediaProjection` token (Research). Hand over: the direct buffer per read, as today | M | None |
| `wallpaper/OutputMixCapture.kt` (126) | `Visualizer` on session 0 writes 8-bit blocks into the ring 51–59 | Stays Kotlin (the ring write is already native) | Whole file | `android.media.audiofx.Visualizer` is a Java class; its page names no native twin (Research) | L | None |
| `wallpaper/SoundSource.kt` (148) | Main-thread poll every 250 ms 65–71; `tick` 95–123 acts on native `soundSourceStep` | The native sound feed picks its own source | Start/stop of the capture | None | L | None |
| `audio/dsp/NativeDspProcessor.kt` (180) | `queueInput` 102–116 (one native call per block); which formats run `runs` 169–172 | Format rule to `core/audio/dsp` | The Media3 `AudioProcessor` | Media3 interface is Java | L | None |
| `audio/OboeAudioOutput.kt` (175) | Which configs play natively `plays` 165–173; 60 s retry 150–152 | `core/audio/output` | Media3 `AudioOutput` wrapper | Same | L | None |
| `audio/AudioFxController.kt` (206) | EQ and effect clamps (114, 138, 145, 182–188) | `core/audio/dsp` ranges | State and prefs | None | L | Prefs keep their keys |
| `playback/PlaybackEngine.kt` (301) | `applyPrefs` 184–202 (native output choice 191) | Small; `core/audio/output` | Player build | None | L | None |
| `playback/SleepTimer.kt` (126), `AutoDirectorFeed.kt` (77) | 500 ms and 1 s tickers (43–68; 55–67) that feed native | Native reads the playout clock itself | Start/stop | None | L | None |

### Small rules and tables

| File (lines) | What it decides | Native owner | Risk | Saved data |
|---|---|---|---|---|
| `export/RenderEta.kt` (109) | ETA estimator 41–78 | `core/output` | L | None |
| `export/ExportRange.kt` (31) | Range clamp 18–29 | `core/output` | L | None |
| `export/ExportPresets.kt` (28), `VideoExporter.kt:77–97` | Bit-rate, ratio and preset tables | `core/output` table, read as text | L | Pref values must keep their names |
| `data/PaletteStore.kt` (176) | `applyGradient`, `applyPalette`, `applyStops`, `clear`, `forgetDeleted` 82–131 write scene params | `core/viz` params | L | Palette JSON unchanged |
| `ui/ModulationController.kt` (324) | Wire unpack 189–216, undo checkpoint rule 101–105. Rolls are native | Mostly fine | L | None |
| `ui/VisualSettingsController.kt` (182) | Transition length clamp 141 | Params schema | L | None |
| `pro/AutoDirectorPolicy.kt` (34) | Free "taste" rule 19–30. An export's twin is native (`director::forExport`, per `AGENTS.md`) | One native rule | L | None |
| `widget/WidgetVisual.kt` (161) | Draws a "glow" picture with its own lights and hues 92–122 | Could stay (a widget is a `RemoteViews` bitmap) or become a native still | L | None |
| `editor/Timeline.kt` (277) | Magnetic snap 234–277. No caller outside `editor/`, `export/ChapterMarkers.kt` and `data/EditorProjectJson.kt` | Delete or port with the Studio model | L | Project JSON |
| `viz/BackgroundImage.kt` (119) | Sample-size loop 103–104; decode on IO 62. Crop and finish are native | Optional: `AImageDecoder` (API 30+, Research) | L | None |

`ui/VisualGestures.kt` (827) recognises gestures on the main thread and hands touch points on (`publish` 450). That is input shell. Its body was outline-read only.
`ui/CustomizeControls.kt` (707) is a `when (key)` per control that draws a slider and copies one field (for example 88–119). It draws; ranges and placement are native.

## One engine: today and target

### Today: seven drivers

```
LIVE STAGE      ui/AppShell.kt:227  VisualizerView -> FramePacer -> VisualizerRenderer.onDrawFrame   (engine/, GL thread)
                look: ui/EnginePlumbing.kt:118   sound: renderer feedRing from AudioBus.soundInput
WALLPAPER       wallpaper/VisualizerWallpaperService.kt:126,141,46  own GL view + renderer + pacer
                look: :190 LookStore   run: :210 syncRunState   sound: wallpaper/SoundSource.kt poll
DREAM           wallpaper/VisualizerDreamService.kt:133  VisualizerView
                look: :137   run: :243 syncRunState (copy)   sound: SoundSource
CAST            cast/CastStream.kt:127 thread "cast-render", own EGL (:191), native clock, fallback renderer (:246)
                look: :263 syncLook + :151 + cast/CastMirror.kt   sound: AudioPump own cursor (:366)
                encode: cast/CastEncoder.kt:87 thread, cast/WebRtcLink.kt:101 thread
EXPORT          export/VideoExporter.kt:434 loop on Dispatchers.Default, EncoderSurface, OffscreenSceneRenderer (:407)
                look: host.look.pin() (ui/ExportController.kt:392)   sound: TrackAudio (engine/)
LOOP RENDER     export/LoopRender.kt:430 loop, EncoderSurface (:386), OffscreenSceneRenderer (:475)
RECORD          ui/RecordController.kt:191 tee on the stage GL thread; core/output/RecordSession thread (native)
WIDGET          no frames: a thumbnail or a drawn glow (widget/WidgetVisual.kt:68)
```

What is duplicated:

- **Frame loops:** live, wallpaper, cast, export, loop render. Five places decide "draw now, stamp this time".
- **EGL owners:** `GLSurfaceView` (live, wallpaper, dream) and `EncoderSurface` (export, loop, cast).
- **Encoder drain and mux loops:** `VideoExporter.kt:442–500`, `LoopRender.kt:608–666`, `CastEncoder.kt:103–175`, `WebRtcLink.kt:113–125`.
- **Encoder ladders:** `VideoExporter.kt:551`, `LoopRender.kt:512` (rungs are native; Record's try-loop is native in `core/output/EncodeSink`, these two are still Kotlin).
- **AAC interleave by PTS:** `VideoExporter.kt:585`, `LoopExtend.kt:462`, `LoopExtend.kt:560`.
- **Decode loops:** `AudioTranscoder.kt` (two), `LoudnessMeter.kt:138`, `TrackAudio` in `engine/`. **AIFF parsers:** `audio/AiffExtractor.kt` and native `AiffReader`.
- **Sound feeds:** live `feedRing`; cast `AudioPump`; wallpaper and dream `SoundSource` + `OutputMixCapture`; record (native take); export `TrackAudio`.
- **Look pushes:** `EnginePlumbing.kt:118`, `CastStream.kt:263`, `VisualizerWallpaperService.kt:190`, `VisualizerDreamService.kt:137`, `ExportController.kt:392`.
- **Run rules:** one native rule, two Kotlin actuators (wallpaper `:210`, dream `:243`), plus `ui/StageRun.kt:75` for the stage.

### Threads that touch frames, audio or encode

| Thread | Where | Per-frame or per-block Kotlin |
|---|---|---|
| GL thread (live, wallpaper, dream) | `GLSurfaceView` in `engine/` and `VisualizerWallpaperService.kt:126` | `onDrawFrame` (engine map) |
| `GeodeFramePacer` | `engine/` `FramePacer` | per vsync (engine map) |
| `cast-render` | `CastStream.kt:127` | every frame: 205–231 |
| `cast-look` | `CastStream.kt:99` | on look change |
| `cast-encoder` | `CastEncoder.kt:87` | every 5 ms or sample |
| `cast-opus` | `WebRtcLink.kt:101` | every block |
| `cast-http`, pool, `cast-http-deadline`, `cast-stop` | `LiveHttpServer.kt:72,74,138`, `CastService.kt:304` | network only |
| `ExportRun.scope` (`Dispatchers.Default`) | `export/ExportRun.kt:12` | every frame: `VideoExporter.kt:434`, `LoopRender.kt:447`; every block: `AudioTranscoder` |
| `Dispatchers.IO` | `LoudnessMeter.kt:88`, `BackgroundImage.kt:62` | every decoded block (meter) |
| `geode-mic`, `geode-playback-capture` | `MicCapture.kt:24`, `PlaybackCapture.kt:24` | every read: `AudioCapturePump.kt:64–90` |
| `Visualizer` callback | `OutputMixCapture.kt:51` | every capture |
| ExoPlayer playback thread | Media3 | every block: `NativeDspProcessor.kt:102`, `PlayoutSink` |
| Main `Handler` | `SoundSource.kt:65` (250 ms), `RecordController.kt:139` (250 ms), `SleepTimer.kt:43` (500 ms), `AutoDirectorFeed.kt:55` (1 s) | timers |
| Not engine: `geode-session`, `geode-resumption`, `geode-art`, `geode-history`, widget and store IO scopes | `PlaybackEngine.kt:230`, `PlaybackService.kt:463`, `SessionBitmapLoader.kt:22`, `HistoryStore.kt:168` | none |

### Target

```
Kotlin hands over:  Surface (stage, wallpaper, dream, second screen) + size + visible flags
                    fds (track in, video out), permissions, MediaProjection, Pro bits, prefs text
                    lifecycle (start, stop, cancel) and reads back: state, progress, one-shot notes

core/ owns:         ONE frame driver   (pacing as today, EGL on ANativeWindow, every surface a target)
                    ONE sound feed     (ring + cursors + source choice + track reader)
                    ONE look latch     (pinned copy per job)
                    ONE encode sink    (ladder, drain, interleave, mux) used by export, loop, record, cast
                    ONE job runner     (export, loop, extend, record, cast) with native threads
```

Frame pacing behaviour is not changed by this map: the same clocks and fps rules move, as they are.

## Port order

Engine path first, leaf first, one PR each. Steps 1–9 are the audio-to-visual path. Each new native part is called through `core/api/geode_api.h`; each PR deletes the Kotlin it replaces.

| # | PR | Removes | Host test that pins it |
|---|---|---|---|
| 1 | Link `mediandk`; native encode sink (ladder try-loop, drain, hold, PTS interleave) with a fake codec seam | Nothing yet (leaf) | new `encode_sink_test.cpp`; `encoder_ladder_test.cpp` |
| 2 | Record uses the sink (landed: `core/output/RecordSession`) | `record/RecordEncoder.kt` (deleted) | `recorder_test.cpp`, `encode_sink_test.cpp` |
| 3 | Cast audio pump and Opus feed native | `CastStream.AudioPump`, `OpusFeed`, `CodecPcmFeed` | `cast_clock_test.cpp` (audio cut) |
| 4 | Native EGL target on `ANativeWindow` | `export/EncoderSurface.kt` | `tools/hostlink` link check; device checklist |
| 5 | Export job native (frame loop, interleave, progress) | `VideoExporter.encodeInto`, `AudioFeed` | `offline_step_test.cpp`, `track_feed_test.cpp`, new `export_job_test.cpp` |
| 6 | Loop render and extend on the same job | `LoopRender.drawFrames`, `StopWriter`, `LoopExtend` copy loops | `loop_seam_test.cpp` |
| 7 | Cast render thread native; box walk into `HlsSegments` | `CastStream.renderLoop`, fallback renderer, `LiveSegments` walk, `CastEncoder` loop | `cast_clock_test.cpp`, `hls_segments_test.cpp` |
| 8 | One surface driver for wallpaper and dream (after the `engine/` frame-driver port lands) | both `syncRunState`, wallpaper pacer and renderer, `SoundSource` poll | `surface_run_test.cpp`, `sound_source_test.cpp`, `wallpaper_window_test.cpp` |
| 9 | One look latch | pushes in `EnginePlumbing`, `CastStream.syncLook`, `CastMirror`, wallpaper and dream restore | `resolver_test.cpp` (pin), new `look_latch_test.cpp` |
| 10 | Native track reader for transcode and loudness | `AudioTranscoder` loops, `LoudnessMeter.drain` | `track_feed_test.cpp`, `resampler_test.cpp`, `loudness_test.cpp` |
| 11 | `AiffExtractor` calls `AiffReader` | Kotlin AIFF parse | `track_feed_test.cpp` (AIFF cases) |
| 12 | Mic through an Oboe input stream | mic use of `AudioCapturePump`, rate ladder | `pcm_ring_test.cpp`, `layer_mix_test.cpp` |
| 13 | Auto stepper native | `AutoVisualsController` decisions | `auto_timer_test.cpp` |
| 14 | Secondary, any order: `RenderEta`, `ExportRange`, export tables, `PaletteStore` param helpers, EQ clamps, DSP and Oboe format rules, `AutoDirectorPolicy`, tickers, `editor/` snap (delete or port) | the small rules above | `params_test.cpp`, `gain_ramp_test.cpp`, `director_test.cpp`, new small tests |

Step 4 and step 8 cannot be proven on the host. They need a device checklist in the PR.

## Research (platform APIs)

Fetched 2026-11-12 from developer.android.com.

- **NDK media** (<https://developer.android.com/ndk/reference/group/media>): the page lists `AMediaCodec_createInputSurface(AMediaCodec*, ANativeWindow**)`, `AMediaMuxer_new(int fd, OutputFormat)`, `AMediaMuxer_writeSampleData`, `AMediaExtractor_setDataSourceFd(…, int fd, off64_t offset, off64_t length)` and `AMediaCodec_signalEndOfInputStream`. The API level of each was not readable in the fetch; check the headers before step 1. Whether `AMediaMuxer` can write fragmented MP4 was not confirmed; plan for cast to keep Media3's muxer.
- **`AImageDecoder`** (<https://developer.android.com/ndk/guides/image-decoder>): API 30+, reads from an fd, a buffer or an `AAsset`, JPEG/PNG/WebP/HEIF and more, `AImageDecoder_setTargetSize` scales. minSdk is 33, so it is usable.
- **Other apps' audio** (<https://developer.android.com/media/platform/av-capture>): capture needs `RECORD_AUDIO`, the `MediaProjectionManager.createScreenCaptureIntent()` prompt, and an `AudioRecord` built with `AudioPlaybackCaptureConfiguration`. The page gives no native capture path (AAudio appears only for the playing app's policy). This stays Kotlin.
- **`Visualizer`** (<https://developer.android.com/reference/android/media/audiofx/Visualizer>): session 0 is the output mix, 8-bit mono waveform, needs `RECORD_AUDIO` and `MODIFY_AUDIO_SETTINGS`. The page names no NDK equivalent. This stays Kotlin.
- **`AChoreographer`** (<https://developer.android.com/ndk/reference/group/choreographer>): `AChoreographer_getInstance` (API 24, must be an `ALooper` thread), `postFrameCallback64` (29), `registerRefreshRateCallback` (30), `postVsyncCallback` (33). All within minSdk 33.
- Not researched in this scan: Oboe or AAudio input details for the mic (step 12), `ANativeWindow_fromSurface`. Check the pages before those steps.

## Packages confirmed shell

No engine logic found (by outline and signal grep unless listed as read in full above):

- root (7 files): app, container, launch intent, crash log.
- `billing` (8), `pro` (8, except the `AutoDirectorPolicy` note), `ui/pro` (3).
- `data/backup` (5), `data/prefs` (2), `di` (4), `model` (19), `tile` (1).
- `ui/theme` (15), `ui/tour` (5), `ui/cast` (4), `ui/wallpaper` (1).
- `data` (66): stores and formats. Native-facing stores (`MacroStore`, `AutoDirectorStore`, `LfoStore`, `NoteResponseStore`, `BackgroundPrefsStore`, `RecordPrefs`) only pass text and values to native. The one exception is `PaletteStore.kt:82–131`.
- `playback` (21): Media3 service, queue and session glue. Queue rules (`PlaybackQueue`, `QueueOps`) and library search are app logic, not audio-to-visual; `LibraryTree.kt` has both a native search (112–126) and a `SearchMatcher` search (159).
- `ui` (123): Compose screens, ViewModels and controllers, apart from the six files in the tables.
- `widget` (5): Glance, apart from the drawn glow.
