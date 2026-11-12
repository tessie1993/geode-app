# Kotlin logic map: the whole codebase, and the one-core port (2026-11-12)

Scanned commit: `57fdf4a` (`main`; some scanners ran on `5dbfc69`, which differs in docs only).
Sources: the code and the Graphify graph (indexed at `57fdf4a`). `AGENTS.md`, `CLAUDE.md` and `docs/` were not used as sources.
Nothing was built or run. Every claim cites `path:line`; app paths are under `app/src/main/java/dev/geode/` unless they start with `app/`, `core/` or `engine/`.

## How it was read

- Fourteen scanners, one per area, each told to read every file in full and classify from the code. Their sections follow this summary unchanged, each with its own "not verified" list.
- Read in full: `engine/` (60 files), `export/`, `record/`, `editor/` (24), `cast/`, `wallpaper/`, `audio/`, `widget/`, `tile/`, `viz/` (50), `data/` (73), all of `ui/` (123), the app tests (135), the first half of the shaders (148 files).
- Partly read: `billing/`, `ui/theme`, `ui/tour`, `ui/pro` (comments stripped; three large files outlined); the second half of the shaders (about 20 files line by line, 130 by script for includes, uniforms and tempo literals); `core/` (headers and the per-frame `.cpp`, not all 460 files).
- Callers were checked with grep by the scanners and with Graphify (`graphify_file_neighbors`) for the frame driver.

## Summary

**The native core is already one engine.** There is one `Renderer` (`core/viz/Renderer.hpp:58`), one `SoundEngine` construction site (`core/viz/Renderer.cpp:97`) and one `Resolver` (`core/signal/Resolver.cpp:53-154`).
**The "three engines" are Kotlin drivers.** Core has no loop, no thread, no EGL context and no vsync source, so each surface's Kotlin repeats the frame protocol: the app view, the wallpaper, Cast and export.
**The port is therefore:** give the core its own stage (thread, EGL, vsync, frame loop, encode path, jobs), then delete the Kotlin drivers. It is not a merge of native engines.

| Area | Files | Engine | Mixed | Duplicate | Shell |
|---|---|---|---|---|---|
| `engine/` (main) | 47 | 8 | 16 | 9 | 14 |
| `export`, `record`, `editor` | 24 | 9 | 8 | 0 | 7 |
| `cast`, `wallpaper`, `audio`, `widget`, `tile`, `viz`, `ui/cast`, `ui/wallpaper` | 50 | 7 | 17 | 1 | 25 |
| `data`, `data/backup`, `data/prefs` | 73 | 0 | 21 | 0 | 49 |
| root, `playback`, `model`, `pro`, `billing`, `di`, `ui/theme`, `ui/tour`, `ui/pro` | 90 | 2 | 6 | 2 | 80 |
| `ui/` (123 files, three scans) | 123 | 3 | 36 | 0 | 83 |

The JNI boundary is 373 `external fun` against 373 JNI functions, an exact match; nine have no production caller. Native never calls Kotlin: no `JavaVM`, no `JNI_OnLoad`, no global refs. Kotlin polls.

## Every duplicated driver, and its one native owner

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
| `export/ExportPresets.kt` (28), `VideoExporter.kt:77–97` | Bit-rate, ratio and preset tables | `core/output` table, read as text | L | Pref values must keep their names |
| `data/PaletteStore.kt` (176) | `applyGradient`, `applyPalette`, `applyStops`, `clear`, `forgetDeleted` 82–131 write scene params | `core/viz` params | L | Palette JSON unchanged |
| `ui/ModulationController.kt` (293) | Undo checkpoint rule. Rolls are native | Mostly fine | L | None |
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
- **Decode loops:** `AudioTranscoder.kt` (two), `LoudnessMeter.kt:138`, `TrackAudio` in `engine/`.
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
| Driver | Where it exists today | One native owner |
|---|---|---|
| Live frame loop | `engine/scenes/src/main/kotlin/dev/geode/render/VisualizerRenderer.kt:258-297` (view, wallpaper, dream); `cast/CastStream.kt:189-242` | `core/stage`: one frame call, then one render thread |
| Vsync tick | `engine/scenes/src/main/kotlin/dev/geode/render/FramePacer.kt:131-213` on thread `GeodeFramePacer` (`:238`) | `AChoreographer` on the stage thread, feeding the existing `core/viz/frame/Pacer` |
| EGL owner | `GLSurfaceView` (`VisualizerView.kt:62`, `wallpaper/VisualizerWallpaperService.kt:153`); `export/EncoderSurface.kt:58` | one native EGL context per stage |
| Look push and change detection | `VisualizerRenderer.kt:34-42`, `render/look/LookApplier.kt:68-96`, `render/bridge/NativeViz.kt:465-522`, `ui/EnginePlumbing.kt:94-180` (seven paths), `cast/CastStream.kt` `syncLook` | one native look per stage, set on change |
| Run / visibility rule | `VisualizerView.kt:195-202` (Kotlin rule); `wallpaper/VisualizerWallpaperService.kt:210-269` and `wallpaper/VisualizerDreamService.kt:243-307` (the same machine twice) | `core/viz` run rule, acted on natively |
| Export frame loop | `export/VideoExporter.kt:350-537`, `export/LoopRender.kt:305-464`, `engine/.../offscreen/OffscreenSceneRenderer.kt:69-114` | native export job |
| Encoder drain and mux | `export/VideoExporter.kt`, `export/LoopRender.kt:590-701`, `record/RecordEncoder.kt:110-207`, `cast/CastEncoder.kt:68-175`, `cast/WebRtcLink.kt:82-145` | one native encode sink (`AMediaCodec`, `AMediaMuxer`) |
| Audio decode | `export/AudioTranscoder.kt:81-235` and `:242-503`, `export/LoudnessMeter.kt`, `engine/.../offscreen/TrackAudio.kt:75-140` | one native track reader |
| Sound feed | renderer `feedRing`; `cast/CastStream.kt:296-410`; `wallpaper/SoundSource.kt`; `audio/AudioCapturePump.kt:40-98` | the stage's own cursor and source choice |
| Thermal | `engine/.../render/ThermalGovernor.kt` | `AThermal` in `core/viz/frame/Thermal` |
| Gyro | `engine/.../render/GyroLook.kt` | `ASensorManager` in `core/viz/GyroLook` |
| Auto visuals | `ui/AutoVisualsController.kt:142-303`, polled at 500 ms from `ui/PlayerSession.kt:974-987` | native Auto stepper on the stage |
| Parameter defaults, ranges, show-if rules | `engine/.../scene/SceneParams.kt:5-271` (169 fields), `ui/CustomizeControls.kt:85-461`, `ui/CustomizeAdvanced.kt:163-526`, `model/ControlRange.kt:22-47`, `data/PresetStore.kt:252-308`, `data/LfoStore.kt:116-188` | the native schema, read as text |

## Threads Kotlin starts on engine paths

`GeodeFramePacer` (vsync), each `GLSurfaceView`'s GL thread (frames), `cast-render`, `cast-look`, `cast-encoder`, `cast-opus`, `record-encoder`, the capture threads `geode-mic` and `geode-playback-capture`, the export scope on `Dispatchers.Default`, and main-thread tickers at 33 ms (`ui/CustomizeMacros.kt:93-99`), 250 ms (`ui/RecordController.kt:139-145`, `wallpaper/SoundSource.kt`), 500 ms (`ui/PlayerSession.kt:974-987`, `playback/SleepTimer.kt`) and 1 s (`playback/AutoDirectorFeed.kt:55-67`). The per-area sections list each with its line.

## What cannot leave Kotlin, and the thinnest hand-over

| Stays Kotlin | Why | Hand-over |
|---|---|---|
| Compose UI, ViewModels' UI state | UI | commands down, state polled up |
| `WallpaperService.Engine`, `DreamService`, `SurfaceView` | Java-only classes | `Surface`, size, visibility flags |
| Media3 player, session, queue, `AudioProcessor`/`AudioSink` wrappers | Java interfaces | direct buffers per block, as today |
| Other apps' audio capture | needs `AudioRecord` with a `MediaProjection` token | direct buffer per read |
| `Visualizer` output-mix capture | no NDK twin | the ring write (already native) |
| Content URIs, MediaStore rows, permissions | `ContentResolver` | file descriptors |
| Billing, Pro entitlement, prefs, backup | owner's rule: data and Pro are Kotlin | "Pro: yes or no", saved text |
| `Bitmap` creation for stills and thumbnails | native can fill a `Bitmap`, not create one | pixels into a Kotlin-made bitmap |

## Design: one core

```
Kotlin shell                         core/
  Surface + size + visible flags ->  stage/   one Stage per surface: thread (ALooper), EGL, AChoreographer,
  commands (set look part, touch,             frame loop = pace -> hear -> resolve -> render -> present
   record, cast, export job)                  run rule, thermal (AThermal), gyro (ASensor), Auto stepper
  fds, Pro bit, saved text       ->  look/    one native look per stage (scene, params, LFO, ADSR, reactions,
  state polled (one read)        <-           tuning, transition, underlay); set on change, pinned per job
                                     output/  one encode sink (codec ladder, drain, interleave, mux) for
                                              record, cast, export, loop; one job runner with native threads
                                     audio/   ring, cursors, source choice, track reader, DSP, output
                                     viz/, sound/, signal/  as today: the renderer, the sound engine, the resolver
```

Rules the guides set (notes at the end of this file): minimise marshalling and the number of threads that touch JNI; no asynchronous calls between Kotlin and C++, so native threads never call up and Kotlin polls one state word; `AChoreographer_getInstance` only on an `ALooper` thread; thermal callbacks arrive on binder threads, so they hand over through atomics; C++ exceptions never cross JNI; every NDK call is checked against minSdk 33 by `-Werror=unguarded-availability`.

Host testing: the stage's lifecycle (surface created, changed, destroyed, paused, context lost, in any order), the frame order and the command queue are pure state machines with host tests. Only a thin layer touches EGL, the looper and the codecs; it is not compiled on the host.

## Port order (each step one PR; per-frame and per-block paths first)

| # | PR | Kotlin deleted | Host test |
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
| 12 | Mic through an Oboe input stream | mic use of `AudioCapturePump`, rate ladder | `pcm_ring_test.cpp`, `layer_mix_test.cpp` |
| 13 | Auto stepper native | `AutoVisualsController` decisions | `auto_timer_test.cpp` |
| 14 | Secondary, any order: export tables, `PaletteStore` param helpers, EQ clamps, DSP and Oboe format rules, `AutoDirectorPolicy`, tickers, `editor/` snap (delete or port) | the small rules above | `params_test.cpp`, `gain_ramp_test.cpp`, `director_test.cpp`, new small tests |
| 0 | Fix `soundSourceStep` (no `return`, `app/src/main/cpp/geode_sound_jni.cpp:42-52`); add `-Werror=return-type` and `-Werror=unguarded-availability` | none | CI build |
| 1 | One native frame call: the frame protocol moves inside core; native keeps the look, reads thermal and its clock | `LookApplier`, `ThermalGovernor`, the version counters, most of `NativeViz`, their tests | frame order, look latch |
| 2 | Native stage thread: EGL on `ANativeWindow`, `AChoreographer`, `ASensor` | `VisualizerRenderer`, `FramePacer`, `GyroLook`, `GLSurfaceView` from the view | lifecycle state machine |
| 3 | Wallpaper and dream on the stage | both run-state machines, the wallpaper's GL view and pacer, `SoundSource` poll | run rule, sound source |
| 4 | Native encode sink (`mediandk`) and Record on it | `record/RecordEncoder` | encode sink with a fake codec |
| 5 | Cast on the stage and the sink | `CastStream` render loop and audio pump, `CastEncoder` loop, `SoundCursor`, `WebRtcLink` feed | cast clock, audio cut |
| 6 | Native export job (frame loop, interleave, progress) | `VideoExporter.encodeInto`, `OffscreenSceneRenderer`, `EncoderSurface` | export job |
| 7 | Loop render and extend on the job | `LoopRender` loops, `LoopExtend` copy loops | loop seam, both PTS rules |
| 8 | Native track reader for export, transcode and loudness | `TrackAudio`, `AudioTranscoder` loops, `LoudnessMeter` drain | track feed, AIFF |
| 9 | Native look as the schema: defaults, ranges, show-if rules, codecs | `SceneParams` defaults, `SceneParamsCodec`, `ModConfigCodec`, Kotlin literal ranges | params, saved-text fixtures |
| 10 | Saved-format readers native (presets, LFO, ADSR, palettes, live state), each behind a fixture of old saved text | parse and clamp in `PresetStore`, `LfoStore`, `PaletteStore` | one fixture test per format |
| 11 | Auto stepper, auto-director amount, undo history, roll orchestration | `AutoVisualsController` decisions, `AutoDirectorFeed`, `ModulationController` history | auto timer, history |
| 12 | Small rules: export tables, EQ clamps, format rules, pitch ratio, sleep timer, `SearchMatcher`, mirrors of native lists | each file as it goes | one small test each |

Steps 2, 3, 5 and 6 cannot be proven by CI alone: they need a phone check (rotation, picture-in-picture, second screen, wallpaper preview, lock screen, 60/90/120 Hz).

## Decisions waiting on the owner

- Free-tier scene lists and export caps (`pro/FreeTier.kt:20-88`): planned as native tables with Kotlin passing only the Pro bit.
- Speed, pitch and skip-silence run in Media3's Java processors (`audio/dsp/MvzAudioProcessorChain.kt:19-20`): native replacements change how audio sounds; planned as separate work.
- Animation time source: draw-time clock today; the NDK vsync guide recommends the frame's expected presentation time. Proposed for step 2 as its own commit.
- 84 shader files read the drum envelopes directly (hardcoded reactions in GLSL): a shader redesign, outside this port.

## Bugs found by the scan

- Confirmed: `soundSourceStep` returns no value (`app/src/main/cpp/geode_sound_jni.cpp:42-52`); the wallpaper reads it as its capture action (`wallpaper/SoundSource.kt:99`).
- Reported, not yet checked: `DspSettings.crossfeed` has no writer; Liquid mode reseeds from `std::random_device` in exports (`core/viz/RendererFrame.cpp:499-504`); `uFormPhase`, `uSpawnSeed`, `uMoveDir` are constants so one style's morph never runs.

---

# Per-area sections

## engine/ (audio-android, audio-core, scenes)

Read in full: 60 Kotlin files, 5,857 lines (47 main files, 4,743 lines; 13 test files, 1,114 lines). `engine/audio-core/.../bridge/GeodeNative.kt` (2,736 lines) was only grepped for names. HEAD is 5dbfc69 (a merge on top of f13229d); `engine/` is as at f13229d. `SoundFrameLayout.kt`, `ReactionRouteCodec.kt`, `ReactSourceCatalog.kt` and `MilkdropEngine.kt` named in the brief no longer exist under `engine/`.

Short paths below: `R/` = `engine/scenes/src/main/kotlin/dev/geode/render/`, `S/` = `engine/scenes/src/main/kotlin/dev/geode/`, `AC/` = `engine/audio-core/src/main/kotlin/dev/geode/engine/audio/`, `T/` = `engine/scenes/src/test/kotlin/dev/geode/`. `GN.x` = `GeodeNative.x`.

Overall finding: almost no formula is left in this tree. What is left is (a) the live frame driver and its ordering (`VisualizerRenderer.onDrawFrame`, `FramePacer.onVsync`, `VisualizerView.syncPacer`), (b) the export frame driver and the MediaCodec decode pump (`OffscreenSceneRenderer`, `TrackAudio`), (c) Kotlin-side change detection and state replay (`LookApplier`, `LastSent`, `NativeViz` latches), (d) hand-kept mirrors of native enums, defaults, limits and option lists, (e) text parsers of three native tables, and (f) one whole Kotlin duplicate of a native module (`SearchMatcher`).

### 1. Per-file table

#### Main files

| File (lines) | Class | Engine logic: function, lines, what it decides | Thread | Native calls | Native owner in core/ | What must stay Kotlin and why |
|---|---|---|---|---|---|---|
| `engine/audio-android/.../audioandroid/PcmTap.kt` (55) | mixed | `flush` 29-39: starts a ring epoch on every format flush, only when `owns`. `handleBuffer` 41-44: drops the block when `!owns` or the format is unset; `staged` 47-54 copies a heap buffer into a direct one. The source-ownership gate (`owns`, 26-27) is a sound-routing decision. | Media3 playback thread, once per audio buffer | via `SampleRing`: `GN.pcmRingBeginEpoch`, `GN.pcmRingWriteDirect` | `core/audio/ingest/PcmRing` (exists); the `owns` gate: new, a writer-ownership flag on the ring | `TeeAudioProcessor.AudioBufferSink` is a Media3 Java callback; the adapter has to be JVM. |
| `AC/SampleRing.kt` (95) | shell (handle) | None. `maxWriteFrames = capacity / 4` default (21, 90) is a ring-sizing default kept in Kotlin. | writer thread, GL threads, main | `GN.pcmRingCreate/Destroy/Written/BeginEpoch/WriteFloats/WriteShorts/WriteBytes/WriteDirect/HeardAt` | `core/audio/ingest/PcmRing` | Lifetime by `Cleaner` + `reachabilityFence` (29, 82-87) is JVM object ownership. |
| `AC/SoundCursor.kt` (39) | shell (handle) | None. | Cast's audio thread (`app/.../cast/CastStream.kt:309,366`) | `GN.pcmCursorCreate/MaxFrames/TakeS16/Destroy` | `core/audio/ingest/PcmRing` `Cursor` | Only because Cast's encoder feed is still Kotlin; goes when that feed is native. |
| `AC/SoundTuning.kt` (79) | mixed | Slot constants 48-65 (`FLOATS = 30`, `GAIN = 0` … `INPUT_TRIM_DB = 29`) are a hand-kept copy of `GeodeSoundTuning`'s field order. `fromFloats` 74-77: migration rule (29 floats gets 0 dB trim appended, any other length is the defaults). Defaults, ranges, clamp are native. | any | `GN.soundTuningClamp`, `GN.soundTuningSchema` | `core/sound/Tuning` and `core/sound/SoundFrame.h` (exist); the 29→30 migration: new, in `core/sound/Tuning` | The value holder for prefs storage. |
| `S/analysis/PlaybackMath.kt` (18) | engine | `semitonesToRatio` 7: pitch ratio `2^(st/12)` handed to `PlaybackParameters` (`app/.../playback/PlaybackEngine.kt:185`). `snap` 14-17: slider step quantiser for speed, pitch, fade, picture delay, preamp (`app/.../ui/PlaybackSettings.kt:82,111,226,235,288`). `formatCountdown` 9-12 is display. | main | none | new: `core/audio/dsp` (pitch ratio) and a playback-settings table (steps) | `formatCountdown` (text). |
| `S/analysis/SearchMatcher.kt` (131) | duplicate | Whole file: `fold` 42-54 (accent/case fold, spelled-out table 119-130), `matches` 57-63, `tier` 73-85 (rank by field), `filterTracks` 87-113 (dedupe by uri, rank, cap 30). Its own KDoc says native `core/library/Search` is to replace it (6-9). | main (Compose `remember`, `app/.../ui/AppShell.kt:1050-1086`) | none | `core/library/Search.{hpp,cpp}`, `core/library/SearchFold.inc` (exist) | Nothing. |
| `S/audio/AiffPcm.kt` (57) | shell (handle) | None; `open` 45-55 resolves the descriptor span. | export thread | `GN.aiffOpenFd/aiffInfo/aiffProgress/aiffReadS16/aiffClose` | `core/audio/ingest/AiffReader` | `ContentResolver.openAssetFileDescriptor` + `detachFd` (49-53): content URIs have no NDK form. |
| `S/util/BestEffort.kt` (18) | shell | None. | any | none | n/a | Logging helper. |
| `R/Adsr.kt` (33) | duplicate (data) | `AdsrConfig` defaults 15-27 (attack 0.05, decay 0.25, sustain 0.5, release 0.35, amount 0.5, gate 0.25) and `AdsrEngine.COUNT = 2` (32) are hand-kept engine defaults/limits; `EnvBand` 3-12 mirrors a native enum by ordinal. | n/a | none | `core/viz/Adsr.hpp` | Labels only. |
| `R/BlendMode.kt` (24) | duplicate (enum) | Ordinal is the wire value of `GN.layerSet` (`R/VisualizerRenderer.kt:156`); `fromOrdinal` fallback SCREEN (22) has no production caller (dead). | n/a | none | `core/viz/LayerState` | Labels. |
| `R/EventRecipe.kt` (86) | mixed | Enum order 20-49 is the native recipe enum's wire order (hand-kept, 30 entries). `routesFor/isOn/fits/toggle` 53-80 only marshal. Titles and sentences are UI text. | main | `GN.eventRecipeRoutes/eventRecipeState/eventRecipeToggle`, `GN.RECIPE_ON/RECIPE_FITS` | `core/viz/ReactionDna` (`EventRecipe`) | Titles/sentences (translatable). |
| `R/FramePacer.kt` (248) | engine (driver) | `onVsync` 197-213: the per-vsync tick; calls native for dt, compares the cap slot to decide a new `Surface.setFrameRate`, publishes the paced rate, skips the frame when `dt <= 0`. `start/stop` 145-163 and the posted runnables 131-142: run state and resume. `applyTo` 184-195: asks native for the effective cap and sets it on the surface. `capFps/fps` 166-173: policy to number. `DEFAULT_TARGET_FPS = 60f` (221). `FrameRatePolicy` 43-60. The clock maths (period estimate, divide-down, dt clamp) is native. | `GeodeFramePacer` HandlerThread; `start/stop/applyTo` from main | `GN.pacerResume`, `GN.pacerOnVsync`, `GN.pacerEffectiveCap`, `GN.PACER_STATE_LONGS/PACER_CAP_SLOT/PACER_PACED_SLOT` | `core/viz/frame/Pacer.hpp` (clock, exists); the tick loop: new, an `AChoreographer` vsync callback on a native looper thread | Nothing in principle: `AChoreographer_postFrameCallback64` and `ANativeWindow_setFrameRate` exist in the NDK. Today `Choreographer`, `HandlerThread`, `Surface.setFrameRate`. |
| `R/GyroLook.kt` (50) | shell | Registers `TYPE_GAME_ROTATION_VECTOR` at `SENSOR_DELAY_GAME`, hands raw x,y,z,w + display rotation; session counter bump 34. Baseline, wrap, clamp are native. | sensor callbacks on main | none (writes `VisualizerRenderer.gyroReading`) | `core/viz/GyroLook.hpp` | `Display.rotation`; the sensor itself could be `ASensorManager`. |
| `R/Lfo.kt` (219) | mixed | Enums `LfoWave` 6-15, `ModSource` 26-38, `ModPolarity` 40-46, `ModCurve` 48-55, `LfoTarget` 63-136 (68 entries with 4 retired placeholders) are hand-kept mirrors of native enums in wire order. `LfoConfig` 139-156: defaults; `BEAT_CHOICES` 154. `LfoEngine.SLOTS = 3` (218) mirrors `core/viz/Lfo.hpp:84` `kSlots`. `ModTable` 163-214: parser of the native text table, version check "3" (173), bit-mask reads (177, 201). | main (lazy) | `GN.modTargetsText` | `core/viz/ModTargets.hpp`, `core/viz/Lfo.hpp` | Labels. |
| `R/MilkPresetMemory.kt` (116) | mixed | `restorePath` 51: which preset a new renderer starts on (shown → requested → first-run). `loaded` 59-63, `forget` 66-72: what counts as shown. `FIRST_RUN_PRESET` 78. Storage in `PrefsStore` 91-116. | GL threads (`loaded`), main (`request`), any | none | `core/viz/MilkShown.hpp` (reports "shown"); the restore choice: new, beside it | SharedPreferences file `geode-viz` key `milk_path`; asset copy. |
| `R/PerformanceSettings.kt` (94) | mixed | `RENDER_SCALES`, `MARCH_SCALES` 43-44: option lists (engine values). `mode` 34-35: preset matching. `LiquidHotPolicy` 72-84 wire values 0/1/2, default SIMPLIFY. `PerformanceMode` ordinals are the native tier index. Tiers and first-run pick are native. | main | `GN.perfTier`, `GN.perfModeForDevice`, `GN.PERF_TIER_FLOATS` | `core/viz/frame/Quality.hpp`, `core/viz/liquid/LiquidMath.hpp` | Value holder for the store. |
| `R/Reactions.kt` (229) | mixed | `ReactSource` 17-140 (118 entries, with `group` and `isEvent` per entry), `ReactMode` 168-176: hand-kept mirrors of `core/viz/Reactions.hpp`. `ReactionRoute` 179-198: defaults (amount 0.3, attack 0.05, release 0.3), `MAX_ROUTES = 12` (mirrors `core/viz/Reactions.hpp:185`), amount/attack/release limits 193-196. `HandMask` 217-229: a process-wide Kotlin copy of the hand mask (`current`) kept beside the native one. | main; `HandMask.current` read on GL and export threads | `GN.handSet` | `core/viz/Reactions.hpp`, `core/viz/ReactionResolve.hpp` | Labels and picker group headings. |
| `R/SceneFactory.kt` (14) | shell | Interface (scene id + milk path for an export). | n/a | none | n/a | Could vanish when export prepare is native. |
| `R/ThermalGovernor.kt` (38) | shell | `FORECAST_SECONDS = 30` (38). Pushes raw status and headroom. | listener on main; `sample` on a GL thread every ~10 s | `GN.thermalPush` | `core/viz/frame/Thermal.hpp` | `PowerManager.addThermalStatusListener`; headroom has `AThermal_getThermalHeadroom` in NDK. |
| `R/TouchField.kt` (13) | duplicate (constant) | `MAX_POINTS = 5` mirrors the native/shader touch slot count. | n/a | none | `core/viz/TouchTransform` | Nothing. |
| `R/TransitionCatalog.kt` (79) | mixed | `parseTable` 65-78: parser of the native transition table (RS/US/GS/FS separators). `BUILT_IN_IDS` 35 derived from `TransitionStyle`. | main | `GN.transitionsTable` | `core/viz/TransitionLibrary.hpp` | List for the picker. |
| `R/TransitionStyle.kt` (9) | duplicate (enum) | CUT, FADE, MELT, SLIDE, ZOOM: built-in transition ids (lower-cased name is the id sent to native and saved). | n/a | none | `core/viz/TransitionLibrary.hpp` / transition programs | Nothing. |
| `R/UnderlayBlend.kt` (22) | duplicate (enum) | Ordinals 0..2 are the wire value of `vizSetUnderlay`; `fromOrdinal` has no production caller (dead). | n/a | none | `core/viz/CompositePass.cpp` | Label. |
| `R/VisualizerRenderer.kt` (313) | engine (driver) | `onDrawFrame` 258-284 and `syncNativeState` 286-294: the live per-frame order (see §4). `look` setter 34-42: Kotlin version counters bumped by identity compare. `onSurfaceCreated` 193-204: reset rules. Gyro session rule 288-292. `exportSceneFactory` 299-307. Everything else is pass-through. | GL thread (GLSurfaceView's, the wallpaper's, Cast's); setters from main | `GN.shaderOverrideSet`, `GN.shaderOverride`, `GN.layerSet`, `GN.frameLiquidMode`, `GN.FRAME_CLEAR_ONLY/THERMAL_DUE/LIQUID_CHANGED/ERROR_CHANGED`; the rest through `NativeViz` | `core/viz/frame/Cadence.hpp`, `PushVersions.hpp`; the frame loop itself: new, one native "live surface" driver | `GLSurfaceView.Renderer` callbacks only while GLSurfaceView owns EGL. |
| `R/VisualizerView.kt` (220) | mixed | `syncPacer` 195-202: the run/visibility rule (attached && window visible && isShown && alpha > 0) that starts/stops the pacer, trims spare scenes, gates the gyro. `gyroListens` 216-220. `applyPerformance` 80-84: splits settings into renderer quality and pacer cap. `releaseScenesOnGlThread` 161-168: 200 ms bounded teardown. `init` 61-74: GLES 3, preserve context, `RENDERMODE_WHEN_DIRTY`. | main; `queueEvent` to GL | via renderer | `core/viz/SurfaceRun` via `core/api/geode_surface_api.cpp` has the wallpaper/dream rule; the in-app rule: new, same place | The `View` itself (attach, visibility, alpha callbacks, `SurfaceHolder`). |
| `R/bridge/ModConfigCodec.kt` (81) | engine (wire codec) | `packLfo` 16-29 (11 floats), `packAdsr` 31-47 (12 + targets), `packReactions` 50-64 (12 routes × 8 floats, padding disabled), `unpackReactions` 70-78 (drops unknown ordinals, curve falls back to LINEAR). Hand-kept float layouts; enums as ordinals. | GL thread on push; main in UI | none | `core/api` config structs (`GEODE_*_CONFIG_FLOATS`), `core/viz/Lfo.hpp`, `Adsr.hpp`, `Reactions.hpp` | Nothing once configs are stored natively. |
| `R/bridge/NativeViz.kt` (522) | mixed | Handle lifecycle 71-108 (create, destroy, Cleaner). Latch-and-replay state: see §8. `LastSent` 465-522: Kotlin change detection. `deliverStill` 410-417: reads the still into a `Bitmap`. `setStyleSchedule` 371-382: marshals cues. `sceneIds` 112: splits a `\n` list. | GL thread mostly; any thread for latches | `GN.vizCreate/vizDestroy/vizSetMilkTextureDir/vizKnows/vizSceneIds/vizSetParams/vizSetReducedMotion/vizSetTransition/vizBeginParamMorph/vizSetTouch/vizSetTouchSmear/vizSetFluidInjection/vizLoadMilkPreset/vizReloadMilkPreset/vizTakeMilkPresetLoaded/vizTakeDirectorStyleMoments/vizWriteAudio/vizHearTrack/vizEndPreRoll/vizFeedRing/vizSetSoundTuning/vizSetLfo/vizSetAdsr/vizSetUserReactions/vizGyroSample/vizGyroReset/vizFrameBegin/vizPinEngineSettings/vizSetOffscreen/vizPrepareOffscreen/vizSetFrameRate/vizSetUiBlurRegion/vizSetCastSource/vizSetEdgeGlow/vizRecordStart/vizRecordStop/vizSurfaceCreated/vizSurfaceChanged/vizSetScene/vizSetStyleSchedule/vizWarmTransition/vizCut/vizRender/vizStillRequest/vizStillSize/vizStillRead/vizReleaseScenes/vizTrimSpares/vizSetUnderlay/vizLastError/vizTapAlong` | `core/viz` Renderer behind `core/api/geode_api.h`; the latches: new, a per-surface settings block that outlives the renderer handle | `AssetManager`, `cacheDir`/`filesDir` paths, `Bitmap` creation for the still. |
| `R/bridge/SceneParamsCodec.kt` (80) | engine (wire codec) | `pack` 21-35, `unpack` 46-66, `slotFields` 69-79: reflection over `SceneParams` by native schema names; retired slots carry the default; ints rounded, flags > 0.5; lines 63-64 drop a saved-palette id when the frame cleared its overrides. Called on the GL thread for every params push (`NativeViz.setParams` 114-118). | GL thread; main (`app/.../ui/ModulationController.kt:149-227`) | none | `core/viz/ParamTable.inc`, `core/viz/Params.hpp` | Nothing once params are held natively; the palette-id rule moves with them. |
| `R/bridge/StyleSchedule.kt` (31) | shell (value) | `everyBeats = 4` default (16). | n/a | none | `core/viz/StyleSchedule.hpp` | Value type. |
| `R/fluid/FluidQuality.kt` (13) | duplicate (labels) | Five tier labels; count held to native `fluidTiers` by a test. | n/a | none | `core/viz/fluid/FluidQuality` | Labels. |
| `R/fluid/FluidShaderTemplate.kt` (15) | shell | Reads an asset as text. | main | none | n/a | Asset read for the editor. |
| `R/look/LookApplier.kt` (97) | engine | `apply` 68-96: decides which of scene, params, LFOs, ADSRs, tuning, reactions+hand mask, reduced motion, transition, underlay cross JNI this frame; two modes (native `due` bits for live, identity compare for export). Scene retry rule 74. `LookTarget` 14-46 is the seam. | GL thread (live, every frame); export thread (every frame) | `GN.FRAME_PUSH_PARAMS/LFO/ADSR/TUNING` constants | `core/viz/frame/PushVersions.hpp` (exists, versions only); the applier: new, native look state per surface | Nothing. |
| `R/look/LookSnapshot.kt` (70) | mixed | Defaults: first style `neon_chrome_sea` (30), transition `fade` 1200 ms (40-41, 48), 3 LFO + 2 ADSR default slots (32-33). `gentleFlight` 45: the dream's override = reducedMotion. `LookSource` 65-69. | any | none | new: `core/signal` look state (ADR 0006 param schema) | The immutable hand-over value while stores are Kotlin. |
| `R/offscreen/OffscreenSceneRenderer.kt` (128) | engine (driver) | `prepare` 69-92: the export prepare order (offscreen pin, frame rate, surface, scene, milk preset, schedule, settings pin, look, pre-roll feed, `endPreRoll`). `renderFrame` 100-114: per export frame: frame time → keyframed params → look apply → audio up to the frame → render at exact pts. | export thread (caller holds EGL) | `GN.exportPreRoll`, `GN.trackFeedTargetMs`, `GN.exportFrameTimeMs`, `GN.trackFeedTargetFrame`, `GN.exportFramePtsNs` + `NativeViz` | `core/output/FrameClock`, `core/output/PreRoll.hpp` (exist); the loop: new, `core/output` export driver | `Context`/`Uri` hand-over only. |
| `R/offscreen/TrackAudio.kt` (158) | engine (decode loop) | `deliverUntil` 75-88: pump loop until native says the target is heard. `decodeStep` 99-140: MediaExtractor → MediaCodec input/output drain, float/int16 detection, EOS. `openDecoder` 50-69: first audio track, seek to previous sync. `TIMEOUT_US = 10_000`. | export thread, several iterations per video frame | `GN.trackFeedCreate/Destroy/HasRate/PushAiff/PushDirect`, `vizHearTrack` | `core/audio/ingest/TrackFeed` (exists); the decoder pump: new, `AMediaExtractor`/`AMediaCodec` in `core/audio/ingest` | Opening a `content://` Uri to an fd (`ContentResolver`); the codec loop itself has an NDK form (`AMediaExtractor_setDataSourceFd`). |
| `R/scene/BuildStyleReactionsUseCase.kt` (51) | shell (marshal) | `packExact` 46-49 trims padding; `.take(MAX_ROUTES)`. | main | `GN.styleReactions`, `GN.styleReactionsHand` | `core/viz/ReactionDna` | Decoding for the Reactivity tab. |
| `R/scene/Catalogue.kt` (131) | mixed | `Table.parse` 85-113: parser of the native catalogue text, version 3. `kindOf` 49: unknown id → SILK fallback. `reaches` 51-54: kind scopes ∪ style scopes (a rule evaluated in Kotlin from table rows). `resolve` 42. | main (lazy) | `GN.catalogueText` | `core/viz/Catalogue.hpp` | UI reads. |
| `R/scene/MilkStarterPack.kt` (50) | shell | Copies bundled `.milk` assets once (marker `.starter-pack`, `VERSION = 1`). | GL thread via `MilkPresetMemory.firstRun` (!) and IO | none | n/a | Asset copy. |
| `R/scene/ParamFormat.kt` (153) | shell (display) | `unitFor` 86-94 + `UNITS` 60-83: which unit a control shows; `format` 97-132. Pure text; decides nothing about the picture. | main | none | n/a | Display text. |
| `R/scene/ParamKeys.kt` (158) | shell (labels) / duplicate (keys) | 147 label strings that are also the keys of the native catalogue's `key|` and `range|` rows and of the randomizer locks; hand-kept against native by `T/render/scene/CatalogueTest.kt:80-88`. | n/a | none | `core/viz/Catalogue.hpp` (key rows) | Labels. |
| `R/scene/ParamSchema.kt` (90) | mixed | `parse` 56-70: parser of the native params schema, version 3. | main (lazy), GL thread via codec | `GN.paramsSchemaText` | `core/viz/ParamTable.inc`, `core/viz/Params.hpp` | UI reads of ranges. |
| `R/scene/ParamScope.kt` (94) | duplicate (enum) | 22 scope names mirroring the native catalogue's. | n/a | none | `core/viz/Catalogue.hpp` | Nothing. |
| `R/scene/SceneCapabilities.kt` (37) | mixed | `SceneKind` 4-22 mirrors native kinds. `SHADER_SCENES` 27-30 builds the asset path `shaders/<id>_frag.glsl` per shader style (a native naming rule restated). | main | none | `core/viz/Catalogue.hpp` | Nothing. |
| `R/scene/SceneIds.kt` (70) | duplicate (table) | 66 style id constants, a hand-kept subset of the native catalogue (274 styles). `DEFAULT = "silk_web"`. | n/a | none | `core/viz/Catalogue.hpp` | Only the ids the shell names (default, milkdrop, pool, first style). |
| `R/scene/SceneParams.kt` (272) | duplicate + mixed | Lines 5-202: 169 fields with defaults, a hand-kept copy of `core/viz/ParamTable.inc` (187 rows, 20 retired) plus 2 Kotlin-only ids. Option lists 223-236 (`PARTICLE_SHAPES`, `SYMMETRY_FOLDS`, `FLUID_PATTERNS`, `FLUID_PATHS`, `CYMATICS_GEOMETRIES`, `CAMERA_PATHS`) mirror native enums by index. Engine functions: `usesCustomPalette` 239-240, `usesCustomPalette2` 245-246, `paletteStops` 242-243, `withoutCustomPalette` 255-265, `withPaletteStops` 268-271 (which palette source wins). | main, GL | `GN.paramsPaletteBase`, `GN.paramsPaletteRange` | `core/viz/Params.hpp`, `core/viz/ParamTable.inc`, `core/viz/Colour.hpp` | A typed holder for Compose only while the UI edits fields by name. |
| `R/scene/StyleFamilies.kt` (37) | shell (reads table) | None. | main | none | `core/viz/Catalogue.hpp` | Label lookup. |
| `R/scene/VisualStyleCatalog.kt` (106) | shell (labels) | Label maps 12-89 keyed by native ids; `idsOf(kind)` 105. | main | none | n/a | Labels (translatable). |

#### Test files

| File (lines) | What it pins | Native test that pins the same |
|---|---|---|
| `engine/audio-core/src/test/.../SoundTuningTest.kt` (74) | Kotlin slot constants against the field order parsed out of `core/sound/SoundFrame.h` (20-53); saved-floats round trip, the 29-float migration, wrong length → defaults (56-73). | Defaults/ranges/clamp: `core/tests/sound_test.cpp`. The slot-constant mirror and the 29→30 migration are pinned only here. |
| `T/analysis/SearchMatcherTest.kt` (80) | `fold`, `matches`, `tier`, `filterTracks` ranking. | `core/tests/library_search_test.cpp` pins the native search (fold at lines 76-78, 169). The Kotlin tier constants and the uri-dedupe/preferred rule are not checked against it. |
| `T/render/GyroListensTest.kt` (16) | `gyroListens` truth table. | None. `core/tests/gyro_look_test.cpp` pins the look maths, not this run rule. |
| `T/render/MilkPresetMemoryTest.kt` (103) | Restore order, saved only after `loaded`, missing-file fallback, `forget`. | None for the restore choice. `core/tests/milk_shown_test.cpp` pins when native reports "shown". |
| `T/render/ModTableTest.kt` (59) | Kotlin enums equal the native rows in wire order; column reads; offer masks past bit 63. | `core/tests/mod_targets_test.cpp` (rules, and `mod_targets.txt` equal to C++ output). The enum-name mirror is pinned only here. |
| `T/render/PerformanceSettingsTest.kt` (18) | `LiquidHotPolicy` wire values 0,1,2 and unknown → SIMPLIFY. | Tiers: `core/tests/quality_test.cpp`; policy behaviour: `core/tests/liquid_test.cpp`. The Kotlin wire mirror only here. |
| `T/render/bridge/LastSentTest.kt` (40) | `LastSent` change detection incl. `look()` and `pacedFps()`, which have no production caller. | `core/tests/push_versions_test.cpp` pins the native version scheme for params/LFO/ADSR/tuning; nothing native pins reduced motion/transition/reactions dedupe. |
| `T/render/bridge/WireLayoutTest.kt` (188) | `SceneParams` fields vs schema rows, kinds, defaults (35-55); round trip (58-76); slot numbers 187/0/108/153/163/168/177 (112-123); reaction/LFO/ADSR float layouts (126-187); enum ordinals 118/72/46/55, 67/60/59/52/55/58 (160-176). | `core/tests/params_schema_test.cpp`, `params_test.cpp` pin the schema and `params_schema.txt`; `reaction_resolve_test.cpp`, `lfo_random_test.cpp` pin behaviour. The Kotlin-side float layouts and ordinals are pinned only here. |
| `T/render/look/LookApplierTest.kt` (179) | First apply sends all nine parts in order; unchanged look pushes nothing; scene retry; pin; `gentleFlight`; due bits. | `core/tests/push_versions_test.cpp` and `renderer_cadence_test.cpp` pin the native half (which bit is due). The applier's own order and identity rules have no native twin. |
| `T/render/scene/CatalogueTest.kt` (109) | `SceneIds` all exist in the catalogue; 274 styles; retired `water` → `pool`; look groups; picker order; labels exist; 71 scoped `ParamKeys`; scope reach; ranges; `FluidQuality.LABELS.size == fluidTiers`. | `core/tests/catalogue_test.cpp` (table content and `catalogue.txt`). The Kotlin parser and the id/key mirrors only here. |
| `T/render/scene/ParamFormatTest.kt` (59) | Unit text formatting. | None; it is display text. |
| `T/render/scene/ParamSchemaTest.kt` (126) | Parser; 187 rows; specific rows; the 20 retired names (49-71); palettes 21, first "Spectrum", last "Yellow"; parser refusals. | `core/tests/params_schema_test.cpp`. Parser checks only here. |
| `T/render/scene/StyleFamiliesTest.kt` (63) | 13 families by name, 12 or 10 styles each, prefix, opener; each style has a shader asset file; each library on `core/viz/ShaderIncludes.inc`. | `core/tests/catalogue_test.cpp` for the table; the file-existence checks only here. |

All three table fixtures (`core/tests/catalogue.txt`, `params_schema.txt`, `mod_targets.txt`) are wired to JVM tests in `build-logic/src/main/kotlin/geode.kotlin-common.gradle.kts:61-66`.

### 2. Threads started here

| Where | Name | What runs | How often |
|---|---|---|---|
| `R/FramePacer.kt:238` | `HandlerThread("GeodeFramePacer", THREAD_PRIORITY_DISPLAY)`, process-wide, never quit | Its own `Choreographer` (247); `FramePacer.onVsync` for every running pacer; the posted start/stop runnables (131-142) | Once per display vsync per running pacer |
| `R/FramePacer.kt:240` | `Handler(thread.looper)` | Posts begin/end (148, 162) | On start/stop |
| `R/VisualizerView.kt:11,68` | GLSurfaceView's GL thread (started by `setRenderer`) | `VisualizerRenderer.onSurfaceCreated/Changed/onDrawFrame`; `queueEvent` jobs (114, 126, 163, 198) | Once per `requestRender` the pacer keeps |
| `R/bridge/NativeViz.kt:455` | `Cleaner.create()` daemon thread, one per process | `Owned.run` → `GN.vizDestroy` (103-107) | When a `NativeViz` is collected |
| `AC/SampleRing.kt:93` | `Cleaner.create()` daemon thread, one per process | `GN.pcmRingDestroy` (29) | When a ring is collected |
| `R/ThermalGovernor.kt:25` | `PowerManager` thermal listener (main thread) | `GN.thermalPush(status, NaN)` | On thermal status change |
| `R/GyroLook.kt:35` | `SensorManager` listener, default (main) looper | Writes `renderer.gyroReading` | `SENSOR_DELAY_GAME` (~50 Hz) while active |

No coroutine scope, dispatcher or Executor is created under `engine/`. One `MutableStateFlow` (`R/VisualizerRenderer.kt:116`) is written from the GL thread. Not started here but running this code: Media3's playback thread (`PcmTap`), the export thread (`OffscreenSceneRenderer`, `TrackAudio`), the wallpaper's and Cast's GL threads (`VisualizerRenderer`).

### 3. Per-frame and per-audio-block Kotlin paths

**Live video frame.** Driver: display vsync → `Choreographer` on `GeodeFramePacer` → `FramePacer.onVsync` (`R/FramePacer.kt:197-213`):
1. 198: return if not running. 201: re-post the callback.
2. 203: `GN.pacerOnVsync(clock, frameTimeNanos, capFps())` → dt.
3. 205-209: if `clock[PACER_CAP_SLOT]` changed, `applyTo(surface)` → `GN.pacerEffectiveCap` + `Surface.setFrameRate` (184-195).
4. 210: publish `pacedMilliHz`. 211-212: if dt > 0, `sink.onFrame(dt)` → `GLSurfaceView.requestRender()` (`R/VisualizerView.kt:25`; the wallpaper's own at `app/.../wallpaper/VisualizerWallpaperService.kt:46`). The dt is not used by the sink.

GL thread → `VisualizerRenderer.onDrawFrame` (`R/VisualizerRenderer.kt:258-284`), in order:
1. 259: read `performance`.
2. 260-272: `nativeViz.frameBegin(pacedRate, renderScale, marchScale, postFx, adaptive, liquidHot.native, paramsVersion, lfoVersion, adsrVersion, tuningVersion)` → `GN.vizFrameBegin` with `paced.fps()` and `paced.capFps()` (`R/bridge/NativeViz.kt:242-269`); returns the `due` bit set.
3. 273: `FRAME_CLEAR_ONLY` → return (a new surface's first frame).
4. 274: `FRAME_THERMAL_DUE` → `ThermalGovernor.sample()` → `PowerManager.getThermalHeadroom(30)` → `GN.thermalPush(-1, headroom)`.
5. 275: `FRAME_LIQUID_CHANGED` → `liquidModeState.value = GN.frameLiquidMode(due)`.
6. 276 → `syncNativeState(due)` (286-294):
   - 287: `lookApplier.apply(look, nativeViz, due)` (`R/look/LookApplier.kt:68-96`): `setScene` if the id differs from the applied one (74, retried every frame while native refuses); `setParams` on `FRAME_PUSH_PARAMS` (81) → `SceneParamsCodec.pack` by reflection into a 187-float frame → `GN.vizSetParams`; `setLfoConfigs` on `FRAME_PUSH_LFO` (82) → 3 × `GN.vizSetLfo` with a new 11-float array each; `setAdsrConfigs` on `FRAME_PUSH_ADSR` (83) → 2 × `GN.vizSetAdsr`; `setSoundTuning` on `FRAME_PUSH_TUNING` (84) → `GN.vizSetSoundTuning(30 floats)`; `setUserReactions` when the routes instance or `HandMask.current` changed (85) → `LastSent.userReactions` → `GN.vizSetUserReactions`; `setReducedMotion` (86), `setTransition` (87-89), `setUnderlay` (90-93) on a Kotlin compare.
   - 288-292: gyro: `gyroReset` when the session changed, `gyroSample(reading)` every frame while a reading exists → `GN.vizGyroSample(x,y,z,w,rotation)`.
   - 293 → `feedSound()` (297): `soundInput()` lambda then `GN.vizFeedRing(handle, ring.handle or 0)` every frame.
7. 277: `nativeViz.render(SystemClock.elapsedRealtimeNanos() / 1e9, targetFbo = 0)` → `GN.vizRender`; then `deliverStill` if a still is waiting (`R/bridge/NativeViz.kt:392-399, 410-417`).
8. 278: `FRAME_ERROR_CHANGED` → `pollError` → `GN.vizLastError` → `onShaderError`.
9. 279-282: `GN.vizTakeMilkPresetLoaded` every frame (a JNI string or null) → `MilkPresetMemory.loaded` (prefs write) + `onMilkPresetLoaded`.
10. 283: `GN.vizTakeDirectorStyleMoments` every frame → `onDirectorStyleMoment()` (the app's Auto changes style on it).

Fixed JNI calls per live frame with nothing changed: `pacerOnVsync`, `vizFrameBegin`, `vizFeedRing`, `vizRender`, `vizTakeMilkPresetLoaded`, `vizTakeDirectorStyleMoments` (6), plus `vizGyroSample` while the gyro runs. The timestamp for `vizRender` is taken in Kotlin from `elapsedRealtimeNanos`, not from the vsync time the pacer received.

**Export video frame.** Driver: the exporter's loop (`app/.../export/VideoExporter.kt:407`, `app/.../export/LoopRender.kt:475`) → `OffscreenSceneRenderer.renderFrame` (`R/offscreen/OffscreenSceneRenderer.kt:100-114`): `GN.exportFrameTimeMs` (107) → `spec.paramsAt(timeMs)` Kotlin keyframe lambda and `look.copy` (109) → `LookApplier.apply` in identity mode, so a keyframed export re-packs 187 floats by reflection every frame → `TrackAudio.deliverUntil` with `GN.trackFeedTargetFrame` (111) → `GN.exportFramePtsNs` + `GN.vizRender` (113).

**Decode loop inside an export frame.** `TrackAudio.deliverUntil` (`R/offscreen/TrackAudio.kt:75-88`) loops `viz.hearTrack(feed)` / `step()`; `decodeStep` (99-140) does one `dequeueInputBuffer` + `readSampleData` + `queueInputBuffer` and one `dequeueOutputBuffer` + `GN.trackFeedPushDirect` + `releaseOutputBuffer` per iteration, reading `outputFormat` on every output buffer (119). The same loop runs once before frame 0 for the pre-roll (`OffscreenSceneRenderer.kt:88-89`).

**Audio block.** Media3 playback thread → `TeeAudioProcessor` → `PcmTap.handleBuffer` (`engine/audio-android/.../PcmTap.kt:41-44`) → `SampleRing.write(ByteBuffer…)` (`AC/SampleRing.kt:63-72`) → `GN.pcmRingWriteDirect`; one JNI call per buffer, plus a bulk copy for a heap buffer (47-54). `PcmTap.flush` (29-39) → `GN.pcmRingBeginEpoch` per format flush.

**Sensor block.** `GyroLook.onSensorChanged` (`R/GyroLook.kt:41-44`) allocates a 5-float array per reading.

### 4. Duplicated drivers

- **Frame loop owners: three.** GLSurfaceView + `FramePacer` in `VisualizerView` (`R/VisualizerView.kt:25,68`); the wallpaper builds its own `FramePacer` and its own `VisualizerRenderer` (`app/.../wallpaper/VisualizerWallpaperService.kt:46,141,147`); Cast builds a `VisualizerRenderer` with a fixed `PacedRate` and no pacer (`app/.../cast/CastStream.kt:246,251`). The dream reuses `VisualizerView` (`app/.../wallpaper/VisualizerDreamService.kt:139`). All three run the same `onDrawFrame`.
- **Second frame loop for export.** `OffscreenSceneRenderer.renderFrame` is a separate per-frame order (look → audio → render) from `onDrawFrame` (frameBegin → look → gyro → ring → render); it never calls `vizFrameBegin` and uses `LookApplier`'s other mode.
- **Two sound feeds.** Live: `GN.vizFeedRing` every frame (`R/VisualizerRenderer.kt:297`). Export: `TrackAudio` + `GN.vizHearTrack`. A third, `NativeViz.writeAudio` (`R/bridge/NativeViz.kt:174-181`), is used only by androidTest (`app/src/androidTest/.../StyleThumbnailRenderTest.kt:220`, `StoreFramesTest.kt:146`).
- **Look pushes: five writers of `VisualizerRenderer.look`** (`app/.../ui/EnginePlumbing.kt:120`, `cast/CastStream.kt:283`, `wallpaper/VisualizerWallpaperService.kt:193`, `wallpaper/VisualizerDreamService.kt:139`) plus the export's `spec.look`. Cast copies only `sceneId` and `params` into its look (`CastStream.kt:283`).
- **Change detection, three layers for the same values.** Kotlin version counters in the `look` setter (`R/VisualizerRenderer.kt:35-42`) feeding native `PushVersions`; `LookApplier`'s identity compare (`R/look/LookApplier.kt:80-93`); `LastSent` inside `NativeViz` (`R/bridge/NativeViz.kt:121,128,224`). Reduced motion, transition and user reactions are checked twice (applier, then `LastSent`).
- **Run/visibility rules: two.** In-app: `VisualizerView.syncPacer` (`R/VisualizerView.kt:195-202`) in Kotlin. Wallpaper/dream: native `GN.surfaceRunMode` (GeodeNative.kt:1940-1945, `core/api/geode_surface_api.cpp`, used by `app/.../wallpaper/WallpaperRunState.kt`).
- **Performance application: three.** `VisualizerView.applyPerformance` (`R/VisualizerView.kt:80-84`), the wallpaper's (`VisualizerWallpaperService.kt:201`), the dream's (`VisualizerDreamService.kt:141,176`).
- **Thermal: one listener for the process, but `sample()` is asked from whichever GL thread native flags** (`R/VisualizerRenderer.kt:274`).
- **Decode loops.** `TrackAudio.decodeStep` here; `app/.../export/AudioTranscoder.kt` has another MediaCodec path and also opens `AiffPcm` (`AudioTranscoder.kt:82,250`). Not read; out of scope.
- **EGL owners.** None under `engine/` besides GLSurfaceView; `OffscreenSceneRenderer` requires the caller to own EGL (`R/offscreen/OffscreenSceneRenderer.kt:49-51`).
- **Hand mask held twice.** `HandMask.current` in Kotlin (`R/Reactions.kt:217-229`) and native `hand::global` set by the same call; `LookApplier` reads the Kotlin copy every frame (`R/look/LookApplier.kt:72`).
- **Search held twice.** `SearchMatcher` (Kotlin) and `core/library/Search` (native).

### 5. Data crossing the JNI boundary (engine/mixed files)

| Call site | To native | Back | How often |
|---|---|---|---|
| `FramePacer.onVsync` | `LongArray(9)` clock state (in place), `Long` vsync ns, `Float` cap | `Double` dt; slots 6 (cap mHz) and 7 (paced mHz) of the array | every vsync |
| `FramePacer.applyTo` | `Float` cap | `Float` effective cap | surface create/change, cap change |
| `vizFrameBegin` | handle, 2 fps floats, 2 scale floats, 2 booleans, 1 int, 4 `Long` versions | `Int` due bits (`FRAME_*`, liquid mode packed in, read by `GN.frameLiquidMode`) | every live frame |
| `vizSetParams` | `FloatArray(187)` (reused buffer `paramFrame`, `NativeViz.kt:56`) | none | on params version change; every frame in a keyframed export |
| `vizSetLfo` × 3 | slot int + `FloatArray(11)` (new array each) | none | on LFO change |
| `vizSetAdsr` × 2 | slot int + `FloatArray(12 + targets)` | none | on envelope change |
| `vizSetUserReactions` | `FloatArray(n × 8)` or null, `Int` mask | none | on routes/mask change |
| `vizSetSoundTuning` | `FloatArray(30)` | none | on tuning change |
| `vizSetUnderlay` | `IntArray(width × height)` ARGB or null, w, h, blend int, amount float | none | on background change (a full-frame copy) |
| `vizFeedRing` | two `Long` handles | none | every live frame |
| `vizGyroSample` | 4 floats + int | none | every live frame while gyro on |
| `vizRender` | handle, `Double` seconds, `Int` fbo | none | every frame |
| `vizTakeMilkPresetLoaded` | handle | `String?` | every live frame |
| `vizTakeDirectorStyleMoments` | handle | `Int` | every live frame |
| `vizLastError` | handle | `String` | when `FRAME_ERROR_CHANGED` |
| `vizStillSize/Read` | `IntArray(2)`, direct `ByteBuffer(w×h×4)` | pixels | per screenshot |
| `vizSetStyleSchedule` | `DoubleArray` cue times, 3 `String[]`, int | none | once per export |
| `vizSetTouch` | `FloatArray(10)`, count | none | per touch move (callers in `:app`) |
| `vizSceneIds` | handle | one `\n`-joined `String` | on demand |
| `thermalPush` | `Int` status, `Float` headroom | none | status change; ~every 10 s |
| `pcmRingWriteDirect` | direct `ByteBuffer`, position, byte count, encoding, channels, rate | `Int` frames | every audio buffer |
| `trackFeedPushDirect` | direct `ByteBuffer`, offset, size, isFloat, channels, rate, pts µs | `Boolean` | every decoded chunk |
| `trackFeedPushAiff` | two handles | `Boolean` | per AIFF step |
| `exportPreRoll` | `Long` start ms, `LongArray(2)` out | from-ms, heard-ms | once per export |
| `exportFrameTimeMs`, `exportFramePtsNs`, `trackFeedTargetFrame` | frame `Long`, fps `Int` | `Long` | every export frame |
| `catalogueText`, `paramsSchemaText`, `modTargetsText` | none | one `String` each (line tables, parsed in Kotlin) | once per process |
| `transitionsTable` | `AssetManager` | `ByteArray` (UTF-8, control-char separated) | once |
| `perfTier` | mode int, `FloatArray(5)` out | `Boolean` | once per mode |
| `soundTuningSchema` | none | `FloatArray(90)` (defaults, mins, maxes) | once |
| `styleReactions(Hand)`, `eventRecipe*` | id, seed/ordinal, `FloatArray(n×8)`, mask | `FloatArray(n×8)` or `Int` | per Customize recomposition key change |
| `paramsPaletteBase/Range` | `Int` palette, `Float` override | `Float` | per property read |
| `aiffOpenFd` | detached fd, start, length | handle | per open |

### 6. Saved formats touched

- **SharedPreferences** `geode-viz`, key `milk_path` (`R/MilkPresetMemory.kt:76-77`), a file path string; also listed as portable in `app/.../data/GeodePrefsFiles.kt:63`.
- **File marker** `<filesDir>/milk/.starter-pack` holding `1` (`R/scene/MilkStarterPack.kt:9-11`); directory `<filesDir>/milk/textures` (`R/bridge/NativeViz.kt:35`); first-run preset name `geode_bloom.milk`.
- **Enum ordinals on the wire and in saved data** (append-only): `LfoTarget` (68, saved by `app/.../data/LfoStore.kt` per the KDoc at `R/Lfo.kt:58-62`), `ModSource`, `ModPolarity`, `ModCurve`, `LfoWave`, `EnvBand`, `ReactSource` (118), `ReactMode`, `EventRecipe` (30), `BlendMode`, `UnderlayBlend`, `PerformanceMode`, `LiquidHotPolicy.native` (stored int, `app/.../data/PerformanceStore.kt:48`). Whether a store saves the name or the ordinal is decided in `:app` stores, not here.
- **Enum names** matched to native text rows: `SceneKind`, `ParamScope`, `ParamSpec.Kind`, `ModPolarity` (`valueOf` at `R/scene/Catalogue.kt:101-107`, `R/scene/ParamSchema.kt:66`, `R/Lfo.kt:178`).
- **`SceneParams` property names** are the preset JSON keys, the native schema names and the reflection keys (`R/scene/ParamSchema.kt:43`, `R/bridge/SceneParamsCodec.kt:70-76`; ProGuard keeps them per the codec KDoc). `customPaletteId` / `customPalette2Id` exist only in Kotlin (`R/scene/SceneParams.kt:214-218`).
- **Transition ids**: lower-cased `TransitionStyle` names (`cut`, `fade`, `melt`, `slide`, `zoom`) and GL-Transitions names (`R/TransitionCatalog.kt:32-35`), saved by `app/.../data/VizStateStore.kt:51`.
- **Style ids** in `SceneIds` (`R/scene/SceneIds.kt`), saved in presets/templates/projects; retired ids resolved through `Catalogue.resolve` (`app/.../data/TemplateFormat.kt:328`, `EditorProjectJson.kt:263`).
- **`SoundTuning` saved floats**: 30 floats, 29 accepted from before the input trim (`AC/SoundTuning.kt:74-77`); null means "engine defaults".
- **Table versions the shell refuses to misread**: catalogue 3 (`R/scene/Catalogue.kt:22`), params schema 3 (`R/scene/ParamSchema.kt:24`), modulation 3 (`R/Lfo.kt:173`).
- **`ParamKeys` strings** are the randomizer lock keys and native catalogue keys (`R/scene/ParamKeys.kt:3-9`); renaming one changes saved locks.
- **`LfoConfig.BEAT_CHOICES`** validates a saved `beats` value (`app/.../data/LfoStore.kt:163`).

### 7. Hand-kept copies of native lists, defaults and limits (the codecs' side)

| Kotlin | Lines | Native source | Held in step by |
|---|---|---|---|
| `SceneParams` 169 fields + defaults | `R/scene/SceneParams.kt:5-202` | `core/viz/ParamTable.inc` | `T/render/bridge/WireLayoutTest.kt:35-55` |
| `SceneParams` option lists (particle shapes 7, symmetry folds 11, fluid patterns 4, fluid paths 5, cymatics geometries 2, camera paths 7) | `SceneParams.kt:223-236` | native enums by index (`CameraRig::pathPoint` etc.) | nothing |
| `LfoTarget`, `ModSource`, `ModPolarity`, `ModCurve`, `LfoWave` | `R/Lfo.kt:6-136` | `core/viz/ModTargets.hpp`, `core/viz/Lfo.hpp` | `T/render/ModTableTest.kt:15-21` |
| `LfoEngine.SLOTS = 3`, `AdsrEngine.COUNT = 2`, `ReactionRoute.MAX_ROUTES = 12` | `R/Lfo.kt:218`, `R/Adsr.kt:32`, `R/Reactions.kt:191` | `core/viz/Lfo.hpp:84`, `core/viz/Adsr.hpp`, `core/viz/Reactions.hpp:185` | nothing direct |
| `AdsrConfig` defaults; `ReactionRoute` defaults and limits (−2..2, 5 s, 10 s) | `R/Adsr.kt:15-27`, `R/Reactions.kt:180-196` | `core/viz/Adsr.hpp`, `core/viz/Reactions.hpp` | nothing |
| `ReactSource` (118, with group and isEvent), `ReactMode` | `R/Reactions.kt:17-176` | `core/viz/Reactions.hpp` | `WireLayoutTest.kt:160-168` (counts and four ordinals); `app/src/androidTest/.../ReactSourceCatalogDeviceTest.kt:20` |
| `EventRecipe` order | `R/EventRecipe.kt:20-49` | `core/viz/ReactionDna` | nothing in `engine/` tests |
| LFO 11-float, ADSR 12-float head, route 8-float layouts | `R/bridge/ModConfigCodec.kt:13-64` | `GEODE_*_CONFIG_FLOATS` in `core/api` | `WireLayoutTest.kt:126-187` |
| `SoundTuning` slot constants | `AC/SoundTuning.kt:48-65` | `core/sound/SoundFrame.h` `GeodeSoundTuning` | `SoundTuningTest.kt:20-53` |
| `SceneIds` (66), `SceneKind` (9), `ParamScope` (22), `ParamKeys` (147) | `R/scene/*` | `core/viz/Catalogue.hpp` | `T/render/scene/CatalogueTest.kt` |
| `TransitionStyle` (5), `BlendMode` (8), `UnderlayBlend` (3), `LiquidHotPolicy` (3), `PerformanceMode` (3) | `R/*.kt` | `core/viz` transition programs, `LayerState`, `CompositePass.cpp`, `liquid/LiquidMath.hpp`, `frame/Quality.hpp` | `PerformanceSettingsTest.kt` for the policy only |
| `TouchField.MAX_POINTS = 5`, `FluidQuality.LABELS` (5) | `R/TouchField.kt:12`, `R/fluid/FluidQuality.kt:12` | shader `uTouchPoints`, `fluidTiers` | `CatalogueTest.kt:107` for the tiers |
| `PerformanceSettings.RENDER_SCALES/MARCH_SCALES`, `LfoConfig.BEAT_CHOICES`, `FramePacer.DEFAULT_TARGET_FPS = 60`, `LookSnapshot` default style and 1200 ms fade, `StyleSchedule.everyBeats = 4`, `ThermalGovernor` 30 s forecast, `SampleRing` runway divisor 4, `TrackAudio.TIMEOUT_US` | various (see table 1) | none: these values exist only in Kotlin | nothing |
| `SHADER_SCENES` asset path rule `shaders/<id>_frag.glsl` | `R/scene/SceneCapabilities.kt:27-30` | native ShaderScene's path rule | `StyleFamiliesTest.kt:46-53` |

### 8. State `NativeViz` latches in Kotlin and replays on a new handle

A handle is destroyed on every view detach (`R/VisualizerView.kt:139-142`) and recreated; the native renderer forgets everything, so Kotlin replays it in `create()` (`R/bridge/NativeViz.kt:71-88`):
- milk texture dir (78); UI blur region, 6 floats (79, 286, 302-318, 338-340); edge glow options array (80, 287, 332-336); a pending still request (81, 61-62, 402-405); `castSource` (82, 324-329); touch smear on/strength (83, 288-289, 143-150); then `onCreated` → the MilkDrop preset from `MilkPresetMemory.restorePath` (84, `R/VisualizerRenderer.kt:114`).
- `sent.clear()` (76) and again on `surfaceCreated` (357) so reduced motion, transition and reactions go again; `LookApplier.reset()` (`R/VisualizerRenderer.kt:200`) resends the whole look; `feedRing(null)` (203) restarts the sound cursor.
- `reportedError` (58) is not cleared on a new handle.
- Not latched in `NativeViz`, so lost on a new handle unless the caller repeats it: fluid injection shaders (`setFluidInjectionShaders` 152-157), a `warmTransition`, an in-flight `beginParamMorph`, the style schedule, record tee.
- Process-wide native state that does not need replay: shader overrides, layer, hand mask, thermal (`R/VisualizerRenderer.kt:143-156`).

### 9. Surprises

- **`VisualizerRenderer.availableSceneIds()` and `milkdropAvailable` create a native renderer from the main thread** (`R/VisualizerRenderer.kt:126-135`, called from Compose at `app/.../ui/VisualsStage.kt:520` and `StudioRender.kt:445`). `submitFluidInjectionShaders` (158-164) and `loadMilkPreset` (172-176) do the same.
- **`NativeViz.onCreated` can run `MilkStarterPack.install` (asset copy to disk) on a GL thread**: `create()` → `onCreated` → `milkPreset.restorePath` → `store.firstRun()` (`R/MilkPresetMemory.kt:51,109-115`). `MilkPresetMemory.loaded` also writes prefs from the GL thread (`R/VisualizerRenderer.kt:280`).
- **`MilkPresetMemory` (engine module) calls `getSharedPreferences` itself** (`R/MilkPresetMemory.kt:94`), outside the `data/GeodePrefsFiles` owner.
- **`LastSent.look()` and `LastSent.pacedFps()` (and their fields) have no production caller** (`R/bridge/NativeViz.kt:466-467, 471, 476-484, 498`); only `T/render/bridge/LastSentTest.kt` calls them. Dead.
- **`NativeViz.cut()` (388-390) has no caller anywhere.** `NativeViz.writeAudio` (174-181) and `setOffscreen` (277-279) are called only from androidTest. `BlendMode.fromOrdinal` and `UnderlayBlend.fromOrdinal` have no production caller.
- **The pacer's dt is thrown away.** `FrameSink.onFrame(dt)` only calls `requestRender()`; `onDrawFrame` takes its own `elapsedRealtimeNanos` (`R/VisualizerRenderer.kt:277`). Frame time on the GL thread is therefore not the vsync time.
- **`SceneParamsCodec` uses Java reflection on the GL thread** for each params push (`R/bridge/SceneParamsCodec.kt:26-34`), and on every frame of a keyframed export.
- **`SearchMatcher` is a complete Kotlin duplicate still in use** by the global search sheet (`app/.../ui/AppShell.kt:1050-1086`) although native search exists.
- **`PlaybackMath` (pitch ratio, slider steps) sits in package `dev.geode.analysis` of `:engine:scenes`**; it is playback logic with no relation to scenes.
- **`VisualizerRenderer.liquidMode` is mirrored a second time** in `PlayerSession` (`app/.../ui/EnginePlumbing.kt:164`, `PlayerSession.kt:688-695`).
- **The director's style decision surfaces in Kotlin**: `onDirectorStyleMoment` (`R/VisualizerRenderer.kt:77-78, 283`) hands the "change style now" moment to the app's Auto, so the style change itself is made in `:app` Kotlin.
- **Cast's look is partial**: only `sceneId` and `params` are copied (`app/.../cast/CastStream.kt:283`), so LFOs, envelopes, reactions and tuning there are `LookSnapshot`'s defaults unless set elsewhere (not verified beyond that line).
- **`Catalogue.reaches` evaluates the scope rule in Kotlin** from two row types (`R/scene/Catalogue.kt:51-54`), while `ModTable` gets a finished answer mask from native; the two tables follow different designs.
- **`ReactSource.FADE_GAIN` is last (ordinal 117)** while the test comment says "Eq22k is last" (`T/render/bridge/WireLayoutTest.kt:161`); the comment is stale, the count 118 is asserted.
- **`ParamUnit.BPM`** exists as a display unit "reserved for the tempo controls of a later change" (`R/scene/ParamFormat.kt:30-31`) and the test formats `124 BPM` (`T/render/scene/ParamFormatTest.kt:37`); `ReactSource.BPM("Tempo")` is a routable source (`R/Reactions.kt:114`). Noted because of the owner's tempo rule; neither weights a visual choice in this tree.
- **`VisualStyleCatalog.FAMILY_LABELS` lacks `flame`, `opal`, `hollow`** (three families the catalogue has, `T/render/scene/StyleFamiliesTest.kt:29-31`); they fall back to the capitalised name (`R/scene/VisualStyleCatalog.kt:103`).
- **Not verified:** the exact body of each `GeodeNative` function and the JNI side (another scanner's scope); `AudioTranscoder`'s decode loop; whether Cast sets the other look parts elsewhere. Graphify was not used: every caller claim above is from Grep over `app/` and `engine/`.


---

## JNI boundary

Read: `GeodeNative.kt` (2,736 lines; every code line, KDoc skipped), all 12 files in `app/src/main/cpp/` (3,331 lines, in full), root `CMakeLists.txt` (141) and `core/CMakeLists.txt` (242) in full, `AndroidManifest.xml` (439). `core/api/` (35 files, about 5,900 lines) was scanned, not read line by line: includes, `static_assert`s, globals and the header's declarations. Working tree is `5dbfc69`, not `f13229d` as the brief says; `git diff --name-status f13229d HEAD` lists only three files under `docs/agents/`.

Counts: 373 `external fun` in `GeodeNative.kt`; 373 `Java_dev_geode_engine_bridge_GeodeNative_*` functions in 11 `.cpp` files; the two sets match exactly (no orphan on either side). No other `external fun` and no other `System.loadLibrary` exists in `app/` or `engine/`. `core/api/geode_api.h` declares about 438 `geode_*` functions, `geode_reaction_text_api.h` two more.

JNI functions per file: `geode_viz_jni.cpp` 204, `geode_export_jni.cpp` 32, `geode_sound_jni.cpp` 31, `geode_ingest_jni.cpp` 29, `geode_jni.cpp` 22, `geode_audio_out_jni.cpp` 15, `geode_playout_jni.cpp` 14, `geode_export_dsp_jni.cpp` 10, `geode_tags_jni.cpp` 7, `geode_webrtc_jni.cpp` 7, `geode_shell_port_jni.cpp` 2.

### 1. Boundary table by area

Core class is taken from the `#include`s of the `core/api` file the JNI function calls into. Caller counts are `GeodeNative.<name>` matches on one line in `src/main` (a call split as `GeodeNative` newline `.name` is not counted, so counts are a floor). `viz` = `app/src/main/cpp/geode_viz_jni.cpp`.

| Area | Externals | JNI file:lines | `core/api` file → core class | What crosses | Top Kotlin callers (count) |
|---|---|---|---|---|---|
| viz lifecycle, scene, transition, milk | 15 | viz:127-139, 158-162, 188-213, 679-698, 722-751 | `geode_viz_api.cpp` → `viz/Renderer`, `viz/SceneRegistry`, `viz/TransitionLibrary`, `viz/scenes/MilkdropScene` | `Long` handle, `AssetManager` jobject (`vizCreate`), `String` in, `String?` out | `render/bridge/NativeViz.kt` (16) |
| frame / pacer / thermal / perf | 20 | viz:599-624, 644-663, 733-736, 1041-1073, 1186-1236, 1281-1299 | `geode_viz_api.cpp` → `viz/frame/Cadence`, `PushVersions`, `Pacer`, `Thermal`, `Quality`, `LiveFps`, `WallpaperWindow`; `geode_frame_api.cpp` → `viz/frame/FixedRateClock` | primitives; caller-owned `LongArray` state (pacer 9, frame clock 3) copied in and out; `FloatArray` out | `NativeViz.kt` (7), `cast/CastStream.kt` (5), `render/FramePacer.kt` (3), `ui/PerformanceSettings.kt` (2), `render/ThermalGovernor.kt` (2) |
| look / params / touch / underlay / gyro / shake | 34 | viz:141-186, 427-446, 582-591, 616-642, 758-774, 791-799, 879-887, 996-1017, 1239-1279, 1678-1694 | `geode_viz_api.cpp` → `viz/Renderer`, `Director`, `DirectorReach`, `NoteResponse`, `LayerState`, `GyroLook`, `TapAlong`; `geode_params_api.cpp` → `viz/Params`, `ParamRandomizer`, `TouchTransform`; `geode_underlay_api.cpp` → `viz/Underlay`; `geode_shake_api.cpp` → `viz/Shake` | `FloatArray` (the whole param frame), `IntArray` ARGB pixels (underlay), `String` | `NativeViz.kt` (17), `AutoDirectorStore.kt` (4), `BackgroundPrefsStore.kt` (4), `EasterEggs.kt` (3), `BackgroundController.kt` (3) |
| macro | 25 | viz:266-425 | `geode_macro_bank_api.cpp` → `viz/MacroBank`, `viz/CustomizeMap` | `String` bank text in, `String` bank text out, on every edit | `data/MacroStore.kt` (26) |
| lfo / adsr / reactions / music moments | 17 | viz:258-264, 466-580, 593-597, 1019-1038; `geode_shell_port_jni.cpp`:14-32 | `geode_viz_api.cpp` → `viz/Lfo`, `Adsr`, `Reactions`, `ReactionDna`, `ReactionResolve`, `ModTargets`; `geode_params_api.cpp` → `viz/WaveRandomizer`; `geode_reaction_text_api.cpp` → `viz/ReactionText` | `FloatArray` configs and routes in, new `FloatArray` out, `String` | `EventRecipe.kt` (3), `MusicMoments.kt` (3), `NativeViz.kt` (3), `LfoStore.kt`, `BuildStyleReactionsUseCase.kt` (method refs) |
| sound | 15 | viz:221-256; `geode_sound_jni.cpp`:13-84 | `geode_sound_api.cpp` → `sound/Silence`, `sound/SoundSource`, `sound/Tuning`, `sound/PlayerSignals`; `geode_viz_api.cpp` → renderer's `sound/SoundEngine` | primitives, ring handle, `LongArray[8]` state, `FloatArray` tuning | `wallpaper/SoundSource.kt` (5), `NativeViz.kt` (5), `audio/AudioCapturePump.kt` (2) |
| pcm ring / cursor | 17 | `geode_ingest_jni.cpp`:20-149 | `geode_ingest_api.cpp` → `audio/ingest/PcmRing`, `Cursor`, `LayerMix`, `SnapshotStitch` | direct `ByteBuffer`; `FloatArray`/`ShortArray`/`ByteArray` pinned critical; `IntArray[2]` out | `engine/audio/SampleRing.kt` (9), `SoundCursor.kt` (4), `audio/PlayoutSink.kt` (2), `PlaybackEngine.kt` (2) |
| playout | 14 | `geode_playout_jni.cpp`:13-76 | `geode_playout_api.cpp` → `sound/interp/PlayoutClock`, `SpeakerSync` | primitives only | `audio/PlayoutSink.kt` (8), `playback/PlaybackEngine.kt` (5), `CastStream.kt` (2) |
| dsp | 22 | `geode_jni.cpp`:13-144 | `geode_dsp_api.cpp` → `audio/dsp/DspProcessor` (`Equalizer`, `GainStage`, `Crossfeed`, `Limiter`) | primitives, `String` stream id, direct `ByteBuffer` (`dspProcess`) | `audio/dsp/NativeDspProcessor.kt` (17), `PlaybackEngine.kt` (3), `TrackCrossfade.kt` (2), `ReplayGain.kt` (2) |
| audio out | 15 | `geode_audio_out_jni.cpp`:15-87 | `geode_audio_out_api.cpp` → `audio/output/OboeOutput`, `OutputFifo` | primitives, direct `ByteBuffer` (`audioOutWrite`) | `audio/OboeAudioOutput.kt` (15) |
| export (frame clock, encode plan, passes, PCM, loudness, resampler, AIFF, track feed) | 60 | `geode_export_jni.cpp`:52-74, 183-208, 241-329; `geode_export_dsp_jni.cpp`:22-143; `geode_ingest_jni.cpp`:152-226; viz:616-619, 738-741, 1085-1173 | `geode_export_api.cpp` → `output/FrameClock`, `PreRoll`, `EncodePlan`, `Chapters`, `audio/dsp/PcmConvert`, `PcmTiming`; `geode_export_dsp_api.cpp` → `audio/dsp/Loudness`, `PcmResampler`; `geode_output_api.cpp` → `output/EncoderLadder`; `geode_export_pass_api.cpp` → `viz/frame/ExportPasses`, `LoopSeam`, `ExportPassMath`; `geode_ingest_api.cpp` → `audio/ingest/AiffReader`, `TrackFeed` | primitives, direct `ByteBuffer` (decoder output, watermark RGBA), `ShortArray` in and new `ShortArray` out, `AssetManager`, fd | `export/AudioTranscoder.kt` (11), `WatermarkOverlay.kt` (10), `LoopRender.kt` (7), `offscreen/TrackAudio.kt` (7), `VideoExporter.kt` (6), `LoudnessMeter.kt` (6), `AiffPcm.kt` (5) |
| loop | 8 | `geode_export_jni.cpp`:78-179 | `geode_export_api.cpp` → `output/LoopPlan`, `LoudnessAdvice` | primitives, `LongArray[7]` spec out, new `LongArray`/`FloatArray` | `export/LoopRender.kt` (5), `LoopExtend.kt` (3) |
| record + still | 22 | `geode_export_jni.cpp`:212-237; viz:1532-1628, 1657-1676 | `geode_record_api.cpp` → `output/Recorder`, `output/RecordPlan`; `geode_viz_api.cpp` → `viz/frame/RecordTap` | `Surface` jobject (`vizRecordStart`), direct `ByteBuffer` (audio, still pixels), primitives | `ui/RecordController.kt` (15), `record/RecordEncoder.kt` (13), `NativeViz.kt` (7), `RecordSettings.kt` (3) |
| cast | 11 | viz:1075-1081, 1302-1357, 1632-1655 | `geode_cast_tee_api.cpp` → `viz/frame/CastTee`; `geode_frame_api.cpp` → `viz/frame/CastAudio`, `CastTeeRule` | primitives, `LongArray` state (3, 6, 3) | `cast/CastStream.kt` (6), `CastEncoder.kt` (4), `NativeViz.kt` (3) |
| hls | 4 | viz:1360-1391, 1510-1528 | `geode_frame_api.cpp` → `viz/frame/HlsSegments` | `ByteArray` (init, fragment) in, new `ByteArray` out, `DoubleArray`, `String` | `cast/LiveSegments.kt` (5) |
| webrtc / link / qr | 19 | viz:1394-1508; `geode_webrtc_jni.cpp`:12-63 | `geode_link_api.cpp` → `viz/frame/LinkShare`, `LinkPage`, `util/QrCode`; `geode_webrtc_api.cpp` → `cast/WebRtcLink` (libdatachannel) | `String`, `Array<String>` headers, `ByteArray` encoded frames, `IntArray` state | `cast/LiveHttpServer.kt` (8), `cast/WebRtcLink.kt` (8) |
| library | 5 | `geode_tags_jni.cpp`:103-179 | `geode_library_api.cpp` → `library/Search` | `Array<ByteArray?>` (3 cells per track), `LongArray`, new `IntArray` | `ui/LibraryBrowse.kt` (4), `playback/LibraryTree.kt` (1) |
| tags | 2 | `geode_tags_jni.cpp`:63-101 | `geode_tags_api.cpp` → `library/Tags` (TagLib) | fd, `Array<ByteArray?>`, `IntArray`, `FloatArray` | `NativeTags.kt` (4) |
| colour / palette | 9 | viz:801-867 | `geode_colour_api.cpp` → `viz/Colour`; `geode_palette_api.cpp` → `viz/Palette`; `geode_art_palette_api.cpp` → `viz/ArtPalette` | primitives, `IntArray` pixels, new `IntArray`/`FloatArray` | `ui/ColourPicker.kt` (10), `PaletteMaker.kt` (5), `ArtPalette.kt` (1) |
| surface / stage run | 15 | `geode_sound_jni.cpp`:88-207 | `geode_surface_api.cpp` → `viz/SurfaceRun`, `viz/StageRun`; `geode_stage_step_api.cpp` → `viz/StageStep` | primitives (flag ints), `IntArray`, `BooleanArray`, `Array<String>` | `wallpaper/WallpaperRunState.kt` (6), `ConnectedDisplays.kt` (3), `VisualsStage.kt` (3), `VisualizerDreamService.kt` (2) |
| auto | 8 | `geode_sound_jni.cpp`:209-261; viz:215-218, 700-720 | `geode_auto_api.cpp` → `viz/AutoTimer`, `AutoDraw`, `TransitionChoice`; `geode_style_pick_api.cpp` → `viz/StylePick`; `geode_viz_api.cpp` → `viz/StyleSchedule` | primitives, `String`, `IntArray`, `BooleanArray`, `DoubleArray` + 4 `Array<String>` (schedule) | `ui/AutoVisualsController.kt` (12), `StylesPage.kt` (4), `NativeViz.kt` (3) |
| customize | 8 | viz:889-989 | `geode_params_api.cpp` → `viz/CustomizeMap`, `ParamRandomizer`; `geode_randomize_all_api.cpp` → `viz/RandomizeAll` | `String`, param `FloatArray` in, caller's `FloatArray` filled | `ui/ModulationController.kt` (4), `CustomizeCategory.kt`, `CustomizeTabs.kt`, `PlayerSession.kt` |
| catalogue / schema text | 8 | viz:448-464, 665-677, 776-789, 991-994, 1175-1184 | `geode_catalogue_api.cpp` → `viz/Catalogue`, `CataloguePresets`; `geode_params_api.cpp` → `viz/Config`; `geode_viz_api.cpp` → `viz/ShaderOverrides`, `TransitionLibrary` | `String` out (line text Kotlin parses), `ByteArray` (transitions table), `AssetManager` | `BuiltInPresets.kt` (2), `Catalogue.kt` (2), `VisualizerRenderer.kt` (2), `NativeViz.kt` (2), `TransitionCatalog.kt` (1) |

Externals with no production caller (Grep of the bare name across `app/` and `engine/`, `GeodeNative.kt` excluded): `exportSeamBudgetedCrossfadeMs` (`GeodeNative.kt:1765`), `soundSetFadeGain` (1588), `configSchemaText` (919), `paramsRollKeys` (934), `wallpaperFps` (752), `macroBankRoll` (398), `playoutDestroy` (2307), `playoutSpeakerFrame` (2347), `recorderFramesDropped` (1359). `reactSourceCatalog` (299) is called only by `app/src/androidTest/.../ReactSourceCatalogDeviceTest.kt:18`. `vizWriteAudio` (276) is reached in production code only through the wrapper `NativeViz.writeAudio` (`NativeViz.kt:174-180`), whose only callers are two androidTest files (`StyleThumbnailRenderTest.kt:220`, `StoreFramesTest.kt:146`).

### 2. Calls per frame or per audio block

Paths: `E/scenes` = `engine/scenes/src/main/kotlin/dev/geode`, `E/core` = `engine/audio-core/src/main/kotlin/dev/geode/engine`, `A` = `app/src/main/java/dev/geode`.

**Live frame, GL thread** (`E/scenes/render/VisualizerRenderer.kt:257-284`, `onDrawFrame`):

| External | Kotlin site | JNI | Copies | Java heap |
|---|---|---|---|---|
| `vizFrameBegin` | `VisualizerRenderer.kt:261` → `NativeViz.kt:255` | viz:599-607 | 12 primitives | none |
| `vizSetParams` (only when `FRAME_PUSH_PARAMS` or the look changed) | `look/LookApplier.kt:81` → `NativeViz.kt:117` | viz:141-145 | `GetFloatArrayElements` + release `JNI_ABORT` (`jni_util.hpp:60-62`) | none in JNI |
| `vizSetLfo` × slots (when due) | `LookApplier.kt:82` → `NativeViz.kt:211` | viz:466-470 | `GetFloatArrayElements` | Kotlin allocates one `FloatArray` per slot (`ModConfigCodec.packLfo`) |
| `vizSetAdsr` × slots (when due) | `LookApplier.kt:83` → `NativeViz.kt:216` | viz:593-597 | `GetFloatArrayElements` | one `FloatArray` per slot in Kotlin |
| `vizSetSoundTuning` (when due) | `LookApplier.kt:84` → `NativeViz.kt:206` | viz:250-256 | `GetFloatArrayRegion` | `tuning.toFloats()` in Kotlin |
| `vizGyroReset` / `vizGyroSample` | `VisualizerRenderer.kt:290-292` → `NativeViz.kt:231` | viz:582-591 | 5 primitives | none |
| `vizFeedRing` | `VisualizerRenderer.kt:297` → `NativeViz.kt:199` | viz:245-248 | two handles; no sample crosses | none |
| `vizRender` | `VisualizerRenderer.kt:277` → `NativeViz.kt:397` | viz:733-736 | primitives | none |
| `vizLastError` (only on `FRAME_ERROR_CHANGED`) | `VisualizerRenderer.kt:278` → `NativeViz.kt:445` | viz:679-682 | `NewByteArray` + `String(byte[], "UTF-8")` | yes, on change only |
| `vizTakeMilkPresetLoaded` (every frame) | `VisualizerRenderer.kt:279` → `NativeViz.kt:168` | viz:205-213 | 4,096-byte stack buffer; a `String` only when a path waits | only when a preset loaded |
| `vizTakeDirectorStyleMoments` (every frame) | `VisualizerRenderer.kt:283` → `NativeViz.kt:171` | viz:215-218 | primitive | none |

A normal live frame is therefore 5 JNI crossings (`vizFrameBegin`, `vizFeedRing`, `vizRender`, two takes), 6 with the gyro on. Each `viz*` crossing takes the global `gVizMutex` and a per-handle shared lock (`VizRef`, viz:44-65).

**Pacer, `PacerThread` vsync** (`E/scenes/render/FramePacer.kt:197-210`): `pacerOnVsync` at `FramePacer.kt:203` → viz:1218-1227, `GetLongArrayRegion` + `SetLongArrayRegion` of 9 longs, no allocation. `pacerEffectiveCap` (`FramePacer.kt:192`) only when the cap changes.

**Thermal**: `thermalPush` from `E/scenes/render/ThermalGovernor.kt:25,34`, on the frames `FRAME_THERMAL_DUE` marks (`VisualizerRenderer.kt:275`); primitives.

**Export frame** (`E/scenes/render/offscreen/OffscreenSceneRenderer.kt:100-114`, the export's GL thread): `exportFrameTimeMs` (:107), the look pushes above, `trackFeedTargetFrame` (:111) inside the decode loop with `trackFeedPushDirect` (`offscreen/TrackAudio.kt:126` → `geode_ingest_jni.cpp:195-205`, direct buffer, no copy) or `trackFeedPushAiff` (`TrackAudio.kt:94`), `vizHearTrack` (`NativeViz.kt:188`), `exportFramePtsNs` (:113), `vizRender`. `VideoExporter.kt:437,439` calls `exportFrameTimeMs`/`exportFramePtsNs` again per frame. `exportWatermarkDraw` (`A/export/WatermarkOverlay.kt:60`), `exportSeamFrame` (`A/export/LoopRender.kt:733`), `exportLoopProgress` (`LoopRender.kt:460`) are per frame; all primitives.

**Cast loop** (`A/cast/CastStream.kt:207-231`, cast thread, 1 pass per frame): `frameClockWaitNs` (:208), `frameClockTake` (:209) (viz:1049-1065, `Get/SetLongArrayRegion` of 3 longs), `castTeeDraw` (:211), `stageRun` (:212), `castTeeAwaitStage` (:213), `frameClockAudioUntilUs` (:216, :231), and the fallback renderer's whole `onDrawFrame` (:222). Audio pump per pass: `castAudioStep` (`CastStream.kt:320` → viz:1311-1322, three region copies), `pcmCursorTakeS16` (`E/core/audio/SoundCursor.kt:33` → `geode_ingest_jni.cpp:132-149`, `GetPrimitiveArrayCritical` + `SetIntArrayRegion`), `castAudioFeedAdd`/`castAudioFeedTake` (`A/cast/CastEncoder.kt:229,241`). No Java allocation in JNI.

**Cast outputs, per encoded buffer**: `webrtcVideo`/`webrtcVideoConfig`/`webrtcAudio` (`A/cast/WebRtcLink.kt:52,59` → `geode_webrtc_jni.cpp:32-58`): `GetByteArrayElements` (may copy) released `JNI_ABORT`. `hlsFragmentSeconds` (`A/cast/LiveSegments.kt:123` → viz:1360-1371): two `GetPrimitiveArrayCritical`. `hlsRewriteFragment` (`LiveSegments.kt:121` → viz:1374-1391): two `GetByteArrayRegion` into native vectors, then `NewByteArray` per fragment (Java heap allocation).

**Audio blocks**:

| External | Kotlin site | Thread | JNI | Copies | Java heap |
|---|---|---|---|---|---|
| `pcmRingWriteDirect` | `E/core/audio/SampleRing.kt:71` from `engine/audio-android/.../PcmTap.kt:43` | Media3 playback | `geode_ingest_jni.cpp:30-39` | `GetDirectBufferAddress`; none | none (a heap buffer is first copied into a reused direct buffer in Kotlin, `PcmTap.kt:47-54`) |
| `dspProcess` | `A/audio/dsp/NativeDspProcessor.kt:115` | Media3 playback | `geode_jni.cpp:131-144` | `GetDirectBufferAddress`, in place | none (Kotlin copies input to the output buffer first, :109-113) |
| `pcmRingHead` ×2, `playoutBufferHandled`, `playoutPosition` | `A/audio/PlayoutSink.kt:30-40` | Media3 playback | `geode_ingest_jni.cpp:103-105`, `geode_playout_jni.cpp:21-30` | primitives | none |
| `audioOutEvents`, `audioOutWrite` | `A/audio/OboeAudioOutput.kt:47-48` | Media3 playback | `geode_audio_out_jni.cpp:49-56, 72-74` | `GetDirectBufferAddress`; native copies into `OutputFifo` | none |
| `audioOutPositionUs`, `audioOutStalled` | `OboeAudioOutput.kt:83,87` | Media3 playback (position polls) | `geode_audio_out_jni.cpp:58-60, 76-78` | primitives | none |
| `pcmRingWriteFloats` / `pcmRingWriteShorts`, `captureHeardAt` | `A/audio/AudioCapturePump.kt:81-85` | capture pump | `geode_ingest_jni.cpp:41-69` | `GetPrimitiveArrayCritical`, released `JNI_ABORT` | none |
| `pcmRingWriteBytes` | `SampleRing.kt:60` | caller's | `geode_ingest_jni.cpp:71-83` | critical | none |
| `recorderTakeAudio`, `recordNowNs`, `recorderWrote`, `recorderVideoEncoded` | `A/record/RecordEncoder.kt:138, 169, 190, 204` | record encoder | viz:1546-1567 | direct buffer + `SetLongArrayRegion` | none |
| `loudnessFeedDirect` | `A/export/LoudnessMeter.kt:313` | export | `geode_export_dsp_jni.cpp:74-99` | direct; a native `std::vector` copy only for a misaligned span | none |
| `resamplerProcess` | `A/export/PcmResampler.kt:43` | export | `geode_export_dsp_jni.cpp:32-51` | `GetShortArrayRegion`, two native vectors, `SetShortArrayRegion` | `NewShortArray` per block |
| `pcmDownmixS16`, `pcmFloatToS16`, `pcmTrimS16` | `A/export/AudioTranscoder.kt:55, 157, 404, 425` | export | `geode_export_jni.cpp:269-296, 319-329` | region copies through native vectors | `NewShortArray` per block (first two) |
| `aiffReadS16` | `E/scenes/audio/AiffPcm.kt:32` | export / decode | `geode_ingest_jni.cpp:175-183` | native vector + `SetShortArrayRegion` | none |
| `vizWriteAudio` | tests only | – | viz:221-230 | `GetFloatArrayRegion` into a new `std::vector` per call | none |

**Tickers that are not per frame but periodic**: `soundSourceStep`/`silenceResting` (`A/wallpaper/SoundSource.kt:24,32,99,118`), `surfaceRunMode` (`A/wallpaper/WallpaperRunState.kt:65`), `autoTimerDue` (`A/ui/AutoVisualsController.kt:146,239`), `recorderRecordedMs`/`recorderStopped` (`A/ui/RecordController.kt:142,223-224`), `thermalTakeWarmNote` (`A/ui/StageWarmNote.kt:34`), `webrtcPeers` (`A/cast/WebRtcLink.kt:62`), `shakeSample` per sensor event (`A/ui/EasterEggs.kt:58`), `vizSetTouch` per touch event (`NativeViz.kt:139`).

### 3. JNI practices

- **JavaVM / JNIEnv caching**: none. No `JavaVM`, `GetJavaVM`, `JNI_OnLoad`, `AttachCurrentThread` or `DetachCurrentThread` anywhere in `app/src/main/cpp/` or `core/` (Grep, zero hits). `core/` includes no `jni.h`.
- **Registration**: name lookup only (`Java_dev_geode_engine_bridge_GeodeNative_*`, `JNIEXPORT`). No `RegisterNatives`. The library loads in `GeodeNative`'s `init` (`GeodeNative.kt:6-8`). The build uses `-fvisibility=hidden` (`CMakeLists.txt:14`), so only `JNIEXPORT` and `GEODE_API` symbols are exported.
- **Native → Kotlin callbacks: none.** The only `Call*Method`/`NewObject` uses are `java.lang.String.getBytes("UTF-8")` (`jni_util.hpp:17-19`) and `new String(byte[], "UTF-8")` (`jni_util.hpp:46-49`), both on the calling thread inside a call from Kotlin. Everything native wants Kotlin to know is polled: `vizFrameBegin`'s due bits, `vizTakeMilkPresetLoaded`, `vizTakeDirectorStyleMoments`, `vizLastError`, `audioOutEvents`, `audioOutStalled`, `recorderStopped`, `webrtcPeers`, `thermalTakeWarmNote`.
- **Native threads that exist** (none attach to the JVM): Oboe's audio callback thread (`core/audio/output/OboeOutput.cpp:284`, `onAudioReady`), the thread Oboe makes for `onErrorAfterClose` (`OboeOutput.cpp:291`), a `std::thread` per stream close (`OboeOutput.cpp:272`), the program-binary cache writer (`core/viz/ProgramBinaryCache.hpp:66`), and libdatachannel's own threads behind `core/cast/WebRtcLink.cpp` (its `onStateChange` callback at :152 stays in C++).
- **Global refs**: none (`NewGlobalRef`, `NewWeakGlobalRef`: zero hits). Handles are raw pointers as `jlong`.
- **FindClass / method id caching**: none cached. `Utf8Chars` does `GetObjectClass` + `GetMethodID` + `NewStringUTF("UTF-8")` on every string argument (`jni_util.hpp:16-19`); `utf8String` does `FindClass` + `GetMethodID` on every returned string (`jni_util.hpp:46-48`). `macroBankSetCurve` does this three times per call (viz:357-361).
- **Critical sections**: `GetPrimitiveArrayCritical` at `geode_ingest_jni.cpp:48, 63, 77, 140` (ring writes, cursor take) and viz:1365-1366 (`hlsFragmentSeconds`, two arrays held at once). No JNI call is made inside any of them.
- **Array elements**: `GetFloatArrayElements` in `FloatElements` (`jni_util.hpp:60`), `GetIntArrayElements` for the underlay (viz:765), `GetByteArrayElements` for WebRTC frames (`geode_webrtc_jni.cpp:36, 45, 54`); all released `JNI_ABORT`. Everything else uses `Get/Set<Type>ArrayRegion`.
- **Direct buffers**: `dspProcess`, `audioOutWrite`, `pcmRingWriteDirect`, `trackFeedPushDirect`, `loudnessFeedDirect`, `recorderTakeAudio`, `vizStillRead`, `exportWatermarkCreate`; each checks address and capacity.
- **Exception checks**: none (`ExceptionCheck`, `ExceptionClear`, `ExceptionOccurred`, `ThrowNew`: zero hits). `Utf8Chars` calls `CallObjectMethod` and continues without a check (`jni_util.hpp:19`). No native code throws into Java.
- **String encodings are mixed**: `Utf8Chars`/`utf8String` (standard UTF-8) for most; `GetStringUTFChars` (Modified UTF-8) at `geode_jni.cpp:94, 104` and `geode_sound_jni.cpp:222`; `NewStringUTF` for every `macroBank*`, catalogue, schema and customize text return (viz:263-424, 549, 676, 778-788, 822, 849, 891-902, 965, 993-998) and `reactionsTextEncode` (`geode_shell_port_jni.cpp:19`).
- **Local refs**: loops over object arrays delete each element ref (viz:105-112, 1457-1462; `geode_sound_jni.cpp:170-175`; `geode_tags_jni.cpp:30-37, 72-76`).
- **Lifetime guard**: only `geode_viz*` handles are guarded (registry + reader/writer lock, viz:31-87). Every other handle type (`dsp`, ring, cursor, playout, audio out, recorder, track feed, aiff, resampler, loudness, export pass, cast tee, shake, library index) is `reinterpret_cast` from the `jlong` with no check.

### 4. Does the JNI layer only marshal?

Mostly yes. Places that hold state, loops or decisions:

- `geode_viz_jni.cpp:31-87`: a process-global registry of live renderers (`gVizMutex`, `gVizLive`, `VizEntry`, `VizRef`, `vizDestroyLive`). State and locking that belong to the handle's lifetime, not marshalling.
- `geode_viz_jni.cpp:700-720` (`vizSetStyleSchedule`): the entry count is decided here as the minimum of three array lengths.
- `geode_viz_jni.cpp:170-174` (`vizSetTouch`), 224-226 (`vizWriteAudio`), `geode_ingest_jni.cpp:46-50, 61-65, 76-79`: clamp the frame or point count to what the array holds.
- `geode_viz_jni.cpp:759-774` (`vizSetUnderlay`): a size that does not fit the array is turned into a "clear" call (a decision).
- `geode_viz_jni.cpp:853-876`: `kMaxSwatch = 256` (viz:25) and the 5-stop limit clamp the sample counts for `paletteRamp`, `paletteStopsFromHues`, `paramsPalettePreview`; `paramsPalettePreview` returns an all-zero swatch when the params array is short (:872).
- `geode_viz_jni.cpp:205-213` (`vizTakeMilkPresetLoaded`): a path of 4,096 bytes or more is dropped.
- `geode_viz_jni.cpp:952-966` (`randomizeAllPick`): the picked style id is cut to a 128-byte buffer.
- `geode_viz_jni.cpp:1613-1624` (`vizRecordStart`): `ANativeWindow_fromSurface` and `ANativeWindow_release`; platform glue, the window is owned natively afterwards.
- `geode_export_dsp_jni.cpp:74-99` (`loudnessFeedDirect`): detects a misaligned span and copies it to an aligned buffer; picks the float or s16 ABI call.
- `geode_export_dsp_jni.cpp:122-143` (`encoderAttempts`): `kMaxCodecs = 2`, `kCapacity = 4` are hardcoded here and pack three arrays into triples.
- `geode_export_dsp_jni.cpp:15` `kReportDoubles = 5` and `geode_export_jni.cpp:15-23` `kLoopSpecLongs = 7` with the field order: wire layouts defined in the JNI file, not in `geode_api.h`.
- `geode_export_jni.cpp:85-105` (`loopSpecBudgeted`, `loopSpecDerive`): build a `geode_loop_spec` from three fields before the call.
- `geode_export_jni.cpp:227-237`: `recordEstimateBytes` and `recordRateLowered` are two views of one ABI call; the second runs the estimate again to read one flag.
- `geode_export_jni.cpp:33-44, 107-121, 144-159, 241-265`: interleave parallel arrays into flat triples or pairs (loops, no decisions); `chaptersFromMarkers` sizes for `count + 1` (the lead-in chapter rule leaks into the buffer size, :260-263).
- `geode_sound_jni.cpp:55-65` (`soundTuningSchema`): assembles defaults, minimums, maximums from two ABI calls into one array.
- `geode_jni.cpp:131-144` (`dspProcess`): chooses `geode_dsp_process` or `geode_dsp_process_pcm16` from a flag.
- `geode_tags_jni.cpp:63-85` (`tagsRead`): loops the text fields and clamps the art size to `INT32_MAX`.

### 5. Non-declaration members of `GeodeNative.kt`

- `init { System.loadLibrary("geode") }` at `:6-8`. The only init block.
- One non-external function: `fun frameLiquidMode(due: Int): Int = (due ushr 8) and 0xF` at `:675`; mirrors `GEODE_FRAME_LIQUID_MODE` in `core/api/geode_api.h` (a bit-field decode kept in Kotlin).
- 137 `const val`. 129 have a constant of the same name with a `GEODE_` prefix in `core/api/geode_api.h` (Grep by name; values were not compared one by one). `core/api/*.cpp` `static_assert`s tie the header constants to the core classes, for example `geode_surface_api.cpp:8-17`, `geode_stage_step_api.cpp:10-21`, `geode_record_api.cpp:11-18`, `geode_link_api.cpp:14-32`, `geode_frame_api.cpp:15-16`, `geode_sound_api.cpp:12-13`, `geode_auto_api.cpp:8-9`, `geode_style_pick_api.cpp:8`. Nothing ties the Kotlin values to the header at build time.
- The 8 with no header constant: `LOOP_SPEC_LOOP_MS` … `LOOP_SPEC_LONGS` (`:1166-1173`); they mirror `kLoopSpecLongs` and the field order written in `app/src/main/cpp/geode_export_jni.cpp:15-23`.

| Lines | Constants | Header group in `geode_api.h` |
|---|---|---|
| 587-593 | `HAND_UNCLAIMED`, `HAND_ALL`, `MUSIC_STATE_FLOATS` | `GEODE_HAND_*`, `GEODE_MUSIC_STATE_FLOATS` |
| 619-622 | `RECIPE_ON`, `RECIPE_FITS` | `GEODE_RECIPE_*` |
| 663-687 | `FRAME_CLEAR_ONLY`, `FRAME_THERMAL_DUE`, `FRAME_LIQUID_CHANGED`, `FRAME_ERROR_CHANGED`, `FRAME_PUSH_PARAMS/LFO/ADSR/TUNING` | `GEODE_FRAME_*` |
| 1166-1173 | `LOOP_SPEC_*` (8) | none; `geode_export_jni.cpp:15-23` |
| 1291-1300 | `RECORD_STOP_*` (5), `RECORD_AUDIO_RATE_HZ`, `RECORD_AUDIO_CHANNELS`, `RECORD_AUDIO_BPS` | `GEODE_RECORD_*` |
| 1600-1609 | `SOUND_SOURCE_STATE_LONGS`, `SOUND_SOURCE_USE_CAPTURE/START_CAPTURE/STOP_CAPTURE` | `GEODE_SOUND_SOURCE_*` |
| 1643, 1668 | `FRAME_CLOCK_STATE`, `CAST_FORMAT_INTS` | `GEODE_FRAME_CLOCK_STATE`, `GEODE_CAST_FORMAT_INTS` |
| 1801-1816 | `PERF_TIER_FLOATS`, `LIVE_FPS_MAX_CHOICES`, `LIVE_FPS_FOLLOW` | `GEODE_PERF_TIER_FLOATS`, `GEODE_LIVE_FPS_*` |
| 1849-1895 | `STAGE_*` (20: run flags, host, display, present) | `GEODE_STAGE_*` |
| 1926-1938 | `STAGE_STEP_*` (13) | `GEODE_STAGE_STEP_*` |
| 2037-2046 | `STYLES_ROLL_INSIDE`, `AUTO_TIMER_PLAYLIST`, `AUTO_TIMER_RANDOM` | `GEODE_STYLES_ROLL_INSIDE`, `GEODE_AUTO_TIMER_*` |
| 2048-2076 | `SURFACE_*` (12), `PACER_STATE_LONGS`, `PACER_CAP_SLOT`, `PACER_PACED_SLOT` | `GEODE_SURFACE_*`, `GEODE_PACER_*` |
| 2462-2487 | `CAST_AUDIO_STATE`, `CAST_AUDIO_STEP`, `CAST_AUDIO_FEED_STATE` | `GEODE_CAST_AUDIO_*` |
| 2512-2632 | `LINK_*` (45: resources, body codes, state, phases, reasons, events, flags, actions, wants, peers, receiver health) | `GEODE_LINK_*` |
| 2647-2661 | `QR_MAX_MODULES`, `WEBRTC_OFFER_MAX`, `WEBRTC_REFUSED_FULL`, `WEBRTC_REFUSED_NOT_READY` | `GEODE_QR_MAX_MODULES`, `GEODE_WEBRTC_*` |
| 2727 | `HLS_TRACKS` | `GEODE_HLS_TRACKS` |

### 6. Native platform use today

Linked (CMake): `geode` links `geode_core log android` (`CMakeLists.txt:141`); `geode_core` links `projectM tag GLESv3 EGL log android nativewindow` (`core/CMakeLists.txt:168`), `oboe::oboe` (`:170`, found by `find_package(oboe REQUIRED CONFIG)` at `CMakeLists.txt:87`), `geode_meshopt` (`:185`), `geode_basisu` (`:234`), `LibDataChannel::LibDataChannelStatic` (`:242`). Third-party sources built: projectM as a shared library (`CMakeLists.txt:68-70`, patched at configure time, `:33-64`), TagLib (`:81`), Mbed TLS static (`:96-103`), libdatachannel (`:123`), meshoptimizer (4 files), cgltf (header), basis_universal encoder + transcoder + zstd.

**Not linked: `mediandk`, `aaudio`, `OpenSLES`, `camera2ndk`, `jnigraphics`, `vulkan`.** `aaudio` is not named anywhere in either CMake file; Oboe reaches it itself (not verified how its prefab package links it).

Exists:
- **EGL** (uses the context Kotlin made current; core creates no context): `core/viz/frame/RecordTap.cpp` (`eglChooseConfig` :23, `eglCreateWindowSurface` :61, `eglMakeCurrent` :64, `eglSwapInterval` :67, `eglDestroySurface` :74, `eglPresentationTimeANDROID` :111, `eglSwapBuffers` :113); `core/viz/frame/CastTee.cpp` (`eglGetProcAddress` :41, `eglCreateImageKHR` :48, `eglCreateSyncKHR` :50, `eglClientWaitSyncKHR` :52, `eglGetCurrentDisplay` :78, `eglGetCurrentContext` :147); `core/viz/frame/LoopSeam.hpp:50` (`eglPresentationTimeANDROID`); `core/api/geode_viz_api.cpp:987` (`eglGetCurrentContext`).
- **`ANativeWindow`**: `app/src/main/cpp/geode_viz_jni.cpp:1617, 1622` (`ANativeWindow_fromSurface`, `ANativeWindow_release`), `core/api/geode_api.h:239`, `core/viz/frame/RecordTap.hpp:26`. The record encoder's input surface is the only window native owns.
- **`AHardwareBuffer`**: `core/viz/frame/CastTee.cpp:81, 90, 107` (allocate, release) with `eglGetNativeClientBufferANDROID` (:47).
- **`AAssetManager`**: `geode_viz_jni.cpp:89, 130, 1178` (`AAssetManager_fromJava`), `core/viz/ShaderSource.cpp:59-63` (`AAssetManager_open`, `AAsset_getLength`, `AAsset_read`), `core/api/geode_export_pass_api.cpp:21`.
- **Oboe**: `core/audio/output/OboeOutput.cpp` (44 `oboe::` uses; data callback :284, error callback :291), `OboeOutput.hpp:34`.
- **`std::thread`**: `core/audio/output/OboeOutput.cpp:272`, `core/viz/ProgramBinaryCache.hpp:66`. No other thread is started in `core/` outside `core/tests/`.
- **`__android_log_print`**: `core/util/Log.hpp:4`, `OboeOutput.cpp:78`.

Does not exist at all (Grep of `core/` and `app/src/main/cpp/`, tests excluded, zero hits): `eglCreateContext`, `eglInitialize`, `eglCreatePbufferSurface`, `AChoreographer`, `ALooper`, `ASensor`, `AThermal`, direct `AAudio*` calls, `AMediaCodec`, `AMediaMuxer`, `AMediaExtractor`, `AMediaFormat`, `AImageDecoder`, `ASurfaceControl`, `APerformanceHint`, `ATrace`, `pthread_*`, `std::jthread`, `std::async`.

So today Kotlin owns: every EGL context and the display surface, the vsync source (`Choreographer`, `FramePacer.kt:200`), sensors, thermal reads (pushed in through `thermalPush`), and every MediaCodec, MediaMuxer and MediaExtractor loop.

### 7. Manifest (`app/src/main/AndroidManifest.xml`)

Features:
- `:4-6` OpenGL ES 3.0, required.
- `:15-17` `android.hardware.microphone`, not required.
- `:77-79` `android.software.live_wallpaper`, not required.
- `:83-85` `android.software.picture-in-picture`, not required.
- `:86-88` `android.software.daydream`, not required.
- `:92-94` `android.software.leanback`, not required.
- `:95-97` `android.hardware.touchscreen`, not required.
- `:98-100` `android.hardware.faketouch`, not required.

Permissions:
- `:8` `READ_MEDIA_AUDIO`.
- `:14` `RECORD_AUDIO`.
- `:27` `FOREGROUND_SERVICE`.
- `:35` `FOREGROUND_SERVICE_MEDIA_PROJECTION`.
- `:41` `FOREGROUND_SERVICE_MEDIA_PLAYBACK`.
- `:45` `FOREGROUND_SERVICE_MICROPHONE`.
- `:52` `FOREGROUND_SERVICE_MEDIA_PROCESSING`.
- `:53` `FOREGROUND_SERVICE_DATA_SYNC`.
- `:54` `POST_NOTIFICATIONS`.
- `:61` `INTERNET`.
- `:62` `ACCESS_NETWORK_STATE`.
- `:65` `VIBRATE`.
- `:69` `MODIFY_AUDIO_SETTINGS`.
- `:74` `WAKE_LOCK`.

Queries (`:107-115`): `android.media.browse.MediaBrowserService`; `MAIN` + `APP_MUSIC`.

Application (`:117-129`): `.GeodeApp`, `allowBackup="true"`, `appCategory="audio"`, TV banner, `enableOnBackInvokedCallback="true"`. Meta-data: `com.google.android.gms.car.application` (`:131-133`), Cast `OPTIONS_PROVIDER_CLASS_NAME` = `dev.geode.cast.CastOptionsProvider` (`:361-363`).

Activities:
- `:139-146` `.ui.MainActivity`: exported, `singleTask`, PiP, handles its own config changes. Filters: `MAIN` + `LAUNCHER` + `LEANBACK_LAUNCHER` (`:147-151`); `VIEW` `geode://preset` and `geode://template` (`:158-168`); `VIEW` `.milk` files with a MIME type (`:176-188`) and without (`:190-201`); `VIEW` `content` audio (`audio/*`, `application/ogg`, `application/x-flac`, `:208-215`); `MEDIA_PLAY_FROM_SEARCH` (`:218-221`).
- `:233-237` `.ui.wallpaper.WallpaperOptionsActivity`: exported.
- `:413-424` `.tile.TileLongPressActivity`: exported, `QS_TILE_PREFERENCES`, no history, no display theme.

Services:
- `:238-248` `.wallpaper.VisualizerWallpaperService`: exported, `BIND_WALLPAPER`.
- `:257-268` `.wallpaper.VisualizerDreamService`: exported, `BIND_DREAM_SERVICE`.
- `:282-290` `.playback.PlaybackService`: exported, type `mediaPlayback`; `MediaLibraryService`, `MediaSessionService`, `MediaBrowserService`.
- `:327-330` `.audio.PlaybackCaptureService`: not exported, type `mediaProjection`.
- `:339-342` `.export.ExportService`: not exported, type `mediaProcessing|dataSync`.
- `:350-353` `.cast.CastService`: not exported, type `mediaPlayback|microphone`.
- `:372-379` `.audio.GeodeNotificationListener`: not exported, `BIND_NOTIFICATION_LISTENER_SERVICE`.
- `:400-409` `.tile.PlaybackTileService`: exported, `BIND_QUICK_SETTINGS_TILE`.

Receivers:
- `:309-315` `androidx.media3.session.MediaButtonReceiver`: exported, `MEDIA_BUTTON`.
- `:364` `androidx.mediarouter.media.MediaTransferReceiver`.
- `:383-392` `.widget.NowPlayingWidgetReceiver`: exported, `APPWIDGET_UPDATE`.

Provider: `:429-436` `androidx.core.content.FileProvider`, authority `${applicationId}.presets`, not exported.

### 8. Surprises

- **`soundSourceStep` has no `return`.** `app/src/main/cpp/geode_sound_jni.cpp:42-52` is declared `jint`, computes `const int out` at `:49` and falls off the end. That is undefined behaviour; Kotlin reads the result as the capture action bits at `app/src/main/java/dev/geode/wallpaper/SoundSource.kt:24, 99`. `-Wall -Wextra` is on (`CMakeLists.txt:15-16`) but warnings are not errors.
- **There is no native → Kotlin callback and no cached `JavaVM`.** Any port that moves a loop into `core/` and needs to tell Kotlin something (encoder output ready, a frame drawn, an error) has no channel today other than Kotlin polling.
- **No native thread drives a frame.** The only native threads are Oboe's, one close thread and the shader cache writer. Every frame, vsync, encode and decode loop is a Kotlin thread calling down.
- **No `mediandk`.** Not linked and no `AMedia*` symbol exists; all codec and muxer work is Java-side. `core/output/Recorder` only keeps time and audio, the encoder is Kotlin (`A/record/RecordEncoder.kt`).
- **Core never creates an EGL context.** `RecordTap` and `CastTee` borrow the context current on the calling Kotlin GL thread (`eglGetCurrentContext`), so they cannot run without a Kotlin-owned GL thread today.
- **The `geode_viz*` guard costs two locks per call** (viz:44-65), five or six times a frame, and no other handle type has any guard.
- **Text returns use `NewStringUTF` on core text** in about 45 places although `jni_util.hpp:9-11` says that call aborts under CheckJNI on plain UTF-8; a macro knob name with an emoji goes through `macroBankRename` (viz:289-294) and back out this way.
- **Every string argument costs 4 JNI calls and a Java `byte[]`** (`jni_util.hpp:14-29`); the macro editor passes the whole bank text in and out on each edit.
- **Two wire layouts live only in the JNI files**, not in `geode_api.h`: the loop spec (`geode_export_jni.cpp:15-23`, mirrored by `GeodeNative.LOOP_SPEC_*`) and the loudness report (`geode_export_dsp_jni.cpp:15`).
- **Kotlin constants are hand-copied**: 137 `const val` with no generated or asserted link to `geode_api.h`.
- **Nine externals have no caller** and one has only a device test (list in section 1). `playoutDestroy` is among them: the playout handle made by `playoutCreate` is never freed from production code.
- **`vizWriteAudio` is a test-only path** that allocates a `std::vector` per call (viz:227); the comment at viz:220 still describes it as the every-frame feed.
- **`geode_sound_jni.cpp` is misnamed for half its content**: it holds the surface, stage, stage-step, auto-timer and style-pick marshalling (`:88-261`).
- **Export allocates Java arrays per audio block** (`resamplerProcess`, `pcmDownmixS16`, `pcmFloatToS16` return new `ShortArray`s), unlike the live path, which is copy-free.
- `MainActivity`'s `.milk` filter matches `mimeType="*/*"` with a path pattern (`AndroidManifest.xml:176-188`), so the app is offered for any file whose name ends in `.milk` from any provider.


---

## 03 · export / record / editor

Read in full: 24 Kotlin files, 5,449 lines (`export/` 18 files 4,289 lines, `record/` 1 file 268 lines, `editor/` 5 files 888 lines, all under `app/src/main/java/dev/geode/`), plus 7 test files, 483 lines. Native headers read: every `.hpp` in `core/output/`, and `ExportPasses.hpp`, `LoopSeam.hpp`, `RecordTap.hpp` in `core/viz/frame/`, `TrackFeed.hpp` in `core/audio/ingest/` (others there listed only).

Notes on method:
- The working tree's HEAD is `5dbfc69`, not `f13229d` as the brief says. Line numbers below are from the files as they are at `5dbfc69`.
- Callers were checked with ripgrep over `app/src/main` and `engine/`; Graphify was not used. "Dead" below means no production caller in those two trees.
- Paths below are relative to `app/src/main/java/dev/geode/` unless they start with `core/`, `engine/` or `app/`.

### 1. Per-file table

| File (lines) | Class | Engine logic: function, lines, what it decides | Thread | Native calls (`GeodeNative.`) | Native owner in `core/` | What must stay Kotlin and why |
|---|---|---|---|---|---|---|
| `export/VideoExporter.kt` (632) | **engine** (with a storage shell around it) | `ExportCodec.attempts` 47-56: codec order (requested, then H.264), reads the encoder's top frame rate; `encodeInto` 350-537: the whole render (order audio→encoder→EGL→renderer→watermark, frame loop 434-470, in-loop drain 442-463, EOS drain 474-500, audio interleave 464-468 and 503-508, muxer stop rules 509-529); `videoFormat` 539-548; `openEncoder` 551-567 (ladder try-loop); `writeSample` 570-583 (drops codec-config and empty buffers); `AudioFeed.writeUpTo` 600-624 (audio/video interleave order and cut at `limitUs`); `measureLoudness` 333-347. Tables: `ExportQuality` 77-84 (short side, bit rate), `ExportRatio` 86-97. Constants: `FPS=60` 125, `TIMEOUT_US=10_000` 126, `FLUSH_ATTEMPT_LIMIT=1_000` 128, `AUDIO_PROGRESS_SPAN=0.1f` 134, the `0.85f` progress share 469. | `Dispatchers.Default` (`export` 188), one coroutine from `ExportRun.scope`; blocking, no suspension inside `encodeInto` | `encoderAttempts`, `exportFrameSize`, `exportClampFps`, `exportTotalFrames`, `exportFrameTimeMs`, `exportFramePtsNs`; via `MuxerBudget` `exportCappedVideoBps`; via renderer/watermark see those rows | `core/output/EncoderLadder.hpp` (attempt list), `core/output/EncodePlan` (frame size, bit-rate cap, fps clamp), `core/output/FrameClock` (frame count, frame time, PTS), `core/output/PreRoll.hpp`. No native owner yet for the loop itself, the drain, the mux and the interleave: new `core/output/ExportJob` (driver) + new `core/output/Mp4Sink` (AMediaCodec/AMediaMuxer wrapper) | `MediaStore` insert / `IS_PENDING` publish / delete (212-256), SAF `openFileDescriptor` and `DocumentsContract.deleteDocument` (277-311): hand native the fd. `MediaCodecList` capability lookup (`getSupportedFrameRatesFor`, 53, 61-67): `AMediaCodec` has no capability query. String resources for errors. `ProSnapshot.gate` (422). |
| `export/AudioTranscoder.kt` (510) | **engine** | `transcode` 242-503: extractor→decoder→trim→downmix→resample→AAC loop 365-484, encoder creation deferred until the decoder's real format is known (`ensureEncoder` 293-318), carry PTS (`feedEncoder` 345-362), end-of-range rule 373, stall rule 479-483; `transcodeAiff` 81-235: AIFF read→downmix→resample→AAC loop 144-221, start skip 131-139, end rule 149; `downmix` 47-56; `resample` 66-79. Constants: AAC-LC, 192 000 bit/s, max input 65 536 (95-99, 307-311), dequeue timeout 10 000 µs, `STALL_LIMIT=1_000` 506, read buffer 16 384 shorts 122, `channels = min(src, 2)` 89, 278. | caller's thread: `Dispatchers.Default` (called from `VideoExporter.encodeInto` 380 and `LoopExtend.transcode` 306) | `aacEndUs`, `aacRateFor`, `pcmDownmixS16`, `pcmFloatToS16`, `pcmTrimS16`, `pcmS16BytesToUs`, `pcmSamplesBeforeUs`, `resamplerCreate/Process/Destroy` (through `PcmResampler`) | `core/audio/dsp/PcmConvert`, `PcmTiming`, `PcmResampler` (maths); `core/audio/ingest/AiffReader` (AIFF). The loop: new `core/output/AacTranscode` | Opening a `content://` Uri (give native an fd from `ContentResolver`). `context.cacheDir` path for the `geode_aac_*.bin` scratch. |
| `export/LoopRender.kt` (753) | **engine** | `LoopSpec` 39-95 (range constants 72-78, `require`s 45-48); `TimeOfDayDrift` 108-134 (`MAX_STOPS=8`); `DriftStop.applyTo` 143-147; `render` 237-275; `renderStops` 305-366 (stop loop, shortest-seam rule 333, effective crossfade 357); `renderStop` 368-424 (build order); `drawFrames` 430-464 (frame loop); `prepareRenderer` 466-493 (drift applied to look and keyframed params, `pinDurationMs = sourceFrames*1000/fps` 489); `negotiate` 512-526 (bit-rate cap for the long form, first accepted attempt); `accepts` 528-547 (probe configure); `budgetedSpec` 553-561; `videoFormat` 563-574; `StopWriter` 590-701 (`drain` 608-622, `finish` 624-631, `writeSample` 656-672); `SeamStash` 713-740 (handle only). Constants `TIMEOUT_US=10_000`, `FLUSH_ATTEMPT_LIMIT=1_000` 748-749. | `Dispatchers.Default` (255) | `loopSpecDerive`, `loopSpecOf`, `loopSpecBudgeted`, `loopDriftStops`, `loopDriftApply`, `exportLoopReportedCrossfadeMs`, `exportLoopProgress`, `exportSeamCreate`, `exportSeamAllocate`, `exportSeamFrame`, `exportPassError`, `exportPassDestroy`, `encoderAttempts` (via `ExportCodec.attempts`), `exportCappedVideoBps`, `exportAudioBytes` | `core/output/LoopPlan` (spec, drift, budget), `core/viz/frame/LoopSeam.hpp` (which frame is stashed/presented, PTS, progress), `core/viz/frame/ExportPasses` `SeamPass`, `core/output/EncoderLadder.hpp`, `core/output/EncodePlan`. The stop loop, frame loop and writer: new `core/output/LoopJob` over the same `Mp4Sink` | `MediaMetadataRetriever` duration read (`soundtrackMs` 213-229; an `AMediaExtractor` track format also carries the duration). `cacheDir`, strings, `ProSnapshot`. |
| `export/LoopExtend.kt` (642) | **engine** | `LoopExtendPlan.of` 84-95; `write` 178-209 (size refusal 189-190); `mux` 236-277 (repeat loop 259-265, interleave rule 262, 266); `checkOneEncoding` 285-291; `transcode` 293-326 (all clips forced to 48 kHz 311, clip start = running sum 317); `joinable` 334-345; `LoopReader.copyInto` 479-498 (PTS shift, cut at total, sync-flag mapping 528-529); `sampleBufferBytes` 532-537; `AudioReel.writeUpTo` 571-586 and `write` 592-609. Constants `AUDIO_PROGRESS_SPAN=0.35f` 636, `SCRATCH_BYTES` 637, `MIN_SAMPLE_BUFFER_BYTES=1 MiB` 638. | `Dispatchers.Default` (141) | `loopExtendPlan`, `loopEstimatedBytes`, `loopFittingBitRate`, `exportMuxerHardLimitBytes`, `chaptersFromClips` (via `ChapterMarkers.of`) | `core/output/LoopPlan` `LoopExtendPlan`, `core/output/EncodePlan` `kMuxerHardLimitBytes`, `core/output/Chapters`. The copy/mux loop: new `core/output/LoopExtend` | MediaStore/SAF target (`openTarget` 384-415, `publish` 417, `discard` 420-427), strings. |
| `export/LoudnessMeter.kt` (346) | **engine** | `decode` 99-135, `drain` 138-231: full decode loop 152-223, mid-file format-change refusal 190-193, PCM layout rule 255-267, stall rule 218-222. `LoudnessAnalyser` 285-344 is a handle wrapper. | `Dispatchers.IO` (88) | `loudnessCreate`, `loudnessFeedDirect`, `loudnessHasBlock`, `loudnessFinish`, `loudnessDestroy`, `loudnessSupports` | `core/audio/dsp/Loudness` (meter). The decode loop: new, shared with the transcoder's decode (`core/output/AudioDecode`) | Uri → fd. Result types and the English `Unreadable` messages (should become string resources). |
| `export/EncoderSurface.kt` (90) | **engine** | `setUp` 31-64: EGL config (RGBA 8888, ES3 renderable, `EGL_RECORDABLE_ANDROID` 0x3142), ES 3 context, window surface; `setPresentationTimeNs` 70-72; `swapBuffers` 74-76. | export thread (`Dispatchers.Default`); also `cast/CastStream.kt:191` | none | new: EGL owner in native (`core/viz/frame` beside `RecordTap`, which already creates an EGL window surface from an `ANativeWindow`) | Nothing: EGL14 and `eglPresentationTimeANDROID` are plain EGL in the NDK. |
| `record/RecordEncoder.kt` (268) | **engine** | `open` 238-266 (encoder try-loop, video format); `start` 68-105 (AAC format, starts thread); `pump` 110-125 (the loop); `feedAudio` 132-150 (EOS rule 145-146); `drain` 153-175; `write` 178-191 (holds samples until both formats are known); `startMuxerWhenReady` 193-207 (track order video then audio, flush held); `release` 210-220 (stop only if a video sample was written). Constants `PCM_FRAMES=4096`, `IDLE_MS=5`, `TEE_WAIT_MS=1_000` 227-231. | own `Thread` `"record-encoder"` (93-103); `open` on `Dispatchers.IO` (`ui/RecordController.kt:162`) | `recorderLimitMs`, `recordEncoderAttempt`, `recorderStopped`, `recorderTakeAudio`, `recorderVideoEncoded`, `recorderWrote`, `recorderStop`, `recordNowNs`; constants `RECORD_AUDIO_RATE_HZ/CHANNELS/BPS`, `RECORD_STOP_FAILED` | `core/output/Recorder` (clock, audio hold, `encoderAttempt`, `videoBps`, `frameSize`), `core/viz/frame/RecordTap` (GL copy). The pump thread and muxer: new `core/output/RecordSink` on the shared `Mp4Sink` | The output fd (`ui/RecordController` gets it from its file store). The `Surface` object is only needed while the tee is started from Kotlin (`renderer.recordStart(..., surface, ...)`, `ui/RecordController.kt:197`); with a native encoder the `ANativeWindow` never crosses JNI. |
| `export/WatermarkOverlay.kt` (97) | **mixed** | `rasteriseTile` 69-90: where the text sits in the tile (centred on the baseline, 87), the rotation pivot (85); constants `SHADOW_ALPHA=70`, `SHADOW_FRACTION=0.12f`, `LETTER_SPACING=0.04f`, white bold sans (93-95, 73-78). Everything else is a handle. | export thread; for Record, the stage GL thread (`ui/RecordController.kt:196`) | `exportWatermarkPlacement`, `exportWatermarkCreate`, `exportWatermarkDraw`, `exportWatermarkTextPx`, `exportWatermarkAlpha`, `exportWatermarkTileWidth`, `exportWatermarkTileHeight`, `exportWatermarkAngle`, `exportPassError`, `exportPassDestroy` | `core/viz/frame/ExportPasses` `WatermarkPass`, `ExportPassMath.hpp`. The three look constants: move to `ExportPassMath.hpp` and read them | `Paint`/`Canvas`/`Bitmap` text raster and the string resource: no NDK text rasteriser. |
| `export/ExportRun.kt` (93) | **mixed** | Holds the one render scope (12), the cancel flag the frame loops poll (28-30), `publish` 77-86 (clamp + ETA, per frame). | `scope` = `SupervisorJob + Dispatchers.Default`; `publish` on the export thread | none | cancel flag and progress belong to the native job (new `core/output/ExportJob`); the `StateFlow` stays | `StateFlow<State>` for UI and the service; the `Job`. |
| `export/ExportRange.kt` (31) | shell | `of` 18-29 hands the segment to native `geode_export_range` (`core/output/ExportRange.hpp`), which holds the clamp, the 1 s minimum and the "whole track = no range" rule. Caller `ui/StudioRender.kt:682`. | main (UI) | none | n/a | The data class as a carrier. |
| `export/ExportPresets.kt` (28) | **engine** (table) | `ALL` 19-27: six platform presets (quality, ratio, fps, loop-safe). Order is load-bearing: `data/ExportPrefs.kt:76` indexes it by `ExportDestination.ordinal`. | main | none | new: table in `core/output/EncodePlan` | Name string resources. |
| `export/ChapterMarkers.kt` (233) | **mixed**, partly **dead** | Engine: sidecar text formats `descriptionText` 107-108, `webVttText` 110-118, `ffMetadataText` 120-129, `timestamp` 205-215, `vttTimestamp` 217-228. Shell: titles 149-154, 181-191. | `Dispatchers.Default` (from `LoopExtend.finish` 223) | `chaptersFromClips`, `chaptersFromMarkers` | `core/output/Chapters` (spans). Formats: new, same file | Title fallbacks (localised). Dead in production: `of(markers, …)` 164-174, `at` 84, `text` 86, `writeTo` 93, `ChapterFormat`; `LoopExtend.Result.Saved.chapters` is never read (`ui/ExportController.kt:547` takes only `uri`). |
| `export/LoudnessTarget.kt` (368) | **shell** (wording) with a thin marshal | `LoudnessTargets.advise` 325-356 only unpacks native's 9 doubles; which sentence is chosen (`lowerDetail` 253-311) branches on native's `band` and `ceilingLimited`. `nativeIndex` 0/1/2 (70, 80, 90) mirrors `core/output/LoudnessAdvice.hpp` `Target`. | `Dispatchers.Default`; text on main | `loudnessWindow`, `loudnessAdvise` | `core/output/LoudnessAdvice` | String resources, number formatting. `summary` strings 72, 82, 92 are hard-coded English. |
| `export/MuxerBudget.kt` (37) | **shell** wrapper; one **duplicate** constant | `AAC_BPS = 192_000` (19) duplicates `core/output/EncodePlan.hpp` `kAacBps`. | export thread | `exportCappedVideoBps`, `exportAudioBytes` | `core/output/EncodePlan` | Nothing once the callers are native. |
| `export/PcmResampler.kt` (61) | **shell** handle; one engine constant | `MIX_RATE_HZ = 48_000` (59). Passthrough copy 41. Also used by `cast/CastStream.kt:310,376-380`. | export / cast audio thread | `resamplerCreate`, `resamplerProcess`, `resamplerDestroy` | `core/audio/dsp/PcmResampler` | Nothing once the callers are native. |
| `export/RenderBatch.kt` (105) | **shell** (job-list bookkeeping) | `RenderScope.tracksFor` 39-51 (dedupe by uri), `RenderBatch.progress` 101-104. Decides nothing about picture or sound. | main / `ExportRun.scope` | none | none needed | Stays. |
| `export/ExportService.kt` (139) | **shell** | none; notification re-post at most once a second (38-47). | main (`Dispatchers.Main.immediate`, 26) | none | none | Foreground service, notification, wake lock. |
| `export/StudioClips.kt` (19) | **shell** | none | caller's | none | none | `ContentResolver.delete`. |
| `editor/Timeline.kt` (277) | **mixed**, engine part **dead** | `Clip.trimmedToEnd` 154-163; `magnetise` 249-268, `SnapMode.resolve/snap` 234-247 (snap tie-break order). Constants `MIN_CLIP_DURATION_MS=33` (19; live: `ui/ExportController.kt:391`), `DEFAULT_SNAP_TOLERANCE_MS=60` 13, `BeatCut.DEFAULT_BEATS_PER_CUT=4` 59. | main | none | none; if kept, new `core/output/ProjectFit` | The data classes (the saved project shape). `magnetise` and `trimmedToEnd` have no production caller outside `editor/`. |
| `editor/StudioProject.kt` (309) | **mixed**, engine part **dead** | `fittedTo` 241-267 (fit a template to a song), `withFreshIds` 283-309, `ProjectTemplate.newProject` 193-216, `batch` 222-227: no production caller. Live: `StudioProject.create` 111-137, `audioLane` 139-162, `lengthMs` 97-98, `withRender` 101, `withoutRender` 103 (`ui/EditorController.kt:71,80,98,118,127,133`). Defaults `DEFAULT_LOOP_LENGTH_MS` 47, `DEFAULT_LOOP_MS=20_000` 48. | main / IO | none | none | The model and `create`. |
| `editor/Markers.kt` (118) | **mixed**, engine part **dead** | `MarkerSet.moveTo` 99-110 (snap + origin rule), `nearest` 112-115: no production caller. `addAll` is used by `data/EditorProjectJson.kt:112`. | main | none | none | The model, `MarkerColour` argb table (saved by name). |
| `editor/Keyframes.kt` (160) | **shell** (saved shape only) | `EaseShape` 88-95 holds four bezier control-point sets; nothing evaluates them (file comment 9-11, and no caller of a `valueAt`). | n/a | none | if keyframes ever play again: native | The model, for loading old projects. |
| `editor/ClipTransition.kt` (24) | **shell** (saved shape only) | `DEFAULT_TRANSITION_MS=800`, `HARD_CUT="cut"`; nothing draws a transition. | n/a | none | none | The model. |

Counts: engine 9, mixed 8, shell 7 (two of them carrying one duplicated/engine constant each), no file wholly dead or wholly duplicate.

### 2. Threads started here

| Where | Name / kind | What runs | How often |
|---|---|---|---|
| `export/ExportRun.kt:12` | `CoroutineScope(SupervisorJob() + Dispatchers.Default)`, process-wide | every render (`launch` 46), one at a time | per render |
| `export/VideoExporter.kt:188` | `withContext(Dispatchers.Default)` | transcode, then the frame loop, drain, mux; blocking | per render |
| `export/LoopRender.kt:255` | `withContext(Dispatchers.Default)` | negotiate + every drift stop's frame loop | per loop render |
| `export/LoopExtend.kt:141` | `withContext(Dispatchers.Default)` | soundtrack transcodes, then the copy/mux loop | per loop render |
| `export/LoudnessMeter.kt:88` | `withContext(Dispatchers.IO)` | full decode of the finished file | once after each saved render |
| `export/ExportService.kt:26` | `CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)` | collects `ExportRun.state`, re-posts the notification, `delay(1_000)` (46) | at most 1 Hz |
| `record/RecordEncoder.kt:93-103` | `Thread(..., "record-encoder")` | `pump`: audio feed + two drains; `Thread.sleep(5)` when idle (123) | for the length of a recording |
| `record/RecordEncoder.kt:47,127-129` | `CountDownLatch teeClosed`, awaited up to 1 000 ms on the encoder thread | waits for the GL thread to release the encoder surface | once per recording |

No `HandlerThread`, `Executor` or `Handler` ticker in scope. `editor/` starts none.

### 3. Every loop (the port's work list)

**A. One-tap render frame loop** — `VideoExporter.encodeInto`, `export/VideoExporter.kt:434-470`, on `Dispatchers.Default`.
- Per iteration: `renderer.renderFrame(frame)` (which calls `exportFrameTimeMs`, `trackFeedTargetFrame`, `viz.hearTrack`, `exportFramePtsNs`, `viz.render`: `engine/scenes/.../offscreen/OffscreenSceneRenderer.kt:100-113`), `exportFrameTimeMs` (437), `exportWatermarkDraw` (438), `exportFramePtsNs` (439), `eglPresentationTimeANDROID`, `eglSwapBuffers`.
- Kotlin decisions: draw order scene → watermark → PTS → swap; non-blocking drain after every frame (timeout 0); audio written up to `timeMs * 1000` µs after each frame (467), so audio never runs ahead of the video frame just drawn; progress = `0.1 + frame/total * 0.85` (469); cancel polled once per frame (435).
- PTS comes from native (`FrameClock::ptsNs`, floor(n·1e9/fps)).

**B. In-loop video drain** — `export/VideoExporter.kt:442-463`. `dequeueOutputBuffer(info, 0)` until it returns a negative index. On `INFO_OUTPUT_FORMAT_CHANGED` before start: `addTrack(video)`, then `addTrack(aac.format)`, then `muxer.start()` (450-453): video is track 0, audio track 1. A second format change after start is logged and ignored (445-448).

**C. End-of-stream drain** — `export/VideoExporter.kt:471-500`. `signalEndOfInputStream()`, then `dequeueOutputBuffer(info, 10_000)`; only an empty poll counts toward `FLUSH_ATTEMPT_LIMIT = 1_000` (so up to about 10 s of silence from the encoder). Fails with `export_error_encoder_no_output` if the muxer never started and `export_error_encoder_stalled` if EOS never came (501-502), unless cancelled. After EOS the rest of the audio is written (`writeUpTo(Long.MAX_VALUE)`, 506) and `muxer.stop()` runs only when not cancelled (509-512). In `finally`, a started muxer with no sample written is not stopped (517-529).

**D. Audio interleave** — `AudioFeed.writeUpTo`, `export/VideoExporter.kt:600-624`. Reads AAC samples back from the `geode_aac_*.bin` scratch file by the `(offset, size, pts, flags)` list; stops at the first sample with `pts >= upToUs`; drops everything with `pts >= limitUs` (the transcoded duration). No native call.

**E. Encoder ladder try-loop (render)** — `openEncoder`, `export/VideoExporter.kt:551-567`. For each attempt from `ExportCodec.attempts` (47-56, list from `GeodeNative.encoderAttempts`): `createEncoderByType`, `configure`; first success wins; the last exception is rethrown. The frame rate actually used is the accepted attempt's.

**F. Encoder probe (loop render)** — `negotiate`/`accepts`, `export/LoopRender.kt:512-547`. Same attempt list; each attempt is configured and released at once (a probe), the first accepted is kept; the real encoder is created again per stop in `StopWriter.open` (675-699). This is a second copy of loop E with different failure handling (null → `export_error_loop_encoder_rejected`).

**G. Loop render stop loop** — `renderStops`, `export/LoopRender.kt:315-341`. One iteration per drift stop: a new temp file `geode_loop_<stop>_*.mp4`, a new encoder, muxer, EGL context, seam stash, renderer and watermark (`renderStop` 368-424). Kotlin decides: the seam for later stops is the minimum any earlier stop got (333).

**H. Loop render frame loop** — `drawFrames`, `export/LoopRender.kt:447-462`. Per iteration: `scene.renderFrame(frame)`, `exportSeamFrame` (returns PTS ns or -1), and when presented: `exportWatermarkDraw`, set PTS, swap, `writer.drain(false)`; then `exportLoopProgress`. Which frames are stashed, blended and presented, and their PTS (`(frame - seamFrames) * (1e9 / fps)` truncated) are native (`core/viz/frame/LoopSeam.hpp:38-53`). Note the PTS rule differs from loop A's (`floor(n·1e9/fps)`).

**I. Loop render drain** — `StopWriter.drain`, `export/LoopRender.kt:608-622`. Timeout 0 while rendering (one empty poll ends it), 10 000 µs at the end with at most 1 000 empty polls. `openTrack` 650-654: video-only file. `finish` 624-631 checks output and EOS.

**J. Audio transcode loop (codec source)** — `AudioTranscoder.transcode`, `export/AudioTranscoder.kt:365-484`. One pass per iteration: (1) feed the decoder one extractor sample, timeout 10 000 µs, EOS when `size < 0` or `sampleTime > endUs` (373); (2) if no carry, take one decoder output (timeout 10 000 µs), convert float→s16 (`pcmFloatToS16`), downmix (`pcmDownmixS16`), trim the frames before the range start (`pcmTrimS16`, 425), resample (`resamplerProcess`); (3) feed the encoder as much of the carry as one input buffer holds, PTS = trim PTS + bytes so far (`pcmS16BytesToUs`, 357); or send EOS; (4) drain the encoder (timeout 0), append to the scratch file, record `(offset, size, pts, flags)`. Stall after 1 000 idle iterations (479-483). Per decoder buffer: 2-4 JNI calls, each with a fresh `ShortArray`/`FloatArray` copy.

**K. Audio transcode loop (AIFF source)** — `transcodeAiff`, `export/AudioTranscoder.kt:144-221`. Same shape without a decoder; reads 16 384 shorts at a time through `AiffPcm.read`; PTS = `pcmS16BytesToUs(fedBytes)` from zero (174); ends when the read is empty or `carryTimeUs > maxUs` (149). Encoder drain timeout is 10 000 µs once EOS is sent, else 0 (188): differs from loop J, which always uses 0 (453).

**L. Loudness decode loop** — `LoudnessMeter.drain`, `export/LoudnessMeter.kt:152-223`. Extractor→decoder, both timeouts 10 000 µs; each output buffer goes to `loudnessFeedDirect` in place (direct buffer, offset, size, float flag). Stall after 1 000 idle iterations.

**M. Long-form repeat/mux loop** — `LoopExtend.mux`, `export/LoopExtend.kt:259-266`. Track order: video (`readers.first().format`), then audio, then `start()` (253-255). Per repeat: `copyInto` (N below), then `audio.writeUpTo(offsetUs + loopDurationUs)` (262); progress `0.35 + 0.65 * done/repeats`. No native call in the loop; the plan (`loopExtendPlan`) is computed once.

**N. Sample copy loop** — `LoopReader.copyInto`, `export/LoopExtend.kt:479-498`. `seekTo(0, SEEK_TO_PREVIOUS_SYNC)`, then for each sample while `offsetUs + sampleTime < limitUs`: PTS = `offsetUs + sampleTime`, flag = `BUFFER_FLAG_KEY_FRAME` iff `SAMPLE_FLAG_SYNC`. Relies on 1 s key-frame interval and no B-frames (comment 457-460).

**O. Long-form audio loop** — `AudioReel.writeUpTo`, `export/LoopExtend.kt:571-586`. PTS = `clip.startUs + sample.pts`; clips are concatenated without removing AAC priming (comment 556-558).

**P. Record pump** — `RecordEncoder.pump`, `record/RecordEncoder.kt:112-124`, on thread `record-encoder`. Per iteration: `recorderStopped`; `feedAudio` (`recordNowNs` + `recorderTakeAudio`, up to 4 096 frames into a direct buffer, PTS from native); `drain(video)` (`recorderVideoEncoded` per frame); `drain(audio)`; `recorderWrote` + `recordNowNs` per written sample; sleep 5 ms when nothing moved. Kotlin decisions: on stop, ask the GL thread to close the tee, wait up to 1 s, then `signalEndOfInputStream` (114-119); audio EOS is the first empty `takeAudio` after stop (145-146); samples produced before both output formats are known are copied and held, then written in arrival order once the muxer starts (178-207); track order video then audio (197-198).

**Q. Record encoder try-loop** — `RecordEncoder.open`, `record/RecordEncoder.kt:249-264`. `recordEncoderAttempt(..., attempt++, ask)` yields width, height, bit rate until it returns false; first accepted `configure` wins. H.264 only.

**R. AIFF start skip** — `export/AudioTranscoder.kt:133-138`: reads and discards `pcmSamplesBeforeUs` samples.

Outside scope but on the same paths: the track decode loop `TrackAudio.deliverUntil`/`decodeStep` (`engine/scenes/src/main/kotlin/dev/geode/render/offscreen/TrackAudio.kt:75-128`) runs inside every `renderFrame`.

### 4. Every MediaCodec / MediaMuxer / MediaExtractor / EGL14 use, with settings

Video encoders (all: input is a `Surface`, `CONFIGURE_FLAG_ENCODE`, no profile, level, bitrate mode, colour standard/range/transfer, or max-B-frames key is set anywhere):

| Where | mime | size | `KEY_COLOR_FORMAT` | `KEY_BIT_RATE` | `KEY_FRAME_RATE` | `KEY_I_FRAME_INTERVAL` |
|---|---|---|---|---|---|---|
| `export/VideoExporter.kt:539-548`, created 557, configured 559 | `video/avc` or `video/hevc` (`ExportCodec` 39-40; HEVC only if an encoder exists, 44) | `ExportAspect` from `exportFrameSize(shortSide, w, h)` (115); short side 720/1080/2160 | `COLOR_FormatSurface` | attempt's rate: preset 6/12/40 Mbit/s (81-83), capped by `exportCappedVideoBps` (390), rung 1 = 2/3 | attempt's fps (request clamped 24-60; rung 1 halves above 30) | 1 s |
| `export/LoopRender.kt:563-574`; probe 536-537; real 686-688 | same | same | `COLOR_FormatSurface` | as above, capped for the long form's length (522-523) | attempt's fps | 1 s |
| `record/RecordEncoder.kt:250-260` | `video/avc` only | `ask[0] x ask[1]` from `recordEncoderAttempt` | `COLOR_FormatSurface` | `ask[2]` | the prefs fps, unchanged across attempts | 1 s |

Audio encoders (AAC):

| Where | mime | rate / channels | profile | bit rate | other |
|---|---|---|---|---|---|
| `export/AudioTranscoder.kt:94-99`, created 106-108 (AIFF) | `audio/mp4a-latm` | `outputRateHz ?: aacRateFor(source)`; `min(channels, 2)` | `KEY_AAC_PROFILE = AACObjectLC` | 192 000 | `KEY_MAX_INPUT_SIZE = 65536` |
| `export/AudioTranscoder.kt:306-317` (codec source) | same | same, from the decoder's output format (295-303) | AAC-LC | 192 000 | `KEY_MAX_INPUT_SIZE = 65536` |
| `record/RecordEncoder.kt:42, 72-81, 85` | `audio/mp4a-latm` | `RECORD_AUDIO_RATE_HZ` 48 000 / `RECORD_AUDIO_CHANNELS` 2 (`engine/audio-core/.../GeodeNative.kt:1298-1299`) | AAC-LC | `RECORD_AUDIO_BPS` 192 000 (`GeodeNative.kt:1300`) | no max input size |

Decoders: `export/AudioTranscoder.kt:280-282` and `export/LoudnessMeter.kt:118-126`: `createDecoderByType(track mime)`, `configure(trackFormat, null, null, 0)`. Output PCM is read as 16-bit unless `KEY_PCM_ENCODING` says float (`AudioTranscoder` 394-409; `LoudnessMeter` 255-267 refuses anything else).

Extractors: `export/AudioTranscoder.kt:253, 267-274` (`setDataSource(context, uri, null)`, first `audio/` track, `seekTo(startMs*1000, SEEK_TO_PREVIOUS_SYNC)`); `export/LoudnessMeter.kt:104-115` (first `audio/` track, no seek); `export/LoopExtend.kt:511-520` (`setDataSource(path)`, first `video/` track), re-seek per repeat at 485.

Muxers (all `MUXER_OUTPUT_MPEG_4`; none sets orientation hint or location): `export/VideoExporter.kt:377` (fd; tracks video then audio, 450-452 / 481-483); `export/LoopRender.kt:691` (file path; video only, 651); `export/LoopExtend.kt:252-255` (fd; video then audio); `record/RecordEncoder.kt:43` (fd; video then audio, 197-198).

Capability query: `export/VideoExporter.kt:53, 61-67`: `MediaCodecList(REGULAR_CODECS)`, first encoder supporting the mime, `videoCapabilities.getSupportedFrameRatesFor(w, h).upper`, truncated to int, 0 when it throws or is missing.

EGL: `export/EncoderSurface.kt:32-63`: default display; config R8 G8 B8 A8, `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES3_BIT_KHR`, `EGL_RECORDABLE_ANDROID (0x3142) = 1`, no depth/stencil asked; context client version 3, no share context; window surface on the encoder's input `Surface` with no attributes; `eglPresentationTimeANDROID` (71); `eglReleaseThread` on release (84). Users: `export/VideoExporter.kt:398-399`, `export/LoopRender.kt:386-387`, and outside scope `cast/CastStream.kt:191`.

Other: `MediaMetadataRetriever` `METADATA_KEY_DURATION`, `export/LoopRender.kt:217-225`.

Timeouts to reproduce: 10 000 µs wherever a blocking dequeue is used; 0 for the per-frame drains; stall/flush limits of 1 000 idle polls (`VideoExporter` 128, `LoopRender` 749, `AudioTranscoder` 506, `LoudnessMeter` 245).

### 5. Per-frame and per-audio-block Kotlin paths

Per video frame (offline render), driver `ExportRun.launch` → `ui/ExportController` → `VideoExporter.export` → `encodeInto` loop A:
- `OffscreenSceneRenderer.renderFrame` (engine/scenes) → `TrackAudio.deliverUntil` → `decodeStep`
- `WatermarkOverlay.draw` (`export/WatermarkOverlay.kt:59-61`)
- `EncoderSurface.setPresentationTimeNs` / `swapBuffers`
- the drain 442-463 and `writeSample` 570-583
- `AudioFeed.writeUpTo` 600-624
- `onProgress` → `ExportController` → `ExportRun.publish` (`export/ExportRun.kt:77-86`) → native `geode_render_eta_sample` (`core/output/RenderEta`) → a new `State` in the `StateFlow` every frame

Per video frame (loop render), `LoopRender.render` → `renderStops` → `renderStop` → `drawFrames` loop H: `renderFrame`, `SeamStash.frame` (728-733), `WatermarkOverlay.draw`, EGL PTS/swap, `StopWriter.drain`/`writeSample`, `onProgress` → `ExportRun.publish`.

Per encoded video sample (Record), thread `record-encoder`: `pump` → `drain` → `write` (`record/RecordEncoder.kt:153-191`).

Per audio block:
- Export transcode: one pass of loop J or K per decoder buffer, including `downmix` 47-56, `resample` 66-79, `PcmResampler.process` (`export/PcmResampler.kt:37-44`), `feedEncoder` 345-362.
- Loudness: one pass of loop L per decoder buffer → `LoudnessAnalyser.feed` (`export/LoudnessMeter.kt:308-313`).
- Record: `feedAudio` (`record/RecordEncoder.kt:132-150`) once per AAC input buffer (up to 4 096 frames).
- Long-form: `AudioReel.write` per AAC sample, `LoopReader.copyInto` per video sample.

### 6. Duplicated drivers

- **Frame loops over `OffscreenSceneRenderer` + `EncoderSurface`:** `VideoExporter.encodeInto` 434-470 and `LoopRender.drawFrames` 447-462. They use two PTS rules (`FrameClock::ptsNs` exact vs `LoopSeam::ptsNs` truncated step).
- **Video encoder drain + muxer start:** four copies: `VideoExporter` 442-463 and 474-500 (the same block twice in one function), `LoopRender.StopWriter.drain` 608-622, `RecordEncoder.drain` 153-175. Outside scope there are more encoder owners: `cast/CastEncoder.kt:44-45` (AVC + AAC) and `cast/WebRtcLink.kt:85` (Opus).
- **"Muxer started but no sample" guard:** `VideoExporter` 517-529, `LoopRender` 634-643, `RecordEncoder` 212.
- **Video `MediaFormat` builder:** three copies with the same four keys: `VideoExporter` 539-548, `LoopRender` 563-574, `RecordEncoder` 250-256.
- **Encoder try-loop:** three: `VideoExporter.openEncoder` 551-567, `LoopRender.negotiate/accepts` 512-547, `RecordEncoder.open` 249-264.
- **AAC encoder format:** three: `AudioTranscoder` 94-99 and 306-311, `RecordEncoder` 72-81.
- **AAC feed/drain loop:** `AudioTranscoder.transcode` 439-478 and `transcodeAiff` 164-215 are near-copies with different drain timeouts.
- **Audio decode loops (extractor → decoder):** three in the app: `AudioTranscoder.transcode` 368-437, `LoudnessMeter.drain` 152-217, and `TrackAudio.decodeStep` (`engine/scenes/.../offscreen/TrackAudio.kt:101-128`). A single render decodes the same track twice at once (transcode for the AAC, `TrackAudio` for the sound engine) and the finished file a third time (loudness).
- **AAC sample-file reader:** `VideoExporter.AudioFeed` 585-629 and `LoopExtend.AudioReel` 560-624.
- **EGL owner:** `export/EncoderSurface.kt` (render, loop render, cast) and native `core/viz/frame/RecordTap` (creates its own EGL window surface on the stage's context).
- **MediaStore target (insert pending → open → publish/delete):** `VideoExporter.export` 211-256 and `LoopExtend.openTarget/publish/discard` 384-427.
- **Renderer spec construction:** `VideoExporter` 406-425 and `LoopRender.prepareRenderer` 475-492.
- **Constants held on both sides:** `MuxerBudget.AAC_BPS` (`export/MuxerBudget.kt:19`) vs `core/output/EncodePlan.hpp` `kAacBps`; `LoopSpec.MIN_/MAX_/DEFAULT_*` (`export/LoopRender.kt:72-78`) vs `core/output/LoopPlan.hpp` `LoopSpec::k*`; `TimeOfDayDrift.MAX_STOPS` (132) vs `kMaxDriftStops`; `RECORD_AUDIO_*` in `GeodeNative.kt:1298-1300` vs `Recorder::kAudioRateHz/kChannels`; `LoudnessTarget.nativeIndex` vs `loudness::Target`; `ChapterMarkers.LEAD_IN` (231) vs `ChapterSpan::kLeadIn`; the stash budget is in both `core/output/LoopPlan.hpp` (`kSeamStashBudgetBytes`) and `core/viz/frame/LoopSeam.hpp` (`kStashBudgetBytes`), a native-side duplicate.
- **Progress shares:** `VideoExporter.AUDIO_PROGRESS_SPAN = 0.1f` (134) and `LoopExtend.AUDIO_PROGRESS_SPAN = 0.35f` (636), same name, different value.

### 7. Data crossing the JNI boundary

| File | To native | Back | How often |
|---|---|---|---|
| `VideoExporter` | ints/longs (frame index, fps, duration µs, bit rate); `IntArray(codecs)` of fps ceilings | `IntArray` of (codec index, fps, bit rate) triples, at most 4 attempts; `IntArray(2)` size; `Long` PTS ns / time ms | setup once; 2 scalar calls per frame (plus the renderer's) |
| `AudioTranscoder` | `ShortArray` (a whole decoder buffer, copied), `FloatArray` (float PCM), scalars, `LongArray(1)` out-param | new `ShortArray` per call; `Long` µs | 2-4 calls per decoder buffer, each allocating |
| `PcmResampler` | handle, `ShortArray`, frame count | new `ShortArray` | per decoder buffer |
| `LoudnessMeter` | handle, direct `ByteBuffer` + offset + size + float flag (read in place) | `Boolean`; at the end `DoubleArray(5)` | per decoder buffer |
| `LoopRender` | scalars; `AssetManager` (seam pass); handle + frame indices | `LongArray(LOOP_SPEC_LONGS)`; `FloatArray` of (hue, warmth) pairs; `FloatArray(2)`; `Long` PTS ns or -1; `Float` progress; error `String` | 2 calls per frame (`exportSeamFrame`, `exportLoopProgress`) |
| `LoopExtend` | scalars | `LongArray` of (offsetUs, loopIndex) pairs, one per repeat | once |
| `ChapterMarkers` | two `LongArray`s (starts, durations) or one (markers) | `LongArray` of (start, duration, source) triples | once |
| `LoudnessTarget` | target index, lufs, peak | `DoubleArray(9)` + kind int; `DoubleArray(4)` window | once per render; `window` re-queried on every `detail()` call (53) |
| `WatermarkOverlay` | `AssetManager`, direct `ByteBuffer` of ARGB_8888 pixels (tile w × h × 4), placement, corner, length; per frame handle, w, h, time ms | handle; ints/floats for layout; error `String` | pixels once; 1 call per frame |
| `RecordEncoder` | recorder handle, now ns, direct `ByteBuffer` (4 096 frames × 4 bytes), `LongArray(1)` PTS; byte counts | frame count, stop reason, `IntArray(3)` (w, h, bps) | `recorderStopped` + `recorderTakeAudio` every pump pass (about every 5 ms when idle); 2 calls per written sample |

Nothing in `editor/` crosses JNI.

### 8. Saved formats touched

- `ExportQuality` (`HD720`, `FHD1080`, `UHD4K`) and `ExportCodec` (`H264`, `HEVC`) are saved **by enum name**: `data/ExportPrefs.kt:140,148`, `data/RecordPrefs.kt:40`, `data/EditorProjectJson.kt:156`, and a `rememberSaveable` list in `ui/ExportHost.kt:54`. `ExportRatio` is in the same prefs family (not traced line by line).
- `ExportPresets.ALL` is indexed by `ExportDestination.ordinal` (`data/ExportPrefs.kt:76`): the list order is a contract.
- `LoudnessTarget.id` strings `"youtube"`, `"shorts"`, `"as-is"` are persisted (`data/ExportPrefs.kt:27`; read back by `byId`, `export/LoudnessTarget.kt:100`).
- The whole `editor/` model is the on-disk Studio project and template JSON, written by `data/EditorProjectJson.kt` (not in scope): `MarkerOrigin` tags `"manual"`/`"tapped"`/…, `EaseShape` and `MarkerColour`/`OverlayBlend`/`ParamKind` by name, `ClipTransition` (`id`, `durationMs`, `onBeat`), `BeatCut` (`sceneIds`, `everyBeats`), `ProjectKind.Loop` lengths, `ProjectRender`. Defaults used on load: `ClipTransition.DEFAULT_TRANSITION_MS` (`EditorProjectJson.kt:234`), `BeatCut.DEFAULT_BEATS_PER_CUT` (289), `EaseShape.IN_OUT` (420).
- Files: output `Movies/BeatRealms`, mime `video/mp4`, `IS_PENDING` (`export/VideoExporter.kt:214-217`, `export/LoopExtend.kt:399-402`); scratch `geode_aac_*.bin` (`export/AudioTranscoder.kt:109, 284`) and `geode_loop_<n>_*.mp4` (`export/LoopRender.kt:316`) in `cacheDir`. The `.bin` is a headerless run of raw AAC access units; its index lives only in memory.
- Chapter sidecars (`txt`, `vtt`, `ffmeta`; `export/ChapterMarkers.kt:54-61`) are defined but never written in production.
- Android ids: notification channel `"geode-export"`, notification id 4711, action `"dev.geode.export.CANCEL"` (`export/ExportService.kt:121-123`).

### 9. App-side tests of these packages

| Test | Pins |
|---|---|
| `app/src/test/java/dev/geode/export/ChapterMarkersTest.kt` (32) | `ChapterMarkers.markerTitle`: a blank marker name takes the caller's title for its 1-based position; the lead-in takes the caller's lead-in title. No native. |
| `…/export/ExportRunTest.kt` (63) | `ExportRun.requestCancel` cancels the job held process-wide and raises `cancelRequested`; `begin` lowers the flag. |
| `…/export/ExportServiceCancelTest.kt` (29) | `routeCommand`: only `ACTION_CANCEL` calls cancel. |
| `…/export/PcmResamplerTest.kt` (19) | Equal rates are a passthrough that copies `frames * channels` shorts without native code. |
| `…/export/RenderBatchTest.kt` (92) | `RenderScope.tracksFor` (each scope's tracks once, in order; no current track → empty), `isBatch`, `RenderBatch.record/cancelRest/progress`, recording past the end is a no-op. |
| `app/src/androidTest/java/dev/geode/export/WatermarkOverlayTest.kt` (202) | On a real pbuffer GL context: the tiled mark lights every quadrant in landscape and portrait while leaving most of the frame; a corner mark stays in its corner; an end card draws nothing until the end. Saves a PNG as test output. |

There is no app-side test under `record/` or `editor/` package directories. Tests elsewhere that import these packages: `app/src/test/java/dev/geode/data/EditorProjectStoreFoldersTest.kt`, `ExportPresetFillTest.kt`, `RecordPrefsStoreTest.kt`, `data/prefs/PrefsSchemaTest.kt` (not read). No test covers `VideoExporter`, `AudioTranscoder`, `LoopRender`, `LoopExtend`, `LoudnessMeter`, `EncoderSurface` or `RecordEncoder`.

### 10. Surprises

- **Every platform API on these paths has an NDK form.** `MediaCodec`, `MediaMuxer`, `MediaExtractor` map to `AMediaCodec` (including `AMediaCodec_createInputSurface` and `AMediaCodec_signalEndOfInputStream`), `AMediaMuxer` (takes an fd), `AMediaExtractor` (`setDataSourceFd`); EGL is native already. This is from the NDK's public API, not from anything in the repo: the repo has no `AMediaCodec` use to confirm against. The pieces with no NDK form are `MediaCodecList` capabilities, MediaStore/SAF, `Paint` text and the foreground service.
- **The render's audio is transcoded whole before the first frame** into a temp file and an in-memory sample list (`export/VideoExporter.kt:379-387`), because the video bit rate depends on the audio's real length and size (390). A native port must keep that order or the 4 GB cap changes.
- **`export/VideoExporter.kt:469` progress never reaches 0.95 + from frames**: the last 5 % is the EOS drain and `onProgress(1f)` at 507, which is skipped on a file with no audio track index (`audioTrack >= 0` is always true once started, so in practice it runs).
- **The measured loudness gain is never applied** (`export/VideoExporter.kt:314-332` says so): the meter re-decodes the finished file only to produce advice text.
- **`LoudnessMeter` runs after `publishPending`** (`export/VideoExporter.kt:248-249`): the file is already public while it is being re-read.
- **Two PTS rules** for the same kind of file (section 3, loops A and H).
- **`LoopRender` builds a new EGL context, seam stash and renderer per drift stop** (`export/LoopRender.kt:384-398`), so shaders compile again for each of up to 8 stops.
- **A mono clip in a stereo mix fails the whole long-form render after every clip was transcoded** (`export/LoopExtend.kt:334-345`); the message is hard-coded English, as are `LoopExtend` 288-289, `LoudnessMeter` 246-272 and `LoudnessTarget.summary`.
- **`LoopExtend` joins separately encoded AAC clips**, leaving one frame of encoder priming at each boundary (comment 556-558).
- **`RecordEncoder.pump` sleeps 5 ms and polls** (`record/RecordEncoder.kt:123`) rather than blocking in a dequeue; `recorderStopped` crosses JNI on every pass.
- **Dead in production:** `ChapterMarkers.of(markers, …)`, `at`, `text`, `writeTo`, `ChapterFormat`, and `LoopExtend.Result.Saved.chapters`/`plan`/`durationMs` (only `uri` is read, `ui/ExportController.kt:547`); `LoudnessMeter.measure(file, …)` (`export/LoudnessMeter.kt:92-96`); `ProjectTemplate.newProject`/`batch`, `fittedTo`, `withFreshIds`, `Clip.trimmedToEnd`, `SnapMode.resolve/snap`/`magnetise`, `MarkerSet.moveTo`/`nearest`/`rename`/`add`/`remove`/`update`, `KeyframeSheet.track`/`removeTrack`. The editor's snap, fit and template logic is therefore deletable rather than portable, unless a planned feature needs it; no file under `app/src/test` names `fittedTo`, `newProject`, `withFreshIds`, `moveTo`, `magnetise` or `trimmedToEnd` either.
- **`editor/Keyframes.kt` and `ClipTransition.kt` are data only**: their own comments (lines 9-11 and 10-11) say the evaluation was removed; nothing in `export/` reads a keyframe. `VideoExporter.export` still takes `paramsAt: ((Long) -> SceneParams)?` (176), a Kotlin lambda the renderer would call per frame (`engine/scenes/src/main/kotlin/dev/geode/render/offscreen/OffscreenSceneRenderer.kt:109`). No file outside `export/` names `paramsAt`, so no caller passes one: the parameter, and `LoopRender.prepareRenderer`'s drifted wrapper (`export/LoopRender.kt:473-474`), are dead.
- **`ExportRun.publish` allocates a new `State` and runs the ETA on the export thread for every frame** (`export/ExportRun.kt:77-86`); the service throttles to 1 Hz only on the collecting side.
- **`cast/CastStream.kt` reuses `export.EncoderSurface` and `export.PcmResampler`** (lines 191, 310, 376-380): removing either from `export/` breaks Cast unless Cast is ported in the same step.


---

## 04 · cast, wallpaper, audio, audio/dsp, widget, tile, viz, ui/cast, ui/wallpaper

Read in full: 50 Kotlin files, 8,222 lines, under `app/src/main/java/dev/geode/` (cast 12, wallpaper 11, audio 12, audio/dsp 2, widget 5, tile 1, viz 2, ui/cast 4, ui/wallpaper 1). Tests read by name and subject: 6 files, 433 lines, under `app/src/test`; `app/src/androidTest` has no test for these packages.

Paths below are relative to `app/src/main/java/dev/geode/` unless they start with `core/`, `engine/` or `app/`.

Class counts: shell 25, engine 7, mixed 17, duplicate 1, dead 0 whole files (dead members are listed under Surprises).

No native file in `core/` or `app/src/main/cpp` uses `AMediaCodec`, `AMediaMuxer` or `AAudio` input today. The only NDK media and EGL users are `core/audio/output/OboeOutput.cpp` (Oboe output), `core/viz/frame/CastTee.cpp` and `core/viz/frame/RecordTap.cpp` (EGL). So every encoder loop in this scope has rules in `core/` but no native driver.

### 1. Per-file table

#### cast/

| File (lines) | Class | Engine logic: function, lines, what it decides | Thread | Native calls (`GeodeNative.`) | Native owner in `core/` | What must stay Kotlin and why |
|---|---|---|---|---|---|---|
| `cast/CastController.kt` (211) | shell | None. Pro gate at 117, session start/end, `LiveStreamConverter` load at 137-139. `CastFailure` maps each failure to a native link reason (21-41). | main | constants `LINK_REASON_*` only | `core/viz/frame/LinkShare.hpp` (reasons) | Cast SDK (`RemoteCastPlayer`, `CastContext`) is Java only. |
| `cast/CastEncoder.kt` (253) | engine | `init` 68-92: both codec configurations. `drainLoop` 103-114: polls both codecs, sleeps 5 ms when idle. `drain` 117-144: copies each output buffer to a new `ByteArray`, drops audio codec config, taps video to WebRTC. `addTracksWhenReady`/`write` 146-166: holds up to 512 samples until both track formats are known. `CodecPcmFeed.queue` 224-247: backlog shifts and codec input feed (counts are native). | `cast-encoder` (drain), `cast-render` (`queueAudio`) | `castFormat`, `castAudioFeedStart`, `castAudioFeedAdd`, `castAudioFeedTake` | Rules exist: `core/viz/frame/CastAudio.hpp` (`CastAudioFeed`, line 94), `core/viz/frame/FixedRateClock.hpp:75-83` (`CastFormat`). Driver: new `core/cast/CastEncoder` on `AMediaCodec` (+ `AMediaCodec_createInputSurface`). Muxing: new fMP4 writer beside `core/viz/frame/HlsSegments.hpp` (NDK `AMediaMuxer` writes no fragmented MP4). | Nothing required. `MediaCodec` has an NDK form; Media3 `FragmentedMp4Muxer` is the only Java-only part and would be replaced. |
| `cast/CastMirror.kt` (100) | mixed | `directorMoment` 76-79: asks native `stageRun` whether the cast leads Auto, posts to main. Holds the look, `.milk` path, stage flags and streaming flag for two threads. | `cast-render` (directorMoment), `Dispatchers.Default` (follow), main | `stageRun`, `STAGE_CASTING`, `STAGE_CAST_LEADS_AUTO` | `core/viz/StageRun.hpp` | The flow collection from the ViewModel. The look hand-off disappears once the fallback renderer reads a native look. |
| `cast/CastOptionsProvider.kt` (45) | shell | None. | main | none | n/a | Cast SDK `OptionsProvider`. |
| `cast/CastService.kt` (432) | mixed | `waitForStream` 259-276: 250 ms poll, 15 s start timeout. `watchFreeze` 214-229: 1 s poll, ends the cast on a 5 s freeze or a lost LAN address. `checkReceiver` 237-257: hands status to native, maps the verdict to a failure. `keepMuted` 84-98: forces player volume back to 0. `recheckPro` 200-212: 60 s Pro recheck, ends at song end. | main (Handler); `cast-stop` | `linkReceiverHealth`, `LINK_RECEIVER_*`, `LINK_REASON_TRIAL` | `core/viz/frame/LinkShare.hpp` (`receiverHealth`); the freeze and start timeouts are new: `core/cast/StreamWatch` | Foreground service, notification, Wi-Fi lock, wake lock, Media3 `Player.Listener`. |
| `cast/CastStream.kt` (464) | engine | `renderLoop` 189-242: the cast frame loop (clock, tee blit or fallback draw, PTS, swap, audio pump). `fallbackRenderer` 245-260: builds a second `VisualizerRenderer`. `syncLook` 263-284: merges the saved look with the mirrored style, params and `.milk`. `readSavedLook` 151-170: identity-preserving diff of the saved look. `AudioPump.pump` 317-333: hold FIFO shifts, silence pad, feeds AAC and Opus. `opusFor` 336-357: makes the Opus encoder on first peer, frees it after 10 s idle. `takeFromRing` 360-374: sends only the player's ring, else silence. `resamplerFor` 376-384. Constants: `FROZEN_MS` 5 s (419), `FALLBACK_IDLE_NS` 30 s (423). `lanLinkAddress` 433-462 is platform shell. | `cast-render` (not a daemon), `cast-look` executor | `playoutSetBypass`, `castTeeOpen`, `castTeeDraw`, `castTeeAwaitStage`, `castTeeClose`, `frameClockStart`, `frameClockWaitNs`, `frameClockTake`, `frameClockAudioUntilUs`, `stageRun`, `castAudioStart`, `castAudioStep` | `core/viz/frame/CastTee.hpp`, `CastTeeRule.hpp`, `FixedRateClock.hpp`, `CastAudio.hpp`, `core/audio/dsp/PcmResampler.hpp`, `core/audio/ingest/PcmRing.hpp`. Driver: new `core/cast/CastStream` (thread, EGL, loop). | `ConnectivityManager` address lookup (433-462) and the `SharedPreferences` listener. |
| `cast/ComputerLink.kt` (123) | shell | None; `step` 108-122 runs the native state machine and maps phases to UI state. | main | `linkStep`, `LINK_*` constants | `core/viz/frame/LinkShare.hpp` | UI state flow, service start. |
| `cast/LiveHttpServer.kt` (426) | engine | `acceptLoop` 159-169, `admit` 172-199 (native verdict, then per-address cap and queue overflow), `serve`/`answer` 201-254, `feed` 265-312 (long-lived fMP4 feed, 10 ms poll, 5 s write deadline, max-streams 503), `offer` 319-334, `drain` 351-361, `randomToken` 421-424. Kotlin sets pool 8, queue 16, 10 s connection deadline, 6 per address (394-416). | `cast-http`, 8 pool threads, `cast-http-deadline` | `linkAdmit`, `linkRoute`, `linkResponseHead`, `linkBodyLength`, `linkAsset`, `linkShareUrl`, `linkLabel`, `LINK_*`, `WEBRTC_OFFER_MAX` | Rules: `core/viz/frame/LinkShare.hpp`, `LinkPage.hpp`. Driver: new `core/cast/LinkServer` (sockets are allowed only under `core/cast/` by `tools/network-gate.sh`). | Nothing required; POSIX sockets exist. `SecureRandom` token can come from `getrandom`. |
| `cast/LiveSegments.kt` (190) | engine | `drainBoxes` 76-88 and `boxSize` 180-188: top-level MP4 box cutting. `onBox` 90-117: ftyp+moov to init segment, moof+mdat to one segment. `publish` 119-126: native rewrite, window trim. `nextAfter` 151-156: live-edge and fell-behind rule for the browser feed. `playlist` 160-164. | `cast-encoder` (write), HTTP pool threads (read), main (`msSinceLastSegment`, `playlist`) | `hlsRewriteFragment`, `hlsFragmentSeconds`, `hlsPlaylist`, `hlsWindow`, `linkMasterPlaylist`, `HLS_TRACKS` | `core/viz/frame/HlsSegments.hpp` (timing, rewrite, playlist); the box cutter and segment window are new there. | Nothing. |
| `cast/LiveStreamConverter.kt` (57) | shell | None. | main | none | n/a | Cast SDK `MediaInfo`. |
| `cast/RequestLine.kt` (148) | engine | `RequestLineReader.read` 47-69, `readBody` 75-96, `readLine` 98-123: HTTP head parse with 8 KB line and 16 KB header caps under one deadline. `PerAddressLimit` 130-148. | HTTP pool threads | none | new `core/cast/LinkServer` (with `LinkShare.hpp` `bodyLength`) | Nothing. |
| `cast/WebRtcLink.kt` (145) | mixed | `WebRtcLink` 27-74 is a handle with a read/write lock (shell). `OpusFeed` 82-145 is engine: Opus `MediaCodec` configuration 93-98 and `drainLoop` 113-130 (10 ms dequeue timeout, drops codec config, one `ByteArray` per packet). | `cast-opus`; HTTP pool (`answer`); `cast-encoder` (`video`) | `webrtcOpen`, `webrtcAnswer`, `linkRtcStatus`, `webrtcVideoConfig`, `webrtcVideo`, `webrtcAudio`, `webrtcPeers`, `webrtcClose` | `core/cast/WebRtcLink.hpp`, `core/viz/frame/WebRtcRule.hpp`. Opus encode: new in `core/cast/` on `AMediaCodec`. | Nothing required. |

#### wallpaper/

| File (lines) | Class | Engine logic | Thread | Native calls | Native owner | Must stay Kotlin |
|---|---|---|---|---|---|---|
| `wallpaper/DreamCover.kt` (72) | mixed | `onSensorChanged` 55-62: near is `distance < min(3 cm, maximumRange)`; decides the `covered` input of the run rule. | main | none | `core/viz/SurfaceRun.hpp` (takes `covered`); the threshold is new there | `SensorManager` registration (an NDK `ASensorManager` exists, but the listener is lifecycle glue). |
| `wallpaper/DreamOverlay.kt` (162) | shell | None about the visual. Text drift every 60 s and now-playing refresh every 5 s are UI. | main | none | n/a | Views, `TextClock`. |
| `wallpaper/EdgeGlow.kt` (22) | shell | Packs 5 floats in `GEODE_EDGE_GLOW_FLOATS` order (21). | main | none | `core/viz/frame/EdgeGlowPass.hpp`, `EdgeGlowMath.hpp` | Could be deleted with a direct setter. |
| `wallpaper/OutputMixCapture.kt` (126) | mixed | `start` 72-106: capture size (largest allowed, capped to the ring's max write), `SCALING_MODE_AS_PLAYED`, rate fallback 48 kHz, max capture rate. Ring size 32,768 frames (122). `onWaveFormDataCapture` 51-59: writes each 8-bit mono snapshot to the ring. | main looper, about 20 callbacks a second | `pcmRingSetSnapshots`; `SampleRing.write` (bytes), `beginEpoch` | `core/audio/ingest/SnapshotStitch.hpp`, `PcmRing.hpp` | `android.media.audiofx.Visualizer` has no NDK form. |
| `wallpaper/SoundSource.kt` (148) | mixed | `tick` 95-123: 250 ms poll; builds `capturePermitted` (105), carries out native start/stop, picks the ring (115), runs the silence flip. `heardAt` 132-141: only a ring with new frames is judged. `start` 77-85: each start counts as fresh sound. `LEVEL_FRAMES` 1024, `POLL_MS` 250. | main (Handler); `input` read on GL thread | `soundSourceStep`, `soundSourceStartFailed`, `silenceResting`, `SOUND_SOURCE_*`; `SampleRing.heardAt`, `writtenFrames` | `core/sound/SoundSource.hpp`, `core/sound/Silence.hpp`. The poll itself is new: a native ticker or a per-frame check in the renderer. | Starting and stopping `Visualizer`, the permission check, the `mix_refused` pref. |
| `wallpaper/SystemAnimations.kt` (53) | shell | None. | main | none | n/a | `ContentObserver` on a system setting. |
| `wallpaper/VisualizerDreamService.kt` (352) | mixed | `syncRunState` 243-307: gathers conditions, acts on the native mode (pacer start/stop, GL pause/resume, settle burst, dark cover, finish when not entitled). `applyPerformance` 179-188: dream fps choice onto the pacer and surface. `showDark` 313-320. `syncListening` 329-333. Look at 137-139 is the saved look with `gentleFlight()`. | main; GL thread is `VisualizerView`'s | `surfaceEntitled`, `surfaceFpsChoice`, `SURFACE_DEFAULT_FPS` (and the `WallpaperConditions` calls) | `core/viz/SurfaceRun.hpp`, `core/viz/frame/Pacer.hpp`. Host driver: new `core/viz/frame/SurfaceHost` (EGL on `ANativeWindow`, pacer, run-state actions). | `DreamService` lifecycle, the view tree, broadcast receiver, Pro snapshot. |
| `wallpaper/VisualizerWallpaperService.kt` (472) | mixed | `syncRunState` 210-269: same rule application as the dream, plus `trimSpares` on stop. `currentLook` 286-300: lock or home look. `onZoomChanged` 321-327: `COVER_ZOOM` 0.9 decides `covered`. `onTouchEvent` 395-416: pixel to y-up NDC, drops the leaving pointer, caps at `TouchField.MAX_POINTS`. `applyFps` 184-187. Own `GLSurfaceView` subclass 126-134 and `FramePacer` 45-46. `releaseScenesOnGlThread` 449-460 (200 ms bounded wait). | main; `GLSurfaceView` GL thread per engine | `surfaceFpsChoice`, `wallpaperWindow`, `SURFACE_DEFAULT_FPS` | `core/viz/SurfaceRun.hpp`, `core/viz/frame/WallpaperWindow.hpp`, `Pacer.hpp`, `core/viz/TouchTransform` (touch). Host driver: new `core/viz/frame/SurfaceHost`. | `WallpaperService.Engine` callbacks, `KeyguardManager`, `WallpaperManager` flags, receivers. |
| `wallpaper/WallpaperPrefs.kt` (122) | shell | Store. Defaults at 34-53 (`restWhenSilent` true, `pauseInBatterySaver` true, `hearOtherApps` false, both looks FULL, fps `SURFACE_DEFAULT_FPS`). | main | `SURFACE_DEFAULT_FPS` | n/a | `SharedPreferences`. |
| `wallpaper/WallpaperRenderer.kt` (36) | shell | None; forwards the edge glow floats. `GLSurfaceView.Renderer by visual`. | main (setters), GL thread (delegated draw) | via `VisualizerRenderer.setEdgeGlow` | `core/viz/frame/EdgeGlowPass.hpp` | Goes away with a native host. |
| `wallpaper/WallpaperRunState.kt` (83) | shell | Packs observations into flag bits 49-63. Enum order mirrors native codes (6-21). | main | `surfaceRunMode`, `surfaceReason`, `surfaceListening`, `surfaceEntitled`, `surfaceRecheckInMs`, `surfaceSettleMs`, `SURFACE_*` | `core/viz/SurfaceRun.hpp` | Thin packer. |

#### audio/ and audio/dsp/

| File (lines) | Class | Engine logic | Thread | Native calls | Native owner | Must stay Kotlin |
|---|---|---|---|---|---|---|
| `audio/AudioBus.kt` (10) | shell | Holder of the live ring reference. | any | none | `core/audio/ingest/PcmRing.hpp` | Could become a native "current ring" handle. |
| `audio/AudioCapturePump.kt` (121) | engine | `startPump` 40-98: the capture thread. Loop 64-90: blocking read of 1024 frames, write to the ring, native heard-at. Generation guard 54, 75. `stop` 102-112 (500 ms join). | `geode-mic` or `geode-playback-capture` | `captureQuiet`, `captureHeardAt`; `SampleRing.write` (floats or shorts) | `core/audio/ingest/PcmRing.hpp`, `core/sound/Silence.hpp` (`kCapturePeak`, `kCaptureQuietMs`). Driver: new `core/audio/ingest/CaptureInput` (Oboe input for the mic). | For playback capture only: see `PlaybackCapture`. |
| `audio/AudioFxController.kt` (206) | mixed | Clamps: band level 114, bass 0..1000 at 138, loudness 0..1000 at 145, restore 176-190 (a stored preset index beats stored band levels). `bandRange` 102-105. `AudioFxFormat.freqLabel`/`dbLabel` 31-49 are UI text. CSV codec 51-57 is storage. | main | via `NativeDspProcessor` | `core/audio/dsp/DspProcessor.hpp`, `Equalizer.hpp` | Prefs store and UI state. |
| `audio/MicCapture.kt` (98) | mixed | `openRecord` 38-66: rate ladder (device output rate, 48000, 44100, 22050), float then 16-bit, buffer size. `preferredSource` 76-87: UNPROCESSED if the device says so, else VOICE_RECOGNITION. | caller (open), `geode-mic` (loop) | none directly | new: `core/audio/ingest/CaptureInput` on Oboe (`InputPreset::Unprocessed` / `VoiceRecognition`) | Permission check; `AudioManager` property reads. |
| `audio/NowPlayingBridge.kt` (75) | shell | `current` 40-64 picks the playing session of another app (45-49). | main / IO (caller) | none | n/a | `MediaSessionManager`, notification listener. |
| `audio/OboeAudioOutput.kt` (175) | mixed | `plays` 165-173: which output configs Oboe takes. `open` 150-163: 60 s retry window after a failure (133, 152). `write` 42-58: failure becomes a stall, or a recoverable `WriteException` at end of source. `report` 113-120: event bits hard-coded 1, 2, 4 (128-130). | playback thread, per sink buffer | `audioOutOpen`, `audioOutSupports`, `audioOutEvents`, `audioOutWrite`, `audioOutPlay`, `audioOutPause`, `audioOutFlush`, `audioOutStop`, `audioOutRelease`, `audioOutSetVolume`, `audioOutSessionId`, `audioOutBufferFrames`, `audioOutPositionUs`, `audioOutStalled`, `audioOutSetDevice` | `core/audio/output/OboeOutput.hpp`, `OutputFifo.hpp` | The Media3 `AudioOutput` object. The format rule and the retry timer can move behind `audioOutOpen`. |
| `audio/PlaybackCapture.kt` (82) | mixed | `openRecord` 34-75: usages MEDIA, GAME, UNKNOWN; ladder 48000/44100/22050 × stereo/mono × float/16-bit; buffer size. | caller (open), `geode-playback-capture` (loop) | none directly | Format choice: new in `core/audio/ingest/`. | `AudioPlaybackCaptureConfiguration` needs a `MediaProjection` and has no NDK form, so the `AudioRecord` and its read stay Kotlin (a direct `ByteBuffer` read into `pcmRingWriteDirect` keeps the loop thin). |
| `audio/PlaybackCaptureService.kt` (177) | shell | None. | main | none | n/a | Foreground service, `MediaProjection`. |
| `audio/PlayoutSink.kt` (70) | shell | None decided; forwards sink events. Two ring-head reads and one clock call per buffer (30-33). | playback thread | `pcmRingHead`, `playoutBufferHandled`, `playoutPosition`, `playoutPlaying`, `playoutFlush`, `playoutSpeed` | `core/sound/interp/PlayoutClock.hpp` | Media3 `ForwardingAudioSink`. |
| `audio/TapRenderersFactory.kt` (69) | mixed | `buildAudioSink` 46-68: picks Oboe or AudioTrack per output (60-64), builds the chain. | playback thread (`getAudioOutput`), main (build) | via `OboeAudioOutput` | `core/audio/output/OboeOutput.hpp` | Media3 factory wiring. |
| `audio/dsp/MvzAudioProcessorChain.kt` (38) | mixed | Chain order at 19-20: tap, Media3 `SilenceSkippingAudioProcessor`, Media3 `SonicAudioProcessor`, then the native DSP. Speed, pitch and skip-silence are applied by the two Media3 Java processors (24-33). | playback thread | none | Order: `core/audio/dsp/DspChain.hpp`. Time-stretch and silence skipping have no native owner: new `core/audio/dsp/TimeStretch`, `SilenceSkip`. | The `AudioProcessorChain` object. |
| `audio/dsp/NativeDspProcessor.kt` (180) | mixed | `queueInput` 102-116: per block frame maths, one copy, one JNI call. `runs` 169-172: format rule (16-bit or float, 1 or 2 channels). `onConfigure` 88-100. `onFlush` 118-124 and `streamWindow` 132-136: names the stream for the native gain. Constants `BANDS` 10, `MIN_MB` -1500, `MAX_MB` 1500 (161-163). `DspSettings` defaults 14-21. | playback thread; setters from main | `dspCreate`, `dspConfigure`, `dspProcess`, `dspFlush`, `dspBeginStream`, `dspEndStream`, `dspDestroy`, `dspSetEnabled`, `dspSetBand`, `dspSetBassBoost`, `dspSetLoudnessMb`, `dspSetCrossfeed`, `dspSetLimiter`, `dspSetBitPerfect`, `dspBandCenterHz` | `core/audio/dsp/DspProcessor.hpp`, `GainStage.hpp`, `Equalizer.hpp` | Media3 `BaseAudioProcessor` and the `Timeline` lookup. |

#### widget/, tile/, viz/, ui/cast/, ui/wallpaper/

| File (lines) | Class | Engine logic | Thread | Native calls | Native owner | Must stay Kotlin |
|---|---|---|---|---|---|---|
| `widget/NowPlayingWidget.kt` (625) | shell | None about sound or the visual. `layoutFor` 172-178, `fitWithin` 190-199, `formatTime` 202-209 are widget layout and text. | Glance worker, IO, main | none | n/a | Glance. |
| `widget/WidgetPublisher.kt` (213) | shell | 60 s tick while playing and seen (149-158, 211). | main, IO | none | n/a | Media3 listener, Glance update. |
| `widget/WidgetState.kt` (148) | shell | Store. `progress` 39-40. | IO, main | none | n/a | Prefs and a cache file. |
| `widget/WidgetStyles.kt` (80) | shell | None. | main, app scope | none | n/a | Flows between session and widget. |
| `widget/WidgetVisual.kt` (161) | mixed | `glow` 92-110: a generated picture (three radial lights, hue spread, drift per tick, `LIGHTS` table 143-148, constants 150-159). `baseHue` 117-122: artwork hue, else a hash of the style id, else 0.59. `render` 68-82 cache rule and `label` 36-40 are shell. | IO (Glance flow) | via `ArtPalette.extract` | `core/viz/ArtPalette` (hue). The glow is new: a CPU raster in `core/viz/` returning ARGB, if the widget still is counted as a picture decision. | `Bitmap`, asset thumbnail decode. |
| `tile/PlaybackTileService.kt` (231) | shell | None. | main | none | n/a | `TileService`, `MediaController`. |
| `viz/ArtPalette.kt` (19) | shell | None; hands pixels over. | caller (IO) | `artPaletteExtract` | `core/viz/ArtPalette` | Thin wrapper. |
| `viz/BackgroundImage.kt` (119) | mixed | `decodeSampled` 90-108: power-of-two sample size rule (103-104). `decode` 54-82: crop (native rect), filtered scale, then native blur and dim. | `Dispatchers.IO` | `underlayCropRect`, `underlayFinish` | `core/viz/Underlay` | `BitmapFactory`, `ContentResolver`. |
| `ui/cast/CastButtonUi.kt` (45) | shell | None. | main | none | n/a | Compose state mapping. |
| `ui/cast/CastUi.kt` (120) | shell | None. `CastHost` 98-104 feeds `CastMirror` the stage look and `.milk` path. | main | none | n/a | Compose. |
| `ui/cast/ComputerLinkSheet.kt` (333) | shell | None. `LinkQrCode` 296-315 draws the native QR modules with a 4-module quiet zone. | main | `qrEncode`, `QR_MAX_MODULES` | `core/util/QrCode` | Compose. |
| `ui/cast/ComputerLinkUi.kt` (54) | shell | None. | main | `LINK_REASON_*` constants | n/a | String mapping. |
| `ui/wallpaper/WallpaperOptionsActivity.kt` (246) | shell | None; asks native for the still reason (113). | main | via `WallpaperConditions.stillReason` | `core/viz/SurfaceRun.hpp` | Activity, Compose. |

### 2. MediaCodec, EGL, AudioRecord and Visualizer uses, with exact configuration

- H.264 encoder: `cast/CastEncoder.kt:44` (`createEncoderByType(video/avc)`), format `:70-75`: 1280×720, `COLOR_FormatSurface`, bit rate 4,000,000, frame rate 30, I-frame interval 1 s; `configure` `:76`; `createInputSurface` `:77`; `start` `:84`. Width, height, fps, keyframe interval and bit rates come from native `castFormat` (`:192-198`; values in `core/viz/frame/FixedRateClock.hpp:75-83`). No profile, level, bitrate mode or latency key is set.
- AAC encoder: `cast/CastEncoder.kt:45`, format `:79-82`: 48,000 Hz, 2 channels, AAC-LC, 160,000 bps; `configure` `:83`. The rate and channel count are Kotlin constants (`:194-195`).
- Muxer: `cast/CastEncoder.kt:46-50`, Media3 `FragmentedMp4Muxer` into `LiveSegments`, fragment duration 1,000 ms (`:199`).
- Opus encoder: `cast/WebRtcLink.kt:85`, format `:93-96`: 48,000 Hz, 2 channels, bit rate 160,000 (the AAC rate); `configure` `:97`. Drain timeout 10,000 µs (`:142`).
- Cast EGL: `cast/CastStream.kt:191` builds `export/EncoderSurface` on the encoder's input surface. Configuration in `app/src/main/java/dev/geode/export/EncoderSurface.kt:32-67`: default display, RGBA 8/8/8/8, `EGL_OPENGL_ES3_BIT_KHR`, `EGL_RECORDABLE_ANDROID` (0x3142) = 1, context client version 3, no shared context, a window surface with no attributes. `makeCurrent` at `CastStream.kt:199`, `setPresentationTimeNs` + `swapBuffers` at `:229-230`.
- Wallpaper EGL: `wallpaper/VisualizerWallpaperService.kt:152-165`: `GLSurfaceView` subclass on the engine's `SurfaceHolder`, `setEGLContextClientVersion(3)`, `preserveEGLContextOnPause = true`, `RENDERMODE_WHEN_DIRTY`, the framework's default config chooser.
- Dream EGL: none of its own; it uses `engine/scenes` `VisualizerView` (`wallpaper/VisualizerDreamService.kt:133`).
- Microphone: `audio/MicCapture.kt:50-56`: source UNPROCESSED or VOICE_RECOGNITION (`:76-87`), rate ladder device rate, 48000, 44100, 22050 (`:42`, `:94`), `CHANNEL_IN_MONO`, float then 16-bit (`:44`), buffer `max(minBuffer × 4, 1024 × 4)` bytes (`:55`).
- Playback capture: `audio/PlaybackCapture.kt:35-41` matches `USAGE_MEDIA`, `USAGE_GAME`, `USAGE_UNKNOWN`; `:42-44` ladder 48000/44100/22050 × stereo/mono × float/16-bit; buffer `max(minBuffer × 4, 1024 × channels × 4)` (`:61-63`).
- Capture read loop: `audio/AudioCapturePump.kt:64-90`: 1,024 frames per read (`:115`), float reads are `READ_BLOCKING`, 16-bit reads use the default blocking read.
- Output mix: `wallpaper/OutputMixCapture.kt:77` `Visualizer(0)`; `:87-88` capture size = top of the allowed range capped at the ring's max write; `:91` `SCALING_MODE_AS_PLAYED`; `:94` waveform only at `getMaxCaptureRate()`; written as `ENCODING_PCM_8BIT`, 1 channel (`:58`); ring 32,768 frames, 2 channels (`:36`, `:122`) with snapshot stitching on (`:39`).

### 3. Threads started here

| Where | Name | What runs, how often |
|---|---|---|
| `cast/CastStream.kt:127-130` | `cast-render` (not a daemon) | The cast frame loop: 30 frames a second for the life of the stream. |
| `cast/CastStream.kt:99-100` | `cast-look` (single-thread executor) | Reads and diffs the saved look on every write to the `viz` or `general` prefs file (`:101`, `:123-125`). |
| `cast/CastEncoder.kt:87-91` | `cast-encoder` | Drains both codecs and muxes; polls with a 5 ms sleep when idle. |
| `cast/WebRtcLink.kt:100-104` | `cast-opus` | Drains the Opus codec; 10 ms dequeue timeout; lives only while a browser is on WebRTC. |
| `cast/LiveHttpServer.kt:138-142` | `cast-http` | Blocking accept loop. |
| `cast/LiveHttpServer.kt:71-72` | 8 pool threads (default names), queue 16 | One request each; a browser feed holds one for its whole life, polling every 10 ms. |
| `cast/LiveHttpServer.kt:73-75` | `cast-http-deadline` (scheduled, 1 thread) | Closes sockets at the 10 s connection deadline and the 5 s feed-write deadline. |
| `cast/CastService.kt:304` | `cast-stop` | One-shot: joins the render thread and closes the codecs. |
| `cast/CastService.kt:53` | main `Handler` | `waitForStream` every 250 ms, `watchFreeze` every 1 s, `recheckPro` every 60 s. |
| `cast/CastMirror.kt:81` | `CoroutineScope(SupervisorJob() + Dispatchers.Default)` | Collects the stage look and `.milk` path for the life of the process. |
| `cast/CastMirror.kt:68`, `cast/CastOptionsProvider.kt:29` | main `Handler` | One-shot posts. |
| `audio/AudioCapturePump.kt:59-60` | `geode-mic` (`MicCapture.kt:24`) or `geode-playback-capture` (`PlaybackCapture.kt:24`), daemon | Blocking `AudioRecord.read` of 1,024 frames, about 47 blocks a second at 48 kHz. |
| `wallpaper/SoundSource.kt:46`, `:65-71` | main `Handler` | Poll every 250 ms while listening. |
| `wallpaper/DreamOverlay.kt:44` | main `Handler` | Drift every 60 s, now-playing refresh every 5 s. |
| `wallpaper/VisualizerWallpaperService.kt:82`, `VisualizerDreamService.kt:58` | main `Handler` | Still-frame settle end and trial end, one-shot. |
| `wallpaper/VisualizerWallpaperService.kt:152` | `GLSurfaceView` GL thread, one per wallpaper engine | Draws on request from the engine's own `FramePacer` (`:45-46`). |
| `wallpaper/OutputMixCapture.kt:94` | main looper (Visualizer listener) | About 20 captures a second. |
| `widget/WidgetPublisher.kt:46`, `:49`, `:52` | `Dispatchers.IO`, `Dispatchers.Main.immediate`, `Dispatchers.IO.limitedParallelism(1)` | Artwork decode; 60 s tick; serialized state writes and redraws. |
| `widget/NowPlayingWidget.kt:110` | `flowOn(Dispatchers.IO)` | Builds each widget snapshot. |
| `widget/WidgetStyles.kt:78`, `WidgetPublisher.kt:140` | process `appScope` | Widget redraws. |
| `viz/BackgroundImage.kt:62` | `withContext(Dispatchers.IO)` | One decode per pick or export. |
| `audio/PlaybackCaptureService.kt:72` | main `Handler` | Projection callback. |

Not started here but run here: the Media3 playback thread (processors, sink, output) and the Media3 loader thread (`AiffExtractor`).

### 4. Per-frame and per-audio-block Kotlin paths

Per cast frame (30 fps), `cast/CastStream.kt:207-232` on `cast-render`:
1. `frameClockWaitNs` + `LockSupport.parkNanos` (`:208`), `frameClockTake` (`:209`).
2. `castTeeDraw` (`:211`), `stageRun` (`:212`), `castTeeAwaitStage` when the tee gave nothing (`:213`).
3. Fallback only: `syncLook` (`:221`) then `VisualizerRenderer.onDrawFrame` (`:222`).
4. `EncoderSurface.setPresentationTimeNs` + `swapBuffers` (`:229-230`).
5. `AudioPump.pump` (`:231`, body `:317-333`): `takeFromRing` → `SoundCursor.take` → `PcmResampler.process` (`:370-373`); `castAudioStep` (`:320`); four `System.arraycopy` shifts of the hold; `CastEncoder.queueAudio` → `CodecPcmFeed.queue` (`cast/CastEncoder.kt:224-247`: `castAudioFeedAdd`, then `castAudioFeedTake` per free input buffer); the same again through `OpusFeed.queue` while a browser is connected (`:332`).
That is about 8 JNI calls per frame from the stage's tee and about 12 or more with the fallback and Opus, plus `System.nanoTime` four times.

Per encoded buffer, on `cast-encoder` (`cast/CastEncoder.kt:117-144`): one `ByteArray` allocation and copy, `FragmentedMp4Muxer.writeSampleData` (`:173`), which calls `LiveSegments.write` (`cast/LiveSegments.kt:66-73`), which copies the whole pending buffer on each call (`drainBoxes` `:77`, `:86-87`); for video also `WebRtcLink.video` → `webrtcVideo` (`cast/WebRtcLink.kt:44-53`). Once per fragment (1 a second): `hlsRewriteFragment` and `hlsFragmentSeconds` (`cast/LiveSegments.kt:121-123`).

Per Opus packet, on `cast-opus` (`cast/WebRtcLink.kt:117-127`): one `ByteArray` and `webrtcAudio`.

Per playback audio block, on the Media3 playback thread, in chain order (`audio/dsp/MvzAudioProcessorChain.kt:19-20`, built at `audio/TapRenderersFactory.kt:44`, `:56`):
1. `TeeAudioProcessor` → the PCM tap (`engine/audio-android/.../PcmTap.kt`, outside this scope).
2. Media3 `SilenceSkippingAudioProcessor` (Java DSP).
3. Media3 `SonicAudioProcessor` (Java DSP: speed and pitch).
4. `NativeDspProcessor.queueInput` (`audio/dsp/NativeDspProcessor.kt:102-116`): one buffer copy, `dspProcess`.
5. `PlayoutSink.handleBuffer` (`audio/PlayoutSink.kt:25-35`): `pcmRingHead` twice, `playoutBufferHandled`.
6. `OboeAudioOutput.write` (`audio/OboeAudioOutput.kt:42-58`): `audioOutEvents`, `audioOutWrite` (only when the native output is on).
Also on that thread, per position poll: `PlayoutSink.getCurrentPositionUs` → `playoutPosition` (`audio/PlayoutSink.kt:37-43`); `OboeAudioOutput.getPositionUs`, `isStalled` (`:83`, `:87`).

Per captured block, on the capture thread (`audio/AudioCapturePump.kt:64-90`): `AudioRecord.read`, `SampleRing.write` (one JNI call with an array), `captureHeardAt`.

Per Visualizer capture, on main (`wallpaper/OutputMixCapture.kt:51-59`): `SampleRing.write` of the 8-bit bytes.

Per wallpaper or dream frame: no Kotlin in this scope beyond the delegation `WallpaperRenderer` → `VisualizerRenderer.onDrawFrame` (`wallpaper/WallpaperRenderer.kt:15`); the frame request comes from `FramePacer` (`engine/scenes`). Per wallpaper touch event, on main: `onTouchEvent` (`wallpaper/VisualizerWallpaperService.kt:395-416`).

### 5. Duplicated drivers

Wallpaper service against dream service, line by line. The two files carry the same run-state machine twice:

| Piece | Wallpaper (`wallpaper/VisualizerWallpaperService.kt`) | Dream (`wallpaper/VisualizerDreamService.kt`) | Same? |
|---|---|---|---|
| `settings` field | 67 | 66 | Same; the dream fills only `hearOtherApps` (132). |
| `SoundSource(...) { syncRunState() }` | 76 | 68 | Same. |
| `listening` + `syncListening` | 79, 275-279 | 94, 329-333 | Identical bodies. |
| `SystemAnimations` | 70 | 90 | Same. |
| Performance prefs listener | 63-66 | 81-84 | Same key test, different target. |
| Power receiver | 99-105, registered 166-174 with `USER_PRESENT` too | 110-116, registered 163-169 | Same body; the wallpaper adds unlock. |
| `runMode` field, `handler` | 81-82 | 96, 58 | Same. |
| `endStillSettle` | 85 | 104-108 (adds a `settling` flag) | Near copy. |
| `trialEnded` | 88 | 207 | Same. |
| Look restore | 190-195 (`pushProcessWide`, `current()`) | 137-139 (adds `gentleFlight()`) | Near copy. |
| fps choice | `applyFps` 184-187 | `applyPerformance` 179-188 | Same native snap, own pacer vs the view's. |
| Build `WallpaperConditions` | 218-229 | 246-258 (`surfaceAvailable = true`, `preview = false`, `darkWhenSilent = true`) | Same shape. |
| Trial recheck scheduling | 230-231 | 265-266 | Identical. |
| `runMode = conditions.runMode(now)`, cancel settle | 234-236 | 270-275 | Same. |
| ANIMATED | 238-241 (`onResume` every time) | 280-283 (`resumeGl` guarded by `glPaused`) | Differ in the guard. |
| STILL | 242-256 (settle 0: one `requestRender`, pacer not stopped here) | 284-298 (settle 0: `pacer.stop()` then `requestRender`, unless settling) | Differ. |
| STOPPED | 258-267 (stop pacer, `trimSpares`, `onPause`) | 299-305 (stop pacer, `onPause` once) | The wallpaper trims spares; the dream does not. |
| DARK | Treated as STOPPED (258) | 279 (pacer stop, GL kept) + `showDark` 313-320 | Dream only. |
| Not entitled | A native STILL reason | `finish()` at 122-125 and 261-264 | Differ. |
| `covered` source | `onZoomChanged` ≥ 0.9 (321-327) | `DreamCover` proximity (87) | Different inputs to the same flag. |
| GL host | Own `GLSurfaceView` subclass and `FramePacer` (126-134, 45-46) | `VisualizerView` and its pacer (133, 182) | Two hosts. |
| Teardown | `releaseScenesOnGlThread` 449-460 | `VisualizerView.onDetachedFromWindow` (226-227) | Two paths. |

Other drivers that exist more than once:
- Frame loops: the cast loop (`cast/CastStream.kt:207-232`), the wallpaper's `FramePacer` + `GLSurfaceView`, the dream's and the stage's `VisualizerView`, and the export and loop-render loops (`app/.../export/VideoExporter.kt:398`, `export/LoopRender.kt:386` both build an `EncoderSurface`).
- EGL owners: `export/EncoderSurface` (cast, export, loop render), two `GLSurfaceView` hosts (wallpaper subclass, `VisualizerView`), native `CastTee` and `RecordTap`.
- A second whole renderer: the cast fallback `VisualizerRenderer` (`cast/CastStream.kt:245-260`) beside the stage's.
- Encoder drain loops (`dequeueOutputBuffer`): `cast/CastEncoder.kt:122`, `cast/WebRtcLink.kt:117`, and outside the scope `export/VideoExporter.kt`, `export/LoopRender.kt`, `export/AudioTranscoder.kt`, `export/LoudnessMeter.kt`, `record/RecordEncoder.kt`, `engine/scenes/.../render/offscreen/TrackAudio.kt`.
- PCM-to-codec feeds: `CodecPcmFeed` (cast AAC and Opus) beside the export's and Record's own feeds.
- Look pushes: wallpaper `restoreLook` (190-195), dream (137-139), cast `syncLook` + `readSavedLook` (`cast/CastStream.kt:151-170`, `263-284`), all building a `LookStore` by hand.
- Sound feeds: `renderer.soundInput = { sound.input }` in the wallpaper (148) and dream (142); `{ AudioBus.soundInput }` in the cast fallback (`cast/CastStream.kt:247`); the cast `AudioPump` has its own `SoundCursor` on the same ring (`:366`).
- Capture format ladders: `audio/MicCapture.kt:42-44` and `audio/PlaybackCapture.kt:42-44` repeat the rate and encoding ladder.
- Silence and freeze watchdogs: `wallpaper/SoundSource.kt` (250 ms), `cast/CastService.kt` `waitForStream`/`watchFreeze` (250 ms / 1 s), and `ui/CaptureController.kt:243-270` (500 ms, outside the scope).
- EQ band constants: `audio/dsp/NativeDspProcessor.kt:161-163` and the native equalizer.
- Transport through a `MediaController`: `widget/NowPlayingWidget.kt:544-584` and `tile/PlaybackTileService.kt:97-142` (shell, same rule twice).

### 6. Data crossing the JNI boundary

- Cast clock: a `LongArray(FRAME_CLOCK_STATE)` state passed on every call, `Long` times back; 3 to 4 calls a frame (`cast/CastStream.kt:205-231`).
- Cast tee: a `Long` handle, `nowNs`, width, height → `Boolean` drew; once a frame (`:211`).
- Cast audio step: `LongArray` state, gained frame count, cut in µs → a 5-long step (evict, skip, keep, take, pad); once a frame (`:320`, layout `:397-404`). The PCM itself stays in Kotlin `ShortArray`s (hold of half a second, block of one frame's audio).
- Ring read for the cast: `SoundCursor.take` returns a frame count; PCM is read from the cursor's 16-bit buffer; `PcmResampler.process` returns a `ShortArray` (`:370-373`).
- Codec feed: `LongArray(CAST_AUDIO_FEED_STATE)`, frames in → drop count; per input buffer, capacity in frames → frames to take and a PTS in a 1-long array (`cast/CastEncoder.kt:220-247`).
- HLS: init segment `ByteArray`, moof offset, raw fragment `ByteArray` (about 0.5 MB at 4 Mbit/s), `LongArray(HLS_TRACKS)` decode times → a new rewritten `ByteArray`; once a second. Playlist: first sequence, `DoubleArray` durations, three names → `String` (`cast/LiveSegments.kt:121-123`, `163`).
- HTTP rules: method and target strings and the token → status and a 2-long route (`cast/LiveHttpServer.kt:244-245`); response head as a `String` per response (`:289`, `:385`); admit takes five ints (`:174`); assets come back as `ByteArray` (`:369`).
- WebRTC: offer `String` → answer `String` and a status int; each H.264 buffer as `ByteArray`, size, PTS, key flag (30 a second); each Opus packet (50 a second) (`cast/WebRtcLink.kt:35-59`).
- Surface run rule: flag bits `Int`, `animateUntilMs`, `nowMs` → mode, reason, listening, recheck delay, settle ms; on every state change (`wallpaper/WallpaperRunState.kt:65-83`).
- Sound source: `LongArray(SOUND_SOURCE_STATE_LONGS)`, now, ring present, ring handle, written frames, two booleans → action bits; 4 times a second (`wallpaper/SoundSource.kt:98-107`).
- Wallpaper window: four floats, a 6-float offsets array → a 4-float window (`wallpaper/VisualizerWallpaperService.kt:382`); edge glow as 5 floats (`wallpaper/EdgeGlow.kt:21`); touch as up to `MAX_POINTS × 2` floats per event (`:415`).
- Playback DSP: a direct `ByteBuffer` of one sink block, frames, channels, float flag; per block (`audio/dsp/NativeDspProcessor.kt:115`). Stream begin: URI `String`, position offset, duration (`:123`). Setters: 10 band ints, 2 ints, 3 booleans (`:151-157`).
- Playout clock: handle, media time, ring head before, frames written; per block (`audio/PlayoutSink.kt:30-33`).
- Oboe output: a `ByteBuffer` with position and remaining → bytes taken; event bits `Int`; per sink buffer (`audio/OboeAudioOutput.kt:47-48`). Open: rate, channel count, encoding, buffer bytes, session id → handle (`:155-161`).
- Capture: `FloatArray` or `ShortArray` of 1,024 × channels, frames, channels, rate → peak `Float`; then peak, now, last heard → heard-at ms (`audio/AudioCapturePump.kt:79-85`). Output mix: `ByteArray` of up to the capture size, 8-bit, mono, rate (`wallpaper/OutputMixCapture.kt:58`).
- Art palette: `IntArray` of 32 × 32 ARGB → 3 floats (`viz/ArtPalette.kt:17-18`, `widget/WidgetVisual.kt:125-131`). Underlay: `IntArray` of render width × height ARGB processed in place (`viz/BackgroundImage.kt:80`); crop rect as 4 ints (`:116`). QR: URL → side length and a module `ByteArray` (`ui/cast/ComputerLinkSheet.kt:297-298`).

### 7. Saved formats touched

- Wallpaper prefs file `GeodePrefsFiles.WALLPAPER` (`wallpaper/WallpaperPrefs.kt:104-120`): `rest_when_silent`, `pause_in_battery_saver`, `hear_other_apps` (booleans), `home_look`, `lock_look` (strings holding the `WallpaperLook` enum name: `FULL`, `EDGE`), `fps` (int, snapped natively on read), `mix_refused` (boolean, device only). All but `mix_refused` ride in a backup.
- Audio FX prefs file `GeodePrefsFiles.AUDIO_FX` (`audio/AudioFxController.kt:193-204`): `enabled` (boolean), `band_levels` (CSV of ints in millibels, low band first), `preset` (int index into `AudioFxPresets.all`, -1 for custom), `bass`, `loudness` (ints 0..1000). The preset index is positional, so the order of `core/audio/dsp/EqPresets.hpp` is a saved format.
- Widget prefs file `GeodePrefsFiles.WIDGET` (`widget/WidgetState.kt:46-65`): `title`, `artist`, `art`, `style_id` (strings), `playing`, `position_ms`, `duration_ms`, `art_hue`, `art_span`, `tick`. All are cache keys. Cache file `widget_art.png` (`:56`).
- Enum ordinals that mirror native codes: `WallpaperRunMode` and `WallpaperStillReason` (`wallpaper/WallpaperRunState.kt:6-21`, indexed by the native int at `:65`, `:68`). Not persisted. `WallpaperLook.ordinal` is used as a UI index only (`ui/wallpaper/WallpaperOptionsActivity.kt:233-234`).
- Read, not owned: the saved look through `LookStore` (cast, wallpaper, dream), `PerformanceStore.KEYS`, `BehaviourPrefs` dream keys (`wallpaper/VisualizerDreamService.kt:130-151`).
- Wire names shared with native: `live.m3u8`, `init.mp4`, `seg`, `.m4s` (`cast/LiveSegments.kt:168-171`, matching `core/viz/frame/LinkShare.hpp`). Intent actions `dev.geode.cast.*` and `dev.geode.STOP_CAPTURE`, notification channels `cast` and `geode-capture` (`cast/CastService.kt:365-370`, `audio/PlaybackCaptureService.kt:135-139`).
- `UnderlayBlend.ordinal` is handed on at `viz/BackgroundImage.kt:51`.

### 8. Tests for these packages (all JVM, under `app/src/test/java/dev/geode/`)

- `cast/RequestLineTest.kt` (155): pins `RequestLineReader` over real loopback sockets (a normal line, headers to the blank line, a body to its length, a short body dropped at the deadline, header and line caps, the line exactly at the cap, a slow client and a silent client cut at the deadline, a closed client) and `PerAddressLimit`. A native server port loses this test unless it is rewritten as a host test.
- `audio/dsp/NativeDspProcessorTest.kt` (49): pins `NativeDspProcessor.runs` (mono/stereo 16-bit and float run; more channels and other encodings step aside) and that a six-channel configure answers inactive instead of throwing.
- `widget/WidgetVisualTest.kt` (76): pins the picture cache rule (position change keeps it, a tick moves the glow, another style or palette redraws, a thumbnail is kept) and `ArtKey`.
- `widget/NowPlayingWidgetTest.kt` (32): pins `layoutFor` breakpoints and `fitWithin`.
- `ui/cast/CastButtonUiTest.kt` (54): pins `castButtonUi` and `castMessage` per state and failure.
- `ui/cast/ComputerLinkUiTest.kt` (67): pins the link row, icon and sheet strings, the ended messages, and that each `CastFailure.linkReason` gives the same message.
- `data/prefs/PrefsSchemaTest.kt:147-186` (outside the scope) pins the pref keys of `AudioFxController`, `WallpaperPrefsStore` and `WidgetState`.
- No Kotlin test covers `CastStream`, `CastEncoder`, `LiveSegments`, `LiveHttpServer`, `OpusFeed`, `SoundSource`, `OutputMixCapture`, either service's `syncRunState`, `AudioCapturePump`, `MicCapture`, `PlaybackCapture`, `OboeAudioOutput`, `PlayoutSink`, `AiffExtractor` or `AudioFxController`'s clamps.
- The native rules they call are host-tested in `core/tests/`: `cast_audio_test`, `cast_clock_test`, `cast_tee_test`, `cast_same_visual_test`, `hls_segments_test`, `link_share_test`, `webrtc_rule_test`, `surface_run_test`, `surface_motion_test`, `sound_source_test`, `snapshot_stitch_test`, `wallpaper_window_test`, `edge_glow_test`, `edge_glow_pass_test`, `playout_test`, `output_test`, `output_fifo_test`, `dsp_handoff_test`, `art_palette_test`, `underlay_test`, `qr_code_test`.

### 9. Surprises

- Speed, pitch and skip-silence are done by Media3's Java processors on the playback thread (`audio/dsp/MvzAudioProcessorChain.kt:16-33`). They are audio DSP outside `core/` and sit between the tap and the native DSP, so the tap hears audio before time-stretch and the native chain after it.
- `audio/AiffExtractor.kt` and `core/audio/ingest/AiffReader` read one header scan (`AiffHeaderScan`), yet playback still plays fewer layouts than an export reads: the extractor refuses 8-bit and little-endian 24/32-bit, which the reader takes.
- `DspSettings.crossfeed` has no writer anywhere in the app (the only `crossfeed` mention outside `audio/dsp` is a comment in `playback/BitPerfectOutput.kt:36`), so crossfeed is always off; `limiter` is always its default, on (`audio/dsp/NativeDspProcessor.kt:19-20`).
- Dead members in `audio/AudioFxController.kt`: `sessionId` is written by `attach`/`release` (87-93) and never read; `available`, `attached`, `bandCount`'s outside use, `bandRange` and `presetNames` have no caller outside the file (Grep); `AudioFxFormat.freqLabel` is used only at `:163`.
- Dead members elsewhere: `AudioCapturePump.sampleRateHz` has no reader outside the pump classes (Grep); `NativeDspProcessor.MAX_CHANNELS` is used only at `:172`; `LiveHttpServer.port` and `pageUrl` only inside the file; `TapRenderersFactory.audioProcessorChain` only at `:56`.
- `OboeAudioOutput` hard-codes the native event bits 1, 2, 4 (`:128-130`) and `WebRtcLink` hard-codes `REFUSED_FAILED = -6` (`cast/WebRtcLink.kt:72`) instead of reading `GeodeNative` constants.
- The cast render thread is the only thread here that is not a daemon (`cast/CastStream.kt:127-130`), and `stop()` joins it for only 2 s (`:135`, `:420`), after which the server and WebRTC handle are closed while the loop may still run.
- `LiveSegments.write` copies the whole pending buffer on every muxer write (`cast/LiveSegments.kt:77`, `:86-87`), so a 0.5 MB fragment written in small pieces is copied many times on the drain thread.
- `CastEncoder.drain` drops a sample silently when 512 are already held (`cast/CastEncoder.kt:162`), and a muxer write failure is only logged (`:173-174`); the freeze watchdog is the only thing that ends such a stream.
- The Opus bit rate is the AAC bit rate, 160,000 (`cast/WebRtcLink.kt:95`); there is no separate Opus constant in native `CastFormat`.
- The cast decides in Kotlin that only the player's ring is sent and that mic or other-app audio becomes silence (`cast/CastStream.kt:360-362`). That is a privacy rule with no native copy and no test.
- `CastService` decides stream health in Kotlin with three timers (15 s start, 5 s freeze, 1 s poll; `cast/CastService.kt:214-276`, `372-376`), while the TV health rule beside it is native.
- The wallpaper's STILL branch with settle 0 does not stop the pacer (`wallpaper/VisualizerWallpaperService.kt:253-255`), where the dream does (`VisualizerDreamService.kt:294-297`); the wallpaper relies on `endStillSettle` having run. The wallpaper also calls `glView.onResume()` on every restate without a guard (`:239`, `:248`), where the dream tracks `glPaused`.
- The dream never calls `trimSpares` on stop; the wallpaper does (`:265`).
- `DreamCover.stop()` clears `covered` without calling `onChanged` (`wallpaper/DreamCover.kt:48-53`); the dream restates right after at `VisualizerDreamService.kt:200-202`, so it holds only by call order.
- `WidgetVisual.glow` is a small procedural visual written in Kotlin with its own colour rule (hue from a `String.hashCode()` of the style id, `widget/WidgetVisual.kt:117-122`), separate from the palette code in `core/viz`.
- `widget/NowPlayingWidget.kt:615-625` copies six theme colours by hand because the layer gate keeps `widget/` from naming `ui/`.
- `tools/engine-kotlin-gate.sh:138` watches `audio/dsp/` only; `cast/`, `wallpaper/` and the rest of `audio/` (capture loops, the Oboe output shell, the AIFF extractor) can grow without tripping it.
- `ui/cast/CastUi.kt:98-104` is the only feeder of the cast fallback's look: the UI layer pushes engine state (style id, params, `.milk` path) to an off-main render thread through `CastMirror`'s volatile fields.

### 10. Could not read or verify

- Everything in scope was read in full. Nothing was compiled or run.
- Graphify was not used; callers were checked with Grep over `app/src/main` and `engine/`.
- Native headers were read at their tops only (`CastAudio`, `CastTee`, `LinkShare`, `Recorder`, `RecordTap`, `AiffReader`, `WebRtcLink`); the `.cpp` bodies were not read, so claims about what native does come from those header comments and the Kotlin call sites.
- The AIFF divide-by-zero and unbounded-skip notes are from reading only.
- `FramePacer`, `VisualizerView`, `VisualizerRenderer`, `PcmTap`, `SampleRing`, `SoundCursor`, `export/EncoderSurface` and `export/PcmResampler` are outside this scope and were only looked at where this scope calls them.


---

## 05 · `app/src/main/java/dev/geode/data/` (+ `data/backup/`, `data/prefs/`)

Read in full: 73 Kotlin files, 10,897 lines (`data/` 66 files, `data/backup/` 5, `data/prefs/` 2). Paths below are relative to `app/src/main/java/dev/geode/` unless they start with `app/`, `core/` or `engine/`. Tests: 34 files / 3,091 lines under `app/src/test/java/dev/geode/data/**`; for those I read the header of `PresetGoldenTest.kt` and every test name, not every body (see "Not verified" at the end).

Counts: shell 49, mixed 21, engine 0 whole files, duplicate 0 whole files (1 duplicated table inside a mixed file), dead 0 whole files (3 dead functions).

### 1. Per-file table

"Thread": `main` = called from UI/ViewModel code; `IO` = `Dispatchers.IO` or the repository's `io`; `caller` = no thread of its own, runs where it is called (callers named when traced).

| File (lines) | Class | Engine logic: function, lines, what it decides | Thread | Native calls | Native owner in core/ | What must stay Kotlin |
|---|---|---|---|---|---|---|
| `data/AppState.kt` (98) | shell | none (tour / safety flags, legacy-key migration `:62-82`) | IO via `PrefsSettingsRepository` | none | n/a | SharedPreferences |
| `data/AtomicWrite.kt` (54) | shell | none (temp + fsync + rename, `.tmp` / `.corrupt` suffixes `:9-11`) | caller | none | n/a | app file I/O (could be NDK, but every store uses it) |
| `data/AutoDirectorStore.kt` (192) | mixed | `newPlay` `:108-114` makes the per-play seed (`Random.nextLong`, also `:61`) and sends the track boundary to every sound engine; `push` `:140-148` computes the director amount through `pro/AutoDirectorPolicy.amount(on, gate, positionMs, playing)` and dedupes; defaults `DEFAULT_ENABLED = true`, `DEFAULT_REACH = 0` `:152-153` | main (settings, `playback/AutoDirectorFeed`); the `init` collector runs in the container scope `:70-78` | `directorKeepSet` `:88`, `directorReachSet` `:95`, `soundTrackBoundary` `:112`, `directorSet` `:147` | `core/viz/Director*`, `core/viz/DirectorReach.*`, `core/sound/PlayerSignals` (named in the KDoc `:104`); seed + free-taste amount: new: one native "new play" call that takes gate, position and playing | prefs read/write of 2 keys; the Pro gate value comes from billing |
| `data/AutoVisualsPrefsStore.kt` (113) | mixed | `INTERVAL_SEC = 5..300` `:57` and the clamps `:32`, `:38` bound the style-change timer; `applyTo` forces `randomEnabled = false` `:31` and switches the visual playlist off when it is empty `:29`; `entriesFromJson` resolves scene ids through `Catalogue.resolve` `:103` | main (called from `VizStateStore.restore`) | none direct (`Catalogue.resolve` is `:engine:scenes`) | new: timer range and scene-id aliasing in `core/viz/Catalogue.*` | prefs I/O |
| `data/BackgroundPrefsStore.kt` (70) | mixed (thin) | Kotlin defaults of the underlay params: blend `SCREEN`, amount `0.6`, blur `0`, dim `0` `:17-23`. Clamps are native | caller (main) | `underlayClampAmount` `:35`, `underlayClampBlur` `:36`, `underlayClampDim` `:37`, `underlayBlurMax` `:54` | `core/viz/Underlay.*` (defaults should move there) | prefs I/O; image URI |
| `data/BehaviourPrefs.kt` (279) | shell | none about a param/sound/picture. UI rules only: `NotificationButton.toggled/ordered` `:40-47`, default gesture map `:156-168`, `dreamFps` default = `GeodeNative.SURFACE_DEFAULT_FPS` `:144` | IO via repository; `current()` on main in the dream service | constant `GeodeNative.SURFACE_DEFAULT_FPS` only | n/a | prefs, gesture → UI action map |
| `data/BootAnimationStore.kt` (24) | shell | none | caller | none | n/a | prefs |
| `data/BuiltInPresets.kt` (60) | mixed | `parse` `:29-50` and `params` `:52-59` read the native presets table and lay each look over Kotlin `SceneParams()` defaults through `SceneParamsCodec.pack/unpack` and `ParamSchema`; `isBuiltIn` `:21` decides "built-in" by the name containing `" · "` | first reader's thread (lazy `ALL` `:19`; first read is `VizStateStore.restore`, main) | `cataloguePresetsText` `:19` | `core/viz/CataloguePresets.*` (already owns the data; the Kotlin overlay on `SceneParams` defaults is the part to remove) | nothing; the UI only needs names + scene ids |
| `data/ByteBudgetCache.kt` (104) | shell | none (LRU byte-budget cache for artwork; used by `ui/ArtworkCache.kt`) | caller coroutine | none | n/a | Bitmap cache |
| `data/CustomizeGroupsStore.kt` (88) | shell | none (open groups by `CATEGORY/section` path; content of a group is `core/viz/CustomizeMap`) | container scope | none | n/a | prefs |
| `data/DeviceTrackQuery.kt` (103) | shell | none (MediaStore query, min-length filter) | IO | none | n/a | MediaStore |
| `data/DocumentUris.kt` (25) | shell | none | caller | none | n/a | ContentResolver |
| `data/EditorProjectJson.kt` (461) | mixed (format codec) | JSON codec of the Studio model in `editor/` (timeline, lanes, clips, keyframes, interpolation, markers). Decides on read: scene-id aliasing `Catalogue.resolve` `:263`; fps must be in `EXPORT_FPS_OPTIONS` `:153`; `BeatCut.everyBeats ≥ 1` `:289`; keys sorted by time `:353`; defaults taken from `editor/` (`ProjectKind.DEFAULT_LOOP_*` `:132-133`, `ClipTransition.DEFAULT_TRANSITION_MS` `:234`, `BeatCut.DEFAULT_BEATS_PER_CUT` `:289`). `toJson`/`fromJson` `:50-52` have no production caller | IO (`EditorProjectStore`, `@WorkerThread`) | none | new: `core/output` or a `core/studio` project codec if the timeline/keyframe model is ported (`editor/` is another scanner's scope) | file I/O only |
| `data/EditorProjectStore.kt` (124) | shell | none (folders, quarantine; deletes legacy dirs `editor`, `takes`, `project-takes` on construction `:118-122`) | IO | none | n/a | filesDir |
| `data/ExportPrefs.kt` (193) | mixed | `EXPORT_FPS_OPTIONS = [24,25,30,50,60]` `:96` (comment `:94` says it mirrors `core/output/EncoderLadder`'s 24..60 clamp: a Kotlin copy of a native range); `DEFAULT_EXPORT_FPS = 30` `:31`; `ExportDefaults` defaults `:21-29`; `ExportDestination.preset` picks the platform preset by enum ordinal into `export/ExportPresets.ALL` `:76`; `factoryDefaults` `:79`, `filledFrom` `:91-92` | caller (main) | none | `core/output/EncoderLadder.hpp`, `core/output/EncodePlan.*` (fps list, per-destination presets) | prefs I/O; `SaveTarget` (SAF tree URI) |
| `data/FavouritesRepository.kt` (80) | shell | none | scope + IO | none | n/a | prefs |
| `data/FavouritesStore.kt` (51) | shell | none | caller | none | n/a | prefs |
| `data/FontColorChoice.kt` (25) | shell | none (UI font colours, ARGB list `:13-23`) | n/a | none | n/a | UI |
| `data/GeneralSettings.kt` (217) | shell | none (UI look; clamps of UI values `:179-183`) | IO via repository | none | n/a | prefs |
| `data/GeodePrefsFiles.kt` (66) | shell | none (registry of the 12 prefs file names `:42-58`) | caller | none | n/a | SharedPreferences |
| `data/HistoryStore.kt` (172) | shell | none (play history; keeps 200 most recent ∪ 200 most played `:137-146`) | constructor reads the file on the constructing thread `:28-30`; writes on `geode-history` | none | n/a | filesDir |
| `data/LfoStore.kt` (208) | mixed | **Kotlin defaults and rules for LFO and envelope slots.** `fromJson` `:153-166`: defaults source `LFO`, target `NONE`, wave `SINE`, depth `0.3`, polarity `BIPOLAR`, curve `LINEAR`, `beats` only if in `LfoConfig.BEAT_CHOICES` `:163`. `adsrFromJson` `:116-137`: attack `0.05`, decay `0.25`, sustain `0.5`, release `0.35`, amount `0.5`, band `BASS`, gate `0.25`, retrigger `true`. `readRateSeconds` `:175-188`: clamp to `ModTable.minRateSeconds..maxRateSeconds`, and the `rateHz` → period migration. Slot counts pad/cut to `LfoEngine.SLOTS` / `AdsrEngine.COUNT` `:34-36`, `:51-53`. `loadHandMask` `:73-83` decides the hand mask (0 / `HAND_ALL`) and deletes undecodable routes. `saveReactions` `:94` packs routes with `BuildStyleReactionsUseCase.packExact` | caller: main (`ui/ModulationController`), wallpaper/dream main, Cast executor (through `LookStore`) | `reactionsTextEncode` `:27`, `reactionsTextDecode` `:28`; constant `HAND_ALL` | `core/viz/Lfo.*`, `core/viz/Adsr.*`, `core/viz/ReactionText.*` (reactions already native text); new: a native `modulation text` codec for `lfos`/`adsrs` that also reads the old JSON | prefs I/O of four keys |
| `data/LibraryPrefsStore.kt` (50) | shell | none (`MinTrackLength.keeps` `:22-25` is a library filter) | caller | none | n/a | prefs |
| `data/LibraryRepository.kt` (102) | shell | interface only | n/a | none | n/a | n/a |
| `data/LibrarySettings.kt` (35) | shell | none | IO | none | n/a | prefs |
| `data/LookStore.kt` (74) | mixed (thin) | `current` `:25-47` assembles the `LookSnapshot` a renderer starts from (scene id, params, LFOs, ADSRs, reactions, sound tuning, reduced motion, transition id and ms) with Kotlin fallbacks from `LookSnapshot()`; `pushProcessWide` `:53-59` sets the hand mask | wallpaper engine and dream service (main) `wallpaper/VisualizerWallpaperService.kt:191-193`, `wallpaper/VisualizerDreamService.kt:137-139`; Cast executor `cast/CastStream.kt:154` | via `HandMask.set` (`:engine:scenes`) and `LfoStore` | new: `core/signal` "saved look" loader fed the raw saved strings | reading prefs and `Settings.Global` |
| `data/Lyrics.kt` (165) | shell | none (LRC parser `:125-160`, line lookup `indexAt` `:14-29`) | IO for load; `indexAt` on main (`ui/PlayerPanels.kt`) | none | n/a | SAF, MediaMetadataRetriever |
| `data/MacroEditor.kt` (233) | shell | none: parsers of three native tables (`macroeditor|1` `:150-180`, `macrocontrols|1` `:125-137`, `mod|…` rows `:95-107`) | main | none (texts come from `MacroStore`) | `core/viz/MacroBank.*`, `core/viz/MacroControls.cpp` | drawing only |
| `data/MacroStore.kt` (304) | shell (thin, one rule) | every edit is a native call returning the new bank text. Kotlin: draws seeds with `Random.nextLong()` `:122`, `:129`, `:132`; `centreAll` folds `macroBankSetValue(…, 0f)` over all knobs `:207-210`; draft handling `:187-235`; `knobsOf` parses `knob|id|value|name|hint` rows `:264-273` | main; load in container scope | `macroBankSet` `:63`, `macroBankEditor` `:79`, `macroBankControls` `:85`, `macroBankLiveMods` `:90`, `macroBankRename` `:95`, `:106`, `macroBankAddKnob` `:98`, `macroBankAddKnobFor` `:101`, `macroBankAutoName` `:119`, `macroBankRollRanges` `:122`, `macroBankAddRandom` `:129`, `macroBankRollValues` `:132`, `macroBankHideKnob` `:135`, `macroBankShowKnob` `:138`, `macroBankDuplicateKnob` `:141`, `macroBankRemoveKnob` `:143`, `macroBankMoveKnob` `:148`, `macroBankResetKnob` `:151`, `macroBankDefault` `:154`, `macroBankAddLink` `:159`, `macroBankSetLink` `:168`, `macroBankSetCurve` `:179`, `macroBankRemoveLink` `:173`, `macroBankSetValue` `:203`, `:209`, `macroBankNormalize` `:284` | `core/viz/MacroBank.*` (owner already); `centreAll` → new native `macroBankCentreAll` | one prefs string |
| `data/MediaStoreSource.kt` (77) | shell | none | IO | none | n/a | MediaStore consent requests, thumbnails |
| `data/MilkPackImporter.kt` (92) | mixed (thin) | `missesATexture` `:76-91` applies the sampler grammar to decide whether a preset lacks a texture | caller (import controllers) | none | new: `core/viz` milk-pack helper beside the projectM adapter | zip/SAF streams |
| `data/MilkTextureLinks.kt` (273) | mixed | decides which texture file a `.milk` preset's `sampler_x` gets: `referencedTextures` `:79-89`, `resolve` ladder `:161-187` (override, exact, normalized, stand-in), `SAMPLER_REFERENCE` regex `:243`, `BUILTIN_SAMPLERS` `:245-257`, `normalize` / `stripRenameSuffix` / `commonPrefix` `:259-271`; materializes symlinks `:197-216` | caller (`ui/MilkImportController`, `ui/TextureController`; thread not traced) | none | new: `core/viz` (projectM texture search path resolver); only files and `Os.symlink`, both available in the NDK | nothing Android-only; the override picker UI |
| `data/MusicLibraryRepository.kt` (547) | shell | none (library scan, tags, playlists) | IO, a 1-lane IO dispatcher, main for publishes | via `NativeTags` | n/a | MediaStore, SAF, ContentObserver |
| `data/MusicPlaylistStore.kt` (141) | shell | none | IO lane | none | n/a | filesDir |
| `data/NativeTags.kt` (168) | shell | none (marshals fd + arrays to TagLib) | IO | `tagsRead` `:72`, `tagsWrite` `:113` | TagLib behind the JNI (not located) | `ContentResolver.openFileDescriptor`, scoped-storage consent |
| `data/NoteResponseStore.kt` (79) | shell | none (one switch; default `true` `:67`) | container scope | `noteResponseSet` `:33` | `core/viz/NoteResponse.hpp` | prefs |
| `data/OpenedAudioSource.kt` (66) | shell | none | IO | none | n/a | ContentProvider checks |
| `data/PaletteStore.kt` (176) | mixed | **decides scene params for a palette:** `applyGradient` `:82-96`, `applyPalette` `:99-108`, `applyStops` `:111-115`, `clear` `:117-120`, `forgetDeleted` `:122-131` write `paletteBaseOverride`, `paletteRangeOverride`, `palette2*Override`, stops and palette ids on `SceneParams`; `MAX_STOPS = 5` `:66`; hue/span clamps 0..1 `:89-90`, `:143-145`, `:163-164`; stops filtered to `0..0xFFFFFF` | main (`ui/TrackColorController`, palette UI); file I/O where the caller puts it | none | `core/viz/Palette.*`, `core/viz/PaletteTable.inc`, `core/viz/ArtPalette.*` (new: "apply custom palette to params" function) | the palette files |
| `data/PendingRows.kt` (40) | shell | none | caller | none | n/a | MediaStore `IS_PENDING` |
| `data/PerformanceStore.kt` (92) | mixed (thin) | first-run pick of the render quality mode: `devicePerformanceMode` `:87-92` → `PerformanceSettings.modeForDevice(totalMem, lowRam, performanceClass)` (in `:engine:scenes`); `load` falls back to `PerformanceSettings.HIGH` `:41` | main (lazy load) | none | new: `core/viz` or `core/signal/EngineSettings` (the mode table) | `ActivityManager`, `Build.VERSION.MEDIA_PERFORMANCE_CLASS` |
| `data/PlayerPrefs.kt` (231) | mixed (thin) | audio parameter ranges in Kotlin: `coerced` `:44-54` and constants `:57-80` (speed 0.5..2, pitch ±6 st, fade ≤ 6000 ms, crossfade ≤ 12000 ms in 1000 ms steps, ReplayGain mode 0..2, preamp ±15 dB); `crossfadeMsFromSeconds` `:67-73`; bookmark rule (tracks ≥ 20 min, 10 s edges) `:206-213`, `:222-223` | IO via repository; `PlayerPrefsStore.load` on the playback session's thread | none | new: `core/audio/dsp` ranges (the DSP params' definitions) | Media3 player options, prefs |
| `data/PlayerPrefsRepository.kt` (6) | shell | typealias | n/a | none | n/a | n/a |
| `data/PlaylistFormats.kt` (~80) | shell | `core/library/Playlist` (M3U/PLS/XSPF parse); Kotlin keeps the wire split and the by-name match | IO | none | n/a | `core/tests/playlist_test.cpp` |
| `data/PresetRepository.kt` (109) | shell | none | IO | none | n/a | n/a |
| `data/PresetStore.kt` (311) | mixed | **the preset/params file codec.** `putParams` `:252-268` and `readParams` `:277-296` walk `ParamSchema.params` (the Kotlin read of `core/viz/ParamTable.inc`) and decide per kind: BOOL threshold 0.5, INT rounding, default on a missing/non-finite value, clamp to `spec.range`, no clamp for CIRCULAR `:289`, retired rows read as default `:283`. `ATTACK_RANGE = 0.005..1`, `DECAY_RANGE = 0.01..1.5` `:210-211`; `finiteOr` / `finiteIn` `:224-236`; `fromJson` aliases scene ids via `Catalogue.resolve` `:302`; file-name rule `safeFileName` `:185-198` | IO for files; `fromJson`/`toJson` also on main (`VizStateStore.restore`, link confirm dialog per `data/TemplateFormat.kt:179-181`) and on the Cast executor (via `LookStore`) | none direct | `core/viz/ParamTable.inc`; new: native preset JSON/text codec (read + clamp + default) so Kotlin stores opaque text | file listing, folders |
| `data/ReadCap.kt` (44) | shell | none (byte caps per document type `:15-20`) | caller | none | n/a | n/a |
| `data/RecordFiles.kt` (154) | shell | none (opens the fd the muxer writes to) | worker | none | n/a | MediaStore, SAF, ParcelFileDescriptor |
| `data/RecordPrefs.kt` (70) | shell | none; fps must be in `EXPORT_FPS_OPTIONS` `:42`; length snap is native | caller | `recordSnapMaxLength` `:35` | `core/output/RecordPlan.*` | prefs |
| `data/RecordSaveStore.kt` (78) | shell | none | caller | none | n/a | persisted URI grants |
| `data/SessionRepository.kt` (22) | shell | none | IO | none | n/a | n/a |
| `data/SessionStore.kt` (107) | shell | none (queue + position) | IO | none | n/a | filesDir |
| `data/SmartPlaylistStore.kt` (167) | shell | none about engine (`SmartPlaylistMatcher.resolve` `:53-71` filters library tracks) | caller (`ui/PlayerSession`) | none | n/a | library feature |
| `data/SoundInputSettings.kt` (59) | shell | none; the tuning is a float list made and read by `engine/audio-core` `SoundTuning` (`savedFloats`, `fromFloats`) `:45-46`, `:50-58` | IO | none direct | sound engine tuning schema (`core/sound`, via `SoundTuning`) | prefs |
| `data/StillFiles.kt` (49) | shell | none | worker | none | n/a | MediaStore, `Bitmap.compress` |
| `data/StudioPrefsStore.kt` (42) | shell | none | caller | none | n/a | prefs |
| `data/StyleShelfStore.kt` (78) | shell | none (favourites/recents; rules in `model/StyleShelf`) | container scope | none | n/a | prefs |
| `data/SystemMotion.kt` (38) | shell | `reducedAt` `:30`: reduced motion only when the animator scale is exactly 0 (a platform reading; the engine gets a boolean) | caller | none | n/a | `Settings.Global` |
| `data/TemplateFormat.kt` (607) | mixed (format codec) | `readLook` `:320-337`: scene-id aliasing + default `SceneIds.DEFAULT` `:328`, attack/decay clamp to `PresetStore` ranges `:329-330`, params through `PresetStore.paramsFromJson`; the rest is the carry-unknown-keys codec | `Dispatchers.Default` for import `data/TemplateRepository.kt:597`; main for the link confirm dialog (`:179-181`); IO for files | none | the look part belongs with the native preset codec (see `PresetStore`); layout/text/export are not rendered (`data/VideoTemplate.kt:9-10`) | sharing, files |
| `data/TemplateRepository.kt` (717) | mixed | `BundledTemplates` `:163-501`: **six built-in looks defined as Kotlin `SceneParams` literals** with attack/decay, palette index, bloom, speed etc. (`reelNeon` `:176-211`, `loFiLoop` `:213-272`, `vinylSpin` `:274-323`, `albumTeaser` `:325-380`, `podcastBars` `:382-437`, `master4K` `:439-473`). Storage part `:77-155`, `:561-717` is shell | IO; `BundledTemplates.ALL` built at class init | none | `core/viz/CataloguePresets.*` (the place bundled looks already live) | template files, share staging |
| `data/TextureStore.kt` (258) | mixed | `generateDisplayPreset` `:173-214` **writes a MilkDrop preset with its composite shader text from a Kotlin string** (pulse `0.75 + 0.45*treb + 0.30*bass`, zoom `1 + 0.10*bass`, `fDecay=0.97`, `fGammaAdj=1.6`); image validation: DDS/TGA header parse and `MAX_TEXTURE_SIDE = 4096`, `MAX_TEXTURE_BYTES = 16 MiB` `:64-112`, `:220-223`; `safeTextureFileName` `:243-256` | caller (`ui/TextureController`; thread not traced) | none | new: generated-preset text in `core/viz` (projectM adapter) or a shipped asset; texture limits beside the native texture decoder | SAF read, `BitmapFactory` bounds decode |
| `data/TrackLibrary.kt` (167) | shell | none (`remove` `:39` has no caller) | IO | none | n/a | filesDir |
| `data/VideoTemplate.kt` (392) | mixed (thin) | Kotlin defaults of a look: `DEFAULT_ATTACK = 0.6`, `DEFAULT_DECAY = 0.12` `:181-182`, `SceneIds.DEFAULT` `:159`; `TemplateExport.resolvedFps` `:383` (nearest of `EXPORT_FPS_OPTIONS`; test-only caller). The layout/text/export model is stored and round-tripped, not rendered `:9-10` | n/a (model) | none | defaults: `core/viz/ParamTable.inc` / native preset codec | template model for the UI |
| `data/VisualsSettings.kt` (76) | mixed (thin) | Kotlin defaults and a range of engine-facing values: `presetMorphSeconds = 2` (max 8 `:38`, clamp `:62-63`), `touchTransform = true`, `touchSmear = false`, `touchSmearStrength = 1` `:19-25` | IO via repository | none | `core/viz/TouchTransform`, `core/viz/TouchSmear` (named `:21-23`); morph length: `core/viz/Transition.*` | prefs |
| `data/VizStateStore.kt` (108) | mixed (thin) | `restore` `:42-62` builds the live look at start: derives `transitionStyle` from `TransitionCatalog.builtIn(id)` `:51`, parses `live_state` with `PresetStore.fromJson`; `write` `:78-89` serialises it under the name `__live__`; `committedSceneId` rule `:34-35`, `:81` | main for restore; write 400 ms later on IO `:67-71`; `flushIfDirty` on the caller | none direct | native preset codec (opaque `live_state`); `core/viz/Transition*.hpp` | prefs, debounce |
| `data/VizUiState.kt` (27) | mixed (data) | **Kotlin defaults of the live look and Auto:** first style `FreeTier.FIRST_STYLE_ID` `:10`, attack `0.6`, decay `0.12` `:11-12`, transition `FADE` / `"fade"` / `1.2 s` `:19-21`, visual playlist interval `30 s` `:18`, random interval `20 s` and its include flags `:23-26` | n/a | none | `core/viz/Catalogue.*` (first style), `core/viz/Transition*.hpp`, param defaults in `core/viz/ParamTable.inc` | UI state holder |
| `data/WatermarkPrefs.kt` (51) | shell | none (ints are native codes, opaque `:39-40`) | IO | none | native watermark codes (not located) | prefs |
| `data/backup/BackupArchive.kt` (196) | shell | none (zip pack/unpack, SHA-256, caps) | IO | none | n/a | user backup file |
| `data/backup/BackupFormat.kt` (295) | shell | none (section allowlist, device-local keys, `PrefsJson`) | IO | none | n/a | n/a |
| `data/backup/BackupManifest.kt` (96) | shell | none | IO | none | n/a | n/a |
| `data/backup/BackupMigrations.kt` (91) | shell | none (v1 → v2 moves prefs keys between archive files; no value is computed) | IO | none | n/a | n/a |
| `data/backup/BackupRepository.kt` (266) | shell | none | IO (`@IoDispatcher`) | none | n/a | SAF, prefs |
| `data/prefs/PrefKey.kt` (106) | shell | none (schema DSL) | n/a | none | n/a | n/a |
| `data/prefs/SettingsRepository.kt` (119) | shell | none | scope + IO | none | n/a | prefs |

### 2. Threads started here

- `data/HistoryStore.kt:167-170`: single-thread executor, thread name `geode-history`, daemon, one per process (companion). Runs `writeNow` (serialise + `AtomicWrite`), coalesced by `writeQueued` `:119`; fired on every `recordPlay` and on `flush`.
- `data/OpenedAudioSource.kt:37`: `CoroutineScope(SupervisorJob() + io)` named `background`; one display-name query per opened file, abandoned after 2 s `:42`, `:64`.
- `data/MusicLibraryRepository.kt:80`: `Dispatchers.IO.limitedParallelism(1)` (`playlistEdits`), one lane for playlist edits and `refresh`. `:172`: a collector in the injected scope debounces MediaStore changes by 1,000 ms (`:62-64`) and re-queries. `:178`: `ContentObserver` on `Handler(Looper.getMainLooper())`. `:158`, `:269`, `:298`, `:334`, `:364`, `:381`: `scope.launch(Dispatchers.IO)` for scans, imports and tag saves.
- `data/VizStateStore.kt:67-71`: `scope.launch(Dispatchers.IO)` with `delay(400 ms)` (`PERSIST_WINDOW_MS` `:106`); at most one pending write, then `commit()`.
- `data/prefs/SettingsRepository.kt:97-103`: one `scope.launch` per repository for the first read (`withContext(io)`); `:116` each update writes on `io` under a `Mutex`.
- `data/FavouritesRepository.kt:46-52`, `:43`: `scope.launch` for the first load and for each prefs-change reload; reads and writes on `Dispatchers.IO` `:56`, `:66`, `:76`.
- `data/AutoDirectorStore.kt:70-78`, `data/MacroStore.kt:69-75`, `data/NoteResponseStore.kt:41-44`: `scope.launch` in the container's scope (dispatcher is the container's, not set here); they push to native after the first load and on each change. Edits launch one coroutine each (`AutoDirectorStore.kt:83`, `:99`; `MacroStore.kt:240`; `NoteResponseStore.kt:48`; `CustomizeGroupsStore.kt:55`; `StyleShelfStore.kt:35`).
- `data/PresetRepository.kt`, `data/SessionRepository.kt`, `data/TemplateRepository.kt`, `data/MediaStoreSource.kt`, `data/backup/BackupRepository.kt`: `withContext(Dispatchers.IO / io)` per call; `data/TemplateRepository.kt:597` decodes on `Dispatchers.Default`.
- No `Thread`, `HandlerThread` or frame/audio ticker is created in this scope.

### 3. Per-frame and per-audio-block Kotlin paths

None in this scope. The nearest:

- `MacroStore.liveMods()` (`data/MacroStore.kt:90`) is polled by the Customize knobs: `ui/CustomizeMacros.kt:94-97` loops `delay(LIVE_POLL_MS)` → `GeodeNative.macroBankLiveMods()` → `MacroLiveTable.parse`. UI poll on main, not per frame (rate is `LIVE_POLL_MS` in that file, outside this scope).
- `MacroStore.setValue` / `setLink` with `persist = false` (`data/MacroStore.kt:162-168`, `:198-204`): one JNI round trip per drag event (bank text in, bank text out, then `macroBankSet`).
- `AutoDirectorStore.onPosition` (`data/AutoDirectorStore.kt:117-124`): called per playback position tick by `playback/AutoDirectorFeed`; pushes `directorSet` only when the amount changes.
- `Lyrics.indexAt` (`data/Lyrics.kt:14-29`): binary search per position update in `ui/PlayerPanels.kt`.
- `LookStore.current()` (`data/LookStore.kt:25`): once per surface start, and on the Cast executor at `cast/CastStream.kt:154`.

### 4. Duplicated drivers and duplicated logic

- **Two bundled-look tables.** Built-in presets are native (`core/viz/CataloguePresets.*`, read by `data/BuiltInPresets.kt:19`), while the six starter templates carry their looks as Kotlin `SceneParams` literals (`data/TemplateRepository.kt:163-473`).
- **Look defaults in four places:** `data/VizUiState.kt:11-12` (0.6 / 0.12), `data/VideoTemplate.kt:181-182` (0.6 / 0.12), `SceneParams()` Kotlin defaults used as the base in `data/BuiltInPresets.kt:53`, `:58`, and the schema defaults (`spec.default`) used in `data/PresetStore.kt:280-287`.
- **fps list:** `data/ExportPrefs.kt:96` is a Kotlin copy of the native encoder ladder's range (`core/output/EncoderLadder.hpp`), reused by `data/EditorProjectJson.kt:153`, `data/RecordPrefs.kt:42`, `data/VideoTemplate.kt:383`.
- **LFO/ADSR defaults and rate range** are in `data/LfoStore.kt:116-188` in addition to `LfoConfig()` / `AdsrConfig()` constructor defaults and `ModTable` in `:engine:scenes` (and `core/viz/Lfo.*`, `core/viz/Adsr.*`).
- **Sampler grammar:** one regex, shared (`data/MilkTextureLinks.kt:243`, used by `data/MilkPackImporter.kt:85`); projectM applies its own lookup natively.
- **Saved-look loaders:** `VizStateStore.restore` (`data/VizStateStore.kt:42-62`) and `LookStore.current` (`data/LookStore.kt:25-47`) both parse `live_state` and the transition keys; they share the key constants only (`data/VizStateStore.kt:103-105`).
- **Bounded readers:** `ReadCap.copy` (`data/ReadCap.kt:24-37`), `TextureStore.readBounded` (`data/TextureStore.kt:143-156`), `FileTemplateRepository.readBounded` (`data/TemplateRepository.kt:706-715`), `BackupArchive.copyHashed` (`data/backup/BackupArchive.kt:169-186`).
- **Quarantine:** `TrackLibrary.readLocked` renames to `.corrupt` by hand (`data/TrackLibrary.kt:55`) instead of `AtomicWrite.quarantine`.
- **Prefs openers outside `GeodePrefsFiles`:** `RecordSaveStore` (`data/RecordSaveStore.kt:29`) and `BackupRepository.prefsFile` (`data/backup/BackupRepository.kt:245`).

### 5. Data crossing the JNI boundary (engine/mixed files)

| From | To native | Back | How often |
|---|---|---|---|
| `AutoDirectorStore` | `directorSet(Boolean on, Float amount, Long seed)`; `directorReachSet(Int)`; `directorKeepSet(String, keys joined by "\n")`; `soundTrackBoundary(Long seed)` | nothing | on change; once per track; at start |
| `MacroStore` | bank text (String, whole bank) + ids, names, floats, style id, `Long` seed | new bank text (String); editor / controls / live-mods tables (String, `|`-separated rows) | per edit; per drag event; live mods per UI poll |
| `MacroBankCodec` | stored bank text | normalized bank text | once at load |
| `LfoStore` | `FloatArray` of packed routes (`BuildStyleReactionsUseCase.packExact`) | text (String) on save; `FloatArray?` on load | per save; per load (app start, each wallpaper/dream/cast start) |
| `BuiltInPresets` | nothing | presets table (String) | once per process |
| `BackgroundPrefsStore` | one Float/Int per clamp | clamped value; blur max (Int) | per `load()` |
| `RecordPrefsStore` | Int seconds | snapped Int | per read |
| `NoteResponseStore` | Boolean | nothing | on change |
| `NativeTags` | detached fd (Int), `Array<ByteArray?>(6)`, `IntArray(4)`, `FloatArray(4)`; write: fd, 6 UTF-8 byte arrays, year, track | bit mask Int (`<0` = failure; bits 1/2/4/8 = track gain, track peak, album gain, album peak); Boolean | per tag read/write |
| `LookStore` | hand mask Int + Boolean (via `HandMask.set`) | nothing | once per surface process start |

`PresetStore`, `PaletteStore`, `TemplateFormat`, `VizStateStore` call no native function: they use `ParamSchema` / `SceneParamsCodec` / `Catalogue` in `:engine:scenes`, which hold the Kotlin read of the native schema.

### 6. Saved formats (the compatibility contract)

"Opaque" = Kotlin stores the string and hands it to native unparsed. "Kotlin" = parsed or interpreted in Kotlin today.

#### 6.1 SharedPreferences files (`data/GeodePrefsFiles.kt:42-58`)

`geode-prefs` (GENERAL), `geode-viz` (VIZ), `geode-player` (PLAYER), `geode-favourites`, `geode-audiofx`, `geode-mod`, `geode-library`, `geode-background`, `geode-wallpaper`, `geode-widget`, `geode-pro`, `geode-record`. The keys of `geode-audiofx` (`audio/AudioFxController`), `geode-mod` (`ui/ModulationController`), `geode-wallpaper`, `geode-widget` and `geode-pro` are owned outside this scope; only `locked_params` of `geode-mod` is read here.

**`geode-prefs`**

| Key(s) | Type | Owner (path:line) | Interpreted where |
|---|---|---|---|
| `macro_bank` | String | `data/MacroStore.kt:245` | **opaque** native bank text; Kotlin reads only its `knob|<id>|<value>|<name>|<hint>` rows (`:263-273`). Test fixture text starts `macrobank|1|1` |
| `macro_knobs`, `macro_presets` (String), `macro_locks` (Int) | retired | `data/MacroStore.kt:251`, `:257-258` | removed on first read (`:291-293`), never restored |
| `lfos` | String, JSON array, up to `LfoEngine.SLOTS` objects | `data/LfoStore.kt:191` | **Kotlin**. Fields `enabled`, `source`, `target`, `wave`, `polarity`, `curve` (enum **names** of `ModSource`, `LfoTarget`, `LfoWave`, `ModPolarity`, `ModCurve`), `rateSeconds`, `depth`, `beats`, `knob`, `moment` (`:139-151`). Legacy field `rateHz` still read (`:182-187`) |
| `adsrs` | String, JSON array, up to `AdsrEngine.COUNT` objects | `data/LfoStore.kt:192` | **Kotlin**. Fields `enabled`, `targets` (array of `LfoTarget` names), `attack`, `decay`, `sustain`, `release`, `amount`, `band` (`EnvBand` name), `gateThreshold`, `sustainTrack`, `retrigger`, `knob`, `moment` (`:100-114`). `moment` absent = `-1` (`:197`) |
| `reactions` | String | `data/LfoStore.kt:193` | **opaque** native reaction text (`core/viz/ReactionText.*`); the KDoc names an older `reactions/1` list that no longer decodes and is deleted (`:70-71`, `:75-80`) |
| `reactions_hand` | Int bit mask | `data/LfoStore.kt:194` | native bits; default `GeodeNative.HAND_ALL` when routes exist without it (`:74`) |
| `auto_director_enabled` (Boolean, default true), `auto_director_reach` (Int 0/1/2, default 0) | | `data/AutoDirectorStore.kt:168-169` | values handed to native |
| `note_response_enabled` | Boolean, default true | `data/NoteResponseStore.kt:65` | handed to native |
| `sound_tuning` | String, comma-separated floats; **absent = engine defaults** | `data/SoundInputSettings.kt:23`, `:44-46` | order/meaning owned by `engine/audio-core` `SoundTuning` (`fromFloats`) |
| `gui_mic_reactive` | Boolean | `data/SoundInputSettings.kt:22` | Kotlin |
| `gui_preset_morph_seconds` (Float), `gui_second_screen`, `gui_touch_transform`, `gui_touch_smear` (Boolean), `gui_touch_smear_strength` (Float); retired `morph_beats` (Int) | | `data/VisualsSettings.kt:32-36`, `:48` | Kotlin |
| `perf_render_scale` (Float), `perf_fps_cap` (Int), `perf_march_scale` (Float), `perf_post_fx`, `perf_adaptive` (Boolean), `perf_liquid_hot` (Int, native code via `LiquidHotPolicy.fromNative`) | device-local | `data/PerformanceStore.kt:63-68` | Kotlin; presence of `perf_render_scale` marks "first run done" (`:40`) |
| `export_quality`, `export_ratio`, `export_codec` (enum **names** of `ExportQuality`, `ExportRatio`, `ExportCodec`), `export_fps` (Int), `export_loop` (Boolean), `export_loudness_target` (String id), `export_save_to` (`"library"` or a tree URI); the same seven with prefixes `dest_tiktok_`, `dest_shorts_`, `dest_feed_`, `dest_square_`, `dest_youtube_`, `dest_master_`, `dest_custom_` | | `data/ExportPrefs.kt:172-181` | Kotlin. `ExportDestination` **ordinal** indexes `ExportPresets.ALL` (`:76`) |
| `record_quality` (String), `record_fps` (Int), `record_max_sec` (Int) | | `data/RecordPrefs.kt:57-59` | Kotlin; length snapped by native |
| `watermark_pro_adds` (Boolean), `watermark_placement`, `watermark_corner` (Int native codes) | | `data/WatermarkPrefs.kt:23-25` | codes are native's |
| `behaviour_start_screen`, `behaviour_last_screen`, `behaviour_queue_end` (enum names), `behaviour_keep_screen_on_visuals`, `behaviour_start_on_headphones`, `behaviour_remember_shuffle_repeat`, `behaviour_dream_screen_bright`, `behaviour_dream_show_clock`, `behaviour_dream_show_now_playing`, `behaviour_dream_hear_other_apps` (Boolean), `behaviour_notification_buttons` (StringSet of `NotificationButton` names), `behaviour_dream_fps` (Int), `behaviour_gesture_<CanvasGesture name>` → `GestureAction` name (10 keys) | | `data/BehaviourPrefs.kt:256-268` | Kotlin; only changed fields are written (`:225-248`) |
| `gui_corner`, `gui_opacity`, `gui_accent_intensity`, `gui_mini_player_fade_sec`, `gui_font_color`, `gui_text_scale`, `gui_pip_auto_enter`, `gui_ui_sounds`, `gui_accent_follows_visual`, `gui_haptics`, `gui_tab_labels`, `gui_show_time_left`, `gui_screen_rotation`, `gui_knob_size`, `gui_library_start_tab`, `gui_library_sort`, `gui_track_tap`, `gui_show_ab_loop`, `gui_mini_player_previous`, `gui_stage_arrows`, `gui_stage_record`; retired `gui_white_font`, `gui_auto_enter_pip`, `gui_reduced_motion`, `app_theme`, `gui_background_dim`, `gui_follow_system_dark`, `gui_clear_visuals_menu` | | `data/GeneralSettings.kt:93-129` | Kotlin (UI) |
| `gui_safety_acknowledged`, `gui_setup_done`, `gui_tutorial_done`, `gui_tutorial_resume`, `gui_tutorial_tips`, `gui_tutorial_off`; retired `gui_intent`, `gui_safety_choice`, `gui_tutorial_seen`, `gui_tutorial_on_first_run` | state | `data/AppState.kt:34-49` | Kotlin (tour strings encoded by `model/TourProgress`) |
| `customize_open_groups` (StringSet of `CATEGORY/section` native ids), `customize_tab` (String) | state | `data/CustomizeGroupsStore.kt:61-62` | ids are native's (`core/viz/CustomizeMap`) |
| `boot_anim` (Boolean) | | `data/BootAnimationStore.kt:17` | Kotlin |
| `studio_last_tab` (String tab name), `studio_keep_as_project` (Boolean, absent = ask) | | `data/StudioPrefsStore.kt:33-34` | Kotlin |
| `preset_mirror_uri` (String) | device | `data/LibrarySettings.kt:21` | Kotlin |

**`geode-viz`**

| Key | Type | Owner | Interpreted |
|---|---|---|---|
| `live_state` | String = a Preset JSON (6.2) named `__live__` | `data/LookStore.kt:63`; written `data/VizStateStore.kt:82-88` | **Kotlin** (`PresetStore.fromJson`) |
| `transition_id` (String, e.g. `"fade"`), `transition_duration_sec` (Float) | | `data/LookStore.kt:64-65` | Kotlin (`TransitionCatalog.builtIn`) |
| `auto_random_interval_sec`, `auto_playlist_interval_sec` (Int, 5..300), `auto_random_include_styles`, `auto_random_include_presets`, `auto_random_include_milk`, `auto_playlist_enabled` (Boolean), `auto_playlist_entries` (String JSON array of `{sceneId, label, presetName?, milkPath?}`); retired `auto_random_enabled` | | `data/AutoVisualsPrefsStore.kt:60-67`, `:77-91` | Kotlin |
| `style_favourites` (StringSet of style ids), `style_recents` (String, comma-separated ids), `style_random_family_only` (Boolean) | | `data/StyleShelfStore.kt:41-46` | Kotlin |
| `MilkPresetMemory.KEY` (String) | | declared `data/GeodePrefsFiles.kt:61-64`; written by `:engine:scenes` | outside scope |

**`geode-player`** (`data/PlayerPrefs.kt:103-126`): `shuffle`, `repeat_mode` (Int 0..2), `speed`, `pitch_semitones` (Float), `skip_silence`, `pause_on_noisy`, `keep_screen_on`, `auto_resume`, `sleep_timer_minutes`, `sleep_finish_track`, `fade_ms`, `replaygain_mode` (Int 0..2), `replaygain_preamp_db`, `replaygain_clip_guard`, `resume_long_tracks`, `bit_perfect`, `playback_crossfade_ms`, `oboe_output`, `picture_delay_ms` (device-local); retired `native_engine`, `crossfade_ms`, `crossfade_curve`, `gapless` (`:137`, `:190-193`); prefix `bookmark:<uri>` → Long ms (`:224`).

**`geode-favourites`**: `uris` = URIs joined by `"\n"`, oldest first (`data/FavouritesStore.kt:42-44`).
**`geode-library`**: `min_track_seconds` (Int, `data/LibraryPrefsStore.kt:43`); `roots` (StringSet of tree URIs, device-local, `data/MusicLibraryRepository.kt:66-71`).
**`geode-background`**: `background_uri` (device), `background_blend` (`UnderlayBlend` **name**), `background_amount`, `background_dim` (Float), `background_blur_radius` (Int) (`data/BackgroundPrefsStore.kt:56-60`).
**`geode-record`**: `save_target` (`"library"` or tree URI; absent = not asked) (`data/RecordSaveStore.kt:69-70`).
**`geode-mod`**: `locked_params` (StringSet of Customize keep keys) read at `data/AutoDirectorStore.kt:156`, `:190-191`; written by `ui/ModulationController`.

#### 6.2 Files under `filesDir`

| Path | Format / version | Owner (path:line) | Interpreted |
|---|---|---|---|
| `presets/**/<safeFileName(name)>.json` (subfolders = user folders) | Preset JSON, 2-space indent, **no version field**. Envelope keys `name`, `sceneId`, `attack`, `decay`, optional `customShader`, `milkPreset` (`data/PresetStore.kt:166-177`); one key per non-retired row of the native param schema under the row's name (bool → JSON boolean, int → int, float/circular → number, `:252-265`); `customPaletteId`, `customPalette2Id` strings (`:242-243`). Unknown keys are dropped on re-save; missing keys read as schema defaults | `data/PresetStore.kt:28`, `:154` | **Kotlin** |
| `milk/<safeFileName(name)>.milk` | MilkDrop preset text materialised from a preset's `milkPreset` | `data/PresetStore.kt:30`, `:114-128` | projectM |
| `milk/textures/<name>.<png|jpg|jpeg|bmp|tga|dds|dib>` | images, name rule `safeTextureFileName` (`_` + 8 hex of SHA-256 when renamed) | `data/TextureStore.kt:36`, `:217`, `:243-256` | projectM |
| `milk/textures/.links/<presetStem>/<expected>.<ext>` | symlinks or copies, rebuilt | `data/MilkTextureLinks.kt:73-75`, `:237` | cache |
| `milk/texture-links.json` | `{ "<preset file name>": { "<expected lowercase>": "<texture file>" } }` | `data/MilkTextureLinks.kt:76`, `:145-157` | Kotlin |
| `milk/generated/show_<texture name, dots → _>.milk` (legacy `show_<stem>.milk`) | generated MilkDrop preset | `data/TextureStore.kt:169-171`, `:121` | cache |
| `palettes/<id>.json` | `{id, name, baseHue, hueSpan, stops:[0xRRGGBB ints, ≤5]}`, 2-space indent, no version | `data/PaletteStore.kt:24`, `:148-155` | Kotlin |
| `templates/<stem>--<idTag>.geode.json` | `"format": "geode.template"`, `"formatVersion": 1`; sections `look` (`sceneId`, `attack`, `decay`, `params` = preset param keys, `customShader`, `milkPreset`), `layout`, `text.slots`, `export` (+ `segment.kind` = `WHOLE_TRACK` / `FIXED` / `LOUDEST_WINDOW`); enum tokens are names, read case-insensitively; unknown keys and tokens are carried and re-written; colours `#AARRGGBB` | `data/TemplateFormat.kt:151-166`, keys `:543-594`; store `data/TemplateRepository.kt:146-147` | Kotlin |
| `geode://template/<gzip+base64url>` link | same JSON through `model/PresetLink` | `data/TemplateFormat.kt:191-203`, `:604-606` | Kotlin |
| `projects/<id>/project.json` | `"version": 1` (folder) with `edit.version: 1`; keys `id`, `name`, `kind{type: standard|loop, targetLengthMs, loopLengthMs}`, `destination` (`ExportDestination` name), `export{quality, fps, ratio, loopSafe, codec, loudnessTargetId, saveTo}`, `createdAtMs`, `updatedAtMs`, `templateId`, `renders[{uri,label,createdAtMs,durationMs}]`, `edit{timeline{durationMs, lanes[{id, kind: visual|media|text|overlay|audio, name, muted, locked, clips[…]}]}, markers[], keyframes[]}`; clip `content.type` = `scene|video|still|text|overlay|audio`; keyframe `value.kind` = `scalar|vector2|colour|toggle|choice`; `interpolation.type` = `hold|linear|ease|custom`; marker `origin.type` = `manual|tapped|detected`. A newer version is refused; unknown tags fail the read and the file is quarantined | `data/EditorProjectJson.kt:45-48`, `:54-67`, `:101-106`; `data/EditorProjectStore.kt:105-108` | Kotlin |
| `project-templates/<id>/template.json` | same shape minus renders, plus `lengthMs` (`data/EditorProjectJson.kt:86-99`); **read only: no writer in this scope** | `data/EditorProjectStore.kt:106`, `:108` | Kotlin |
| `editor/`, `takes/`, `project-takes/` | legacy; deleted on store construction | `data/EditorProjectStore.kt:110-122` | n/a |
| `music-playlists/<safeFileName(name)>.json` | `{name, tracks:[uri…]}` | `data/MusicPlaylistStore.kt:16`, `:127-131` | Kotlin |
| `smart-playlists/<safeFileName(name)>.json` | `{name, matchAll, limit, rules:[{field: RuleField name, op: RuleOp name, value}]}` | `data/SmartPlaylistStore.kt:121`, `:138-146` | Kotlin |
| `library.json` | `{version: 3, tracks:[{uri,title,artist,durationMs,album,genre,year,trackNo,comment,fileName,sizeBytes}]}`; a bare array is also read | `data/TrackLibrary.kt:12`, `:64`, `:125-150` | Kotlin |
| `history.json` | `{tracks:[{uri,last,count,title,artist,ms}]}`; a bare array is also read | `data/HistoryStore.kt:10`, `:45-51`, `:147-158` | Kotlin |
| `session.json` | `{version: 1, tracks:[{uri,title,artist}], index, positionMs}`; newer version → quarantined | `data/SessionStore.kt:93-103` | Kotlin |
| `lyrics/<sha1(track uri) hex>.lrc` | LRC text | `data/Lyrics.kt:44-54`, `:79` | Kotlin |
| `*.tmp`, `*.corrupt` | AtomicWrite in-flight / quarantined | `data/AtomicWrite.kt:9-11` | n/a |

File-name rule shared by presets, playlists, smart playlists, palettes, projects, templates: `PresetStore.safeFileName` (`data/PresetStore.kt:181-198`): chars outside `[A-Za-z0-9-_ ]` → `_`, and when anything changed (or the name is blank) `-` + 8 hex chars of SHA-256 is appended.

#### 6.3 Other locations

- `cacheDir/studio-record-<ms>.mp4` scratch recording (`data/RecordFiles.kt:80`); `cacheDir/share/templates/<name>.geode.json` (`data/TemplateRepository.kt:686`).
- MediaStore: `Movies/BeatRealms/beatrealms_record_<ms>.mp4`, `video/mp4`, `IS_PENDING` (`data/RecordFiles.kt:52`, `:151-152`); `Pictures/BeatRealms/beatrealms_<ms>.png` (`data/StillFiles.kt:28-30`).
- `noBackupFilesDir/backup-restore/{import.zip, staged/, aside/}` (`data/backup/BackupRepository.kt:53-55`, `:87`).

#### 6.4 Backup archive (`beatrealms-backup-YYYYMMDD.zip`, `data/backup/BackupFormat.kt:244-245`)

- `manifest.json`: `format: "muzviz-backup"` (`data/backup/BackupFormat.kt:112`), `version: 2` (`:118`), `appVersionCode`, `createdAtMs`, `sections` (ids `settings`, `creative`, `music`, `milk`, `:64-97`), `entries[{path,size,sha256}]` (`data/backup/BackupManifest.kt:47-55`).
- `prefs/<file>.json` and slices `prefs/creative/geode-prefs.json`: `{key: {type: string|int|long|float|boolean|stringSet, value}}` (`data/backup/BackupFormat.kt:179-185`, `:254-291`).
- Creative owns `geode-mod` plus the borrowed keys `macro_bank`, `lfos`, `adsrs`, `reactions`, `reactions_hand` of `geode-prefs` (`:87`), dirs `presets`, `palettes`, `templates`, `editor`, `projects`, `project-takes`, `project-templates` (`:85`). Music owns `geode-favourites`, `music-playlists`, `smart-playlists`, `library.json`, `history.json` (`:89-94`). Milk owns `milk` minus `milk/generated` and `milk/textures/.links` (`:97`, `:174`). Settings owns `geode-prefs`, `geode-viz`, `geode-player`, `geode-audiofx`, `geode-library`, `geode-background`, `geode-wallpaper` (`:64-77`).
- Device-local keys never exported or restored: `:150-167`.
- Migration v1 → v2 moves the borrowed keys from `prefs/geode-prefs.json` to the Creative slice (`data/backup/BackupMigrations.kt:23-46`).
- Consequence for the port: every key and file above can arrive from an old backup, so the old JSON forms of `lfos`, `adsrs`, `live_state` and preset files must stay readable even if native takes over the format.

#### 6.5 Enum names and ordinals that are persisted

By **name**: `StartScreen`, `QueueEndAction`, `NotificationButton`, `CanvasGesture` (in key names), `GestureAction` (`data/BehaviourPrefs.kt`); `CornerStyle`, `TabLabels`, `ScreenRotation`, `KnobSize`, `LibraryStartTab`, `TrackTap`; `ExportQuality`, `ExportRatio`, `ExportCodec`, `ExportDestination`; `UnderlayBlend`; `ModSource`, `LfoTarget`, `LfoWave`, `ModPolarity`, `ModCurve`, `EnvBand` (in `lfos`/`adsrs`); `RuleField`, `RuleOp`; `MarkerColour`, `OverlayBlend`, `EaseShape`; `ArtworkStyle`, `ProgressStyle`, `TextRole`, `TextAnchor`, `TextWeight`, `AudioHandling`.
By **ordinal / int code**: `ExportDestination.ordinal` → `ExportPresets.ALL` index (`data/ExportPrefs.kt:76`, not stored but order-coupled); `repeat_mode`, `replaygain_mode` (Int); `perf_liquid_hot` (native code); `watermark_placement`, `watermark_corner` (native codes); `reactions_hand` (bit mask); palette index inside preset params (`palette`, e.g. `data/TemplateRepository.kt:194`); `ParamValue.Choice.index` in project keyframes (`data/EditorProjectJson.kt:383`).
Scene ids are strings and pass through `Catalogue.resolve` (alias table in `:engine:scenes`) at `data/PresetStore.kt:302`, `data/AutoVisualsPrefsStore.kt:103`, `data/EditorProjectJson.kt:263`, `data/TemplateFormat.kt:328`.

### 7. Tests for these packages

All under `app/src/test/java/dev/geode/` (JVM). No test for this scope exists under `app/src/androidTest` except setup helpers that write prefs (`journey/Journeys.kt:68`, `store/StoreSetup.kt:125-129` use `PerformanceStore` and `PresetStore.toJson`).

| Test | What it pins |
|---|---|
| `data/PresetGoldenTest.kt` | **saved text:** golden files `app/src/test/resources/presets/{current,legacy,hostile}.json`. Round trip keeps the same keys, values and JSON types; written keys = live schema names + the two palette ids; legacy file fills defaults and drops retired keys; hostile file is clamped to schema ranges |
| `data/SharedLinkNumbersTest.kt` | `finiteOr` / `finiteIn`; a preset and a template with NaN / overflow decode and save again |
| `data/BuiltInPresetsTest.kt` | parser of the native `presets|1|…` table: look = values over defaults, table order, shared looks, unknown param refused |
| `data/MacroEditorTableTest.kt` | parsers of the native macro editor / controls / live-mods texts, read from `core/tests/macro_editor.txt` (system property `geode.macroEditor`); other version or garbage reads as empty |
| `data/MacroStoreTest.kt` | load pushes the native default once; edit is drawn, pushed and saved; a drag writes once at commit; drafts; the three retired keys are dropped |
| `data/NoteResponseStoreTest.kt` | default on, saved choice wins, push only on change |
| `data/prefs/PrefsSchemaTest.kt` | **saved keys:** every store writes exactly its declared keys with the declared types; no key declared twice; backup classes match `BackupFormat`; `KEYS` sets match `PREF_KEYS`; every file that touches prefs is a listed owner |
| `data/prefs/PrefsSettingsRepositoryTest.kt` | repository contract (defaults until loaded, update ordering, normalize); player prefs round trip and removal of the retired engine keys |
| `data/prefs/BehaviourRepositoryTest.kt` | every Behaviour field under today's keys; last screen writes one key; unknown enum name → default |
| `data/prefs/ProcessStoresRepositoryTest.kt` | style shelf and Customize groups persistence; director switch and reach under today's keys; keep set read from `geode-mod` |
| `data/GuiSettingsCodecsTest.kt` | legacy key migrations of `AppState` and `GeneralSettings` (intent, tour-seen, reduced motion, PiP, white font); the codecs share one file without overwriting each other |
| `data/TouchAndDreamPrefsTest.kt` | touch and dream keys and defaults |
| `data/WatermarkPrefsCodecTest.kt`, `data/StudioPrefsStoreTest.kt`, `data/RecordPrefsStoreTest.kt`, `data/RecordSaveStoreTest.kt`, `data/LibraryPrefsStoreTest.kt` | each store's keys, defaults and fallbacks |
| `data/PlayerPrefsCrossfadeTest.kt` | `crossfadeMsFromSeconds` rounding and bounds |
| `data/ExportFpsTest.kt` | `TemplateExport.resolvedFps` (the function's only caller) |
| `data/ExportPresetFillTest.kt` | `filledFrom` sets only quality, fps, ratio, loop; every platform preset is an offered combination |
| `data/backup/BackupArchiveTest.kt` | **saved format:** manifest round trip, pack/unpack, newer version, wrong format id, zip-slip, allowlist → section, checksum, caps, migration chain, typed prefs JSON, device-local keys, macro bank rides with Creative |
| `data/BackupRulesCoverageTest.kt` | Android auto-backup XML rules (`backup_rules.xml`, `data_extraction_rules.xml`) |
| `data/MusicPlaylistStoreTest.kt` | legacy file-name migration happens on first use, not in the constructor |
| `data/EditorProjectStoreFoldersTest.kt` | project folder per id; delete is scoped |
| `data/FavouritesRepositoryTest.kt` | toggle saved under the URI; external write reloads; newest first |
| `data/ByteBudgetCacheTest.kt`, `data/ReadCapTest.kt`, `data/PendingPublishTest.kt`, `data/MinTrackLengthTest.kt`, `data/DeviceTrackTagTest.kt`, `data/NotificationButtonTest.kt`, `data/SystemMotionTest.kt`, `data/TemplateStyleGateTest.kt` | helper behaviour (cache accounting, caps, publish retry, min length, tag fallback, button room, reduced motion = scale 0, template → Pro gate) |
| `ui/SavedPalettesTest.kt`, `playback/PlayRecorderTest.kt`, `playback/ResumeSourceTest.kt` | use `PaletteStore`, `HistoryStore`, `SessionStore` through their callers |

**Saved formats with no test on their text** (Grep of `app/src/test` and `app/src/androidTest` for the class names): the `lfos` / `adsrs` JSON and the `rateHz` migration (`LfoStore` appears only in `PrefsSchemaTest` for key names); `EditorProjectJson` (project.json); `TemplateFormat` round trip and unknown-key carry (only `SharedLinkNumbersTest` touches it); palette JSON; `auto_playlist_entries`; `library.json`; `history.json`; `session.json` versions; `PlaylistFormats`; LRC parsing; `MilkTextureLinks`; `texture-links.json`.

### 8. Surprises

- `TextureStore.generateDisplayPreset` (`data/TextureStore.kt:173-214`) is a shader and a MilkDrop preset written from a Kotlin string in a "store".
- `BundledTemplates` (`data/TemplateRepository.kt:163-501`) holds six looks as Kotlin literals although bundled presets moved to native.
- `AutoDirectorStore` is not only storage: it makes the play seed, sends `soundTrackBoundary` to the sound engines and applies the free-tier amount (`data/AutoDirectorStore.kt:108-148`).
- Reads with side effects: `LfoStore.loadHandMask` deletes `reactions` and `reactions_hand` when the routes no longer decode (`data/LfoStore.kt:75-80`); `MacroBankCodec.read` removes the retired keys (`data/MacroStore.kt:291-293`); `PerformanceStore.load` writes the first-run pick (`data/PerformanceStore.kt:40`); `EditorProjectStore(context)` deletes three legacy directories (`data/EditorProjectStore.kt:45`, `:118-122`).
- `LookStore.current()` builds an `LfoStore` whose default decoders are JNI calls, so native reaction decoding runs on the Cast executor and on the wallpaper/dream main thread (`data/LookStore.kt:29`, `:36`; `data/LfoStore.kt:27-28`).
- `PresetStore.fromJson` (schema walk, clamps) runs on the main thread at start through `VizStateStore.restore` (`data/VizStateStore.kt:20`, `:59`), and `BuiltInPresets.ALL` makes its JNI call there too (`:43`).
- Preset files have no version field; compatibility rests on param names in `core/viz/ParamTable.inc` and on `PresetGoldenTest`.
- The backup format id is still `muzviz-backup` (`data/backup/BackupFormat.kt:112`).
- `VizUiState` takes its default style from the Pro package (`pro/FreeTier.FIRST_STYLE_ID`, `data/VizUiState.kt:10`).
- `BuiltInPresets.isBuiltIn` decides by the name containing `" · "` (`data/BuiltInPresets.kt:21`): a user preset with that text in its name counts as built-in.
- `AutoVisualsPrefsStore.applyTo` always restores the timer as off and removes the legacy key on the next save (`data/AutoVisualsPrefsStore.kt:31`, `:45`).
- The template's layout, text and export sections are stored and round-tripped but not rendered (`data/VideoTemplate.kt:9-10`).
- `project-templates/<id>/template.json` has a reader (`data/EditorProjectJson.kt:86`) and no writer in this scope.
- `HistoryStore` reads `history.json` in its constructor on the constructing thread (`data/HistoryStore.kt:28-30`); `VizStateStore.flushIfDirty` does a `commit()` on the caller's thread (`data/VizStateStore.kt:74-76`, `:88`).
- Dead code (Grep over `app/src` and `engine`): `TrackLibrary.remove` (`data/TrackLibrary.kt:39`) has no caller; `EditorProjectJson.toJson` / `fromJson` (`data/EditorProjectJson.kt:50-52`) have no production caller; `TemplateExport.resolvedFps` (`data/VideoTemplate.kt:383`) is called only by `ExportFpsTest`.

### Not verified

- Test bodies: I read `PresetGoldenTest.kt:1-60` and the test names of the other files; the "what it pins" column for those rests on names and imports.
- Threads of `MilkTextureLinks`, `TextureStore`, `PaletteStore` file calls: their callers are in `ui/` (`MilkImportController`, `TextureController`, `TrackColorController`) and were not traced.
- The dispatcher of the container scope that `AutoDirectorStore`, `MacroStore`, `NoteResponseStore` launch in (`GeodeContainer`, outside this scope).
- Graphify was not used; caller checks were done with Grep.
- The native owners of TagLib and of the watermark codes were not located in `core/`.
- Keys of `geode-audiofx`, `geode-mod` (other than `locked_params`), `geode-wallpaper`, `geode-widget`, `geode-pro` and `MilkPresetMemory.KEY`'s value are owned outside this scope.


---

## 06: root package, `playback/`, `model/`, `pro/`, `billing/`, `di/`, `ui/theme/`, `ui/tour/`, `ui/pro/`

90 Kotlin files, 9,637 lines, under `app/src/main/java/dev/geode/` (paths below are relative to that directory unless they start with `app/`, `engine/` or `core/`). Read in full with comments: root (7), `playback/` (21), `pro/` (8), `di/` (4), `model/` (19). Read as code with comments and imports stripped: `billing/` (8), `ui/theme/` (14 of 15), `ui/tour/` (4 of 5), `ui/pro/PaywallViewModel.kt`, `ui/pro/ProComponents.kt`. Read as an outline plus a keyword scan only (not every line): `ui/theme/MaterialSymbols.kt` (icon path strings), `ui/tour/TourOverlay.kt`, `ui/pro/PaywallScreen.kt` (lines 752-815 read in full). To trace the audio path I also read, outside my scope: `audio/TapRenderersFactory.kt`, `audio/PlayoutSink.kt`, `audio/dsp/MvzAudioProcessorChain.kt`, `audio/dsp/NativeDspProcessor.kt`, `audio/AudioBus.kt`, `engine/audio-android/.../PcmTap.kt`, `data/AutoDirectorStore.kt`; `audio/OboeAudioOutput.kt` and `ui/PlaybackFades.kt` by grep only.

Class counts: shell 80, mixed 6, engine 2, duplicate 2 (one of them also dead in production).

### 1. Per-file table

Columns: File (lines) | Class | Engine logic | Thread | Native calls | Native owner | What must stay Kotlin.

#### Root package (7)

| File | Class | Engine logic | Thread | Native calls | Native owner | Stays Kotlin |
|---|---|---|---|---|---|---|
| `AppLaunch.kt` (16) | shell | none | main | none | n/a | `PackageManager` launch intent |
| `ExportTimeLimit.kt` (45) | shell | none; a 30 s mark that words a cancelled render (`:21`, `:41-44`) | service main, UI | none | n/a | `Service.onTimeout` bookkeeping |
| `ForegroundStart.kt` (19) | shell | none | main | none | n/a | `startForeground` wrapper |
| `GeodeApp.kt` (55) | shell | none | main | none direct (`MemoryTrim.onTrim` `:53`) | n/a | `Application`, crash handler `:23-34` |
| `GeodeContainer.kt` (255) | shell | none; builds the settings repositories and the stores that push engine configuration (`macros` `:71`, `autoDirector` `:73-76`, `noteResponse` `:78`); sweeps scratch files and pending MediaStore rows (`:149-212`) | main; sweeps on `Dispatchers.IO` | none direct | n/a | SharedPreferences, MediaStore, `cacheDir` |
| `RingLog.kt` (28) | shell | none | any (synchronized) | none | n/a | log ring for the crash file |
| `ServiceWakeLock.kt` (29) | shell | none; `LONG_WORK_WAKE_MS` 6 h `:29` | service main | none | n/a | `PowerManager` |

#### `playback/` (21)

| File | Class | Engine logic | Thread | Native calls | Native owner | Stays Kotlin |
|---|---|---|---|---|---|---|
| `AutoDirectorFeed.kt` (77) | mixed | `startTicker` `:55-67`: a 1 Hz coroutine ticker (`TICK_MS` `:75`) that feeds the player position into `AutoDirectorStore.onPosition`, which decides the director amount (0 or 1) through `pro/AutoDirectorPolicy.amount`. It is a timer that decides when the picture stops being directed. `onMediaItemTransition` `:39-45` triggers a new seed and the sound track boundary. | main (`Dispatchers.Main.immediate`, session scope) | none direct; through `data/AutoDirectorStore`: `GeodeNative.directorSet` (`data/AutoDirectorStore.kt:147`), `soundTrackBoundary` (`:112`) | `core/viz/Director` (already holds `kFreeTasteMs`, `core/viz/Director.hpp:42`, and `director::forExport`); new: a live "position + entitled + playing" input so native cuts the taste itself | `Player.Listener` callbacks, `player.currentPosition` |
| `BitPerfectOutput.kt` (156) | shell | none about sound; `applyOn34` `:108-127` picks the DAC mixer entry that equals the track format (a platform match), `onActive` tells native to bypass | playback thread (`onOutput` `:91`), main (`setEnabled` `:67`) | none direct; `dsp.bitPerfect` setter calls `GeodeNative.dspSetBitPerfect` (`audio/dsp/NativeDspProcessor.kt:79`) | `core/audio/dsp/DspProcessor` (bypass) | `AudioManager.setPreferredMixerAttributes`, `ExoPlayer.setPreferredAudioDevice`: no NDK form |
| `HeadphoneAutoplay.kt` (65) | shell | none (app rule: play when a headphone type connects, `:34-44`) | main (`Handler(mainLooper)` `:27`) | none | n/a | `AudioDeviceCallback` |
| `LibraryTree.kt` (282) | shell (app logic) | not audio or visual. `similarTo` `:83-99` (same artist/album or playlist mate, shuffled, 50), `groups` `:188-192` (sort by lowercase), `buildTracks` `:201-207` (merge) are Kotlin library rules with no native twin. Search is native. | `geode-resumption` executor; `Dispatchers.IO` for `similarTo` | `GeodeNative.librarySearch` `:126` | `core/library/Search` (exists) for search; nothing for similar/grouping | Media3 `MediaItem` building, MediaStore, stores |
| `MediaArtwork.kt` (59) | shell | none (bitmap sample-size loop `:53-54`) | `geode-art` | none | n/a | `MediaMetadataRetriever`, `BitmapFactory` |
| `NotificationButtons.kt` (161) | shell | none (repeat-mode cycle `:86-91`) | main | none | n/a | Media3 session commands |
| `PlayRecorder.kt` (217) | shell | none (saves queue and position; position tick) | main; disk write on `geode-session` | none | n/a | Media3 listener, stores |
| `PlaybackEngine.kt` (301) | mixed | `applyPrefs` `:184-202`: pitch ratio from semitones in Kotlin (`PlaybackMath.semitonesToRatio`, `engine/scenes/src/main/kotlin/dev/geode/analysis/PlaybackMath.kt:7`, `2^(st/12)`) `:185`; the native-output rule `oboe = nativeOutput && !(bitPerfect && supported)` `:191`; reopening the output by seeking in place `:196-198`. `rampUiFade` `:178-181` picks the target (0 dB or -inf). The chain order is fixed in `audio/dsp/MvzAudioProcessorChain.kt:19-20`. Everything else is wiring. | main; `geode-session` executor for the session file | `playoutRingFrames`, `playoutRingRunway` `:42`; `pcmRingSetLayer` `:63`; `dspSleepFade` `:122`; `pcmRingSetPlayout` `:158`; `dspRampTo` `:181`; `dspSetSpeed` `:189`; `playoutSetOffsetMs` `:190`; `playoutCreate` `:243`; `playoutLagMs` `:250` | `core/audio/dsp` (GainStage, DspProcessor), `core/sound/interp/PlayoutClock`, `core/audio/ingest/PcmRing`, `LayerMix`; new: semitone-to-ratio in `core/audio/dsp` | ExoPlayer build, Media3 listeners, `AudioAttributes`, wake mode |
| `PlaybackErrorRecovery.kt` (55) | shell (app logic) | none audio; skips or pauses on error | main | none | n/a | `Player.Listener` |
| `PlaybackErrors.kt` (68) | shell (app logic) | `decide` `:16-24` (3 failures stop); `describe` holds English strings in code `:26-67` | main | none | no twin | Media3 error codes |
| `PlaybackQueue.kt` (55) | shell (app logic) | `around` `:31-38` (window of 1001 around the tap), `PREV_RESTART_MS` 3 s `:8` | main, resumption executor | none | no twin in `core/library/` | none strictly; pure Kotlin |
| `PlaybackService.kt` (605) | shell | none; `LibraryAccess.mayBrowse` `:601-604` is a security rule; `pageOf` `:254-262` | main; `geode-resumption` executor `:463-466` | none | n/a | `MediaLibraryService`, notifications, `PackageManager` |
| `QueueEnd.kt` (63) | shell (app logic) | at queue end: stop, repeat or play similar `:28-38` | main; IO for the lookup | none | no twin | `Player.Listener` |
| `QueueOps.kt` (31) | shell (app logic) | `playOrder` `:12-25`, `insertNextIndex` `:4-10` | main | none | no twin | pure Kotlin |
| `ReplayGain.kt` (92) | shell | none: reads tags of current, next and previous item and hands them over; the mask packing `:67-71`; mode constants `:76-78`. The gain rule is native. | main; tag read on `Dispatchers.IO` | `dspSetReplayGain` `:41`, `dspSetStreamTags` `:71` | `core/audio/dsp/ReplayGain.hpp`, `GainStage` | `ContentResolver` open, Media3 timeline |
| `ResumeSource.kt` (35) | shell (app logic) | clamps saved index and position `:25-33` | main, resumption executor | none | no twin | pure Kotlin |
| `SessionBitmapLoader.kt` (96) | shell | none | `geode-art` executor `:22` | none | n/a | Media3 `BitmapLoader` |
| `SleepTimer.kt` (126) | mixed | `start` `:35-70`: the countdown, the report interval (`fade(remaining)` answer clamped to 1..500 ms `:50-51`), the wait of `PlaybackEngine.sinkLagMs()` before the pause `:58`, and the pause itself `:60` are Kotlin. The fade curve is native. | main (session scope) | through the `fade` lambda: `GeodeNative.dspSleepFade` (`playback/PlaybackEngine.kt:122`) | `core/audio/dsp/GainStage`; new: the countdown could live there and report "pause now" | `player.pause()`, `Player.Listener` for finish-track |
| `TrackBookmarks.kt` (46) | shell | none (save every 5 s for long tracks) | main (UI poll) | none | n/a | store, `player.seekTo` |
| `TrackCrossfade.kt` (52) | shell | none on samples; clamps `crossfadeMs >= 0` `:27`; `pushFollowsOn` `:39-40` says whether an item follows (next item or repeat-one) | main | `dspSetCrossfade` `:28`, `dspSetFollowsOn` `:40` | `core/audio/dsp/GainStage` | Media3 owns the queue, so "does something follow" is only known here |
| `VoiceSearch.kt` (44) | shell | none (reads Media3 request fields) | resumption executor | none | `core/library/Search` does the match | Media3 `requestMetadata`, `MediaStore` extras |

#### `model/` (19)

| File | Class | Engine logic | Thread | Native calls | Native owner | Stays Kotlin |
|---|---|---|---|---|---|---|
| `ControlRange.kt` (48) | engine | `CAPS` `:22-32` (brightness 1.25, intensity 1.25, bloom 0.25, camFly 2) and `FLOORS` `:35` (camFly -2), and `of` `:44-47`, which intersects them with the native schema range. A range of a visual parameter held in Kotlin. | main (Compose) | none direct; reads `ParamSchema` (engine/scenes, native-backed) | new: a "control min/max" column in `core/viz/ParamTable.inc` | nothing |
| `DeviceTrack.kt` (11) | shell | none | n/a | none | n/a | data class |
| `KnobSize.kt` (13) | shell | none | n/a | none | n/a | UI pref enum |
| `LibraryStartTab.kt` (19) | shell | none | n/a | none | n/a | UI pref enum |
| `LibraryTrack.kt` (15) | shell | none | n/a | none | n/a | data class |
| `LyricLine.kt` (6) | shell | none | n/a | none | n/a | data class |
| `MilkFile.kt` (6) | shell | none | n/a | none | n/a | data class |
| `OpenedAudio.kt` (139) | shell | none (intent routing and refusal rules `:43-85`, name cleaning `:92-119`) | main, IO | none | n/a | intent and provider security |
| `PresetLink.kt` (76) | shell (data format) | none; gzip + base64url link codec with caps `:13-17` | main | none | n/a (see section 7) | could be native, nothing forces it |
| `PresetShelf.kt` (116) | shell | none; list filter, order and naming rules for the Presets page `:40-116` | main | none | n/a | UI list logic |
| `QueueTrack.kt` (12) | shell | none | n/a | none | n/a | data classes |
| `ScreenRotation.kt` (17) | shell | none | n/a | none | n/a | UI pref enum |
| `StyleLook.kt` (28) | duplicate, dead in production | mirrors `enum class Look` at `core/viz/Catalogue.hpp:57` (same nine entries, upper snake case). No production code names `StyleLook.` (Grep: only comments in `engine/scenes/.../render/scene/Catalogue.kt:13,125` and `StyleLookTest`). | n/a | none | `core/viz/Catalogue.hpp:57` | nothing |
| `StyleShelf.kt` (43) | shell | none; favourites and recents rules, `RECENT_LIMIT` 12 `:35` | main | none | n/a | UI shelf |
| `TabLabels.kt` (30) | shell | none | n/a | none | n/a | UI pref enum |
| `Tour.kt` (304) | shell | none; the tour's pure rules and card placement `:144-296` | main | none | n/a | UI onboarding |
| `TrackTap.kt` (21) | shell | none | n/a | none | n/a | UI pref enum |
| `VizPlaylistEntry.kt` (8) | shell | none (a visuals playlist row: `sceneId`, preset, milk path) | n/a | none | n/a | data class; it names a scene but decides nothing |
| `WatermarkChoice.kt` (36) | duplicate (mirror) | integer codes `:17-27` copy `GEODE_WATERMARK_*` at `core/api/geode_api.h:696-707` (TILED 1, CORNER 2, END_CARD 3, corners 0-3, USE_* 0-3). Defaults `placement = TILED`, `corner = BOTTOM_RIGHT` `:12-13` are Kotlin. | n/a | none | `core/api/geode_api.h:696-707`, `geode_export_watermark_placement` | the data class as a carrier |

#### `pro/` (8)

| File | Class | Engine logic | Thread | Native calls | Native owner | Stays Kotlin |
|---|---|---|---|---|---|---|
| `AutoDirectorPolicy.kt` (34) | engine | `amount` `:19-30`: director drive is 0 when off, 1 when entitled, 1 for free only while the player plays and position < 60 s, else 0. `isTaste` `:33` has no caller (dead). | main | none (result goes to `GeodeNative.directorSet`) | `core/viz/Director.hpp:40-52` already has the same 60 s (`kFreeTasteMs`) and the export twin `director::forExport`; the live rule is the Kotlin half | nothing; the entitlement is an input |
| `BillingRepository.kt` (113) | shell | none; product id `:44` | n/a | none | n/a | billing interface |
| `FeatureGate.kt` (87) | mixed | `allowsStyle` `:35` (which scene a user may keep); `blockingExportFeature` `:41-49` (short side > 1080, fps > 30); `exportCapMs` `:55`; `watermarksExports` `:52`; `renderLimits` `:62`. The entitlement switch `allows` `:28-33` is Pro policy. | main, GL-free | none | new: a native "limits" input (entitled flags in, caps applied in `core/output`); the watermark placement is already native | the entitlement itself |
| `FreeTier.kt` (102) | mixed | every rule that picks or limits a visual: see the list under section 8. | n/a | none; reads `StyleFamilies` and `SceneCapabilities.SHADER_SCENES` (engine/scenes) | new: a free flag per style in `core/viz/Catalogue`; `core/viz/Director` for the taste | trial length, product policy constants |
| `GetFeatureGateUseCase.kt` (28) | shell | none | main | none | n/a | Flow combine |
| `PaywallNavigator.kt` (48) | shell | none | main | none | n/a | UI state |
| `ProFeature.kt` (59) | shell | none | n/a | none | n/a | enum |
| `TrialRepository.kt` (73) | shell | none; trial clock rules `:45-71` | main | none | n/a | Pro policy |

#### `billing/` (8)

| File | Class | Engine logic | Thread | Native calls | Native owner | Stays Kotlin |
|---|---|---|---|---|---|---|
| `BillingBuild.kt` (17) | shell | none | n/a | none | n/a | BuildConfig |
| `EntitlementStore.kt` (205) | shell | none; cache and revoke-grace rules `:61-78` | main | none | n/a | prefs |
| `FakeBillingRepository.kt` (119) | shell | none | main | none | n/a | debug billing |
| `ForegroundActivityTracker.kt` (48) | shell | none | main | none | n/a | lifecycle callbacks |
| `PlayBillingRepository.kt` (299) | shell | none | main (`Dispatchers.Main.immediate` scope `:62`) | none | n/a | Play Billing IPC |
| `ProSnapshot.kt` (46) | shell | none itself; `animateUntilMs` `:38-45` returns the wall-clock time until which the wallpaper may animate (0, trial end, or forever). The caller (`ui/wallpaper/WallpaperOptionsActivity.kt:85`) hands it on; a run rule for a visual surface is derived from it. | any | none | n/a | prefs read, signature check |
| `PurchaseVerifier.kt` (85) | shell | none | any | none | n/a | `java.security` |
| `StoredTrialRepository.kt` (129) | shell | none; re-check loop every 60 s while a trial runs `:91-96` | collector's | none | n/a | prefs |

#### `di/` (4)

| File | Class | Engine logic | Thread | Native calls | Native owner | Stays Kotlin |
|---|---|---|---|---|---|---|
| `BillingModule.kt` (65) | shell | none | n/a | none | n/a | Hilt |
| `DataModule.kt` (50) | shell | none | n/a | none | n/a | Hilt |
| `MemoryTrim.kt` (26) | shell | none; filters to levels 20 and 40 `:18-22` before calling native, which the comment says native also decides | main | `GeodeNative.memoryTrim` `:23` | `core/viz/SceneResidency` | `onTrimMemory` |
| `PlayerSessionProvider.kt` (111) | mixed | `leaveLockedStyle` `:70-77` selects `FreeTier.FALLBACK_STYLE_ID` when the chosen style is no longer allowed; `chooseUnlockedStyle` `:84-87` selects the style whose choice opened the paywall. Both pick the scene on screen. | main (own scope `:56`) | none direct (`session.selectScene`) | new: a native "allowed styles" mask with the fallback applied in `core/viz` | the entitlement flow, the paywall |

#### `ui/theme/` (15)

| File | Class | Notes |
|---|---|---|
| `EdgeFade.kt` (46), `FloatingGlass.kt` (39), `GlassBlur.kt` (117), `SelectedGlow.kt` (58), `StonePress.kt` (55), `Wordmark.kt` (39) | shell | Compose modifiers and drawing |
| `GeodeMotionScheme.kt` (122) | shell | UI animation specs; a `ContentObserver` on `Handler(mainLooper)` for the system animator scale `:107-114` |
| `GeodeTheme.kt` (155) | shell | UI colour scheme; calls `ColorDerive.scaleSaturation` `:29` for accent intensity; contrast maths `:142-149` |
| `GeodeTokens.kt` (237) | shell | UI tokens |
| `MaterialSymbols.kt` (372) | shell | icon path strings only |
| `StoneHapticCue.kt` (48), `StoneIcon.kt` (120), `Television.kt` (55) | shell | haptics, icons, TV ripple |
| `UiSounds.kt` (86) | shell | UI tap sounds through `SoundPool` `:46-57`, volume 0.35 `:86`. A second audio output in Kotlin, but sonification, not the music path. Reads a system setting on `Dispatchers.IO` `:77`. |
| `VisualAccent.kt` (160) | shell | derives the UI accent colour from a colour the visual hands over (`from` `:67-85`, binary search on lightness, HSL maths `:133-160`). It colours the UI, not the picture. |

No `GeodeNative` call, no thread and no engine logic in `ui/theme/`.

#### `ui/tour/` (5) and `ui/pro/` (3)

| File | Class | Notes |
|---|---|---|
| `ui/tour/TourViewModel.kt` (277) | shell | ViewModel UI state; `viewModelScope`; delays 400 ms, 600 ms, 1 s `:272-274` |
| `ui/tour/TourHost.kt` (133), `TourOverlay.kt` (391), `TourTargets.kt` (97), `TourWords.kt` (71) | shell | Compose; `TourWords.fits` `:62-71` reads gesture prefs |
| `ui/pro/PaywallViewModel.kt` (181) | shell | billing UI state |
| `ui/pro/ProComponents.kt` (97) | shell | Compose |
| `ui/pro/PaywallScreen.kt` (856) | shell | Compose. `VisualHero` `:757-813` draws a fake visualizer (56 bars and three blobs from `sin`/`cos` of a phase) in a Compose `onDrawBehind` every frame while the paywall is open. See section 8. |

### 2. Threads started here

| Where | Name | What runs | How often |
|---|---|---|---|
| `GeodeContainer.kt:48` | `appScope`, `SupervisorJob + Dispatchers.Main.immediate` | settings repositories, engine-config stores | process lifetime |
| `GeodeContainer.kt:110,131,150,174` | `appScope.launch(Dispatchers.IO)` | analysis-cache sweep, prefs warm-up, scratch sweep, pending-row sweep | once at process start |
| `playback/PlaybackEngine.kt:115` | `PlaybackSession.scope`, Main.immediate | ReplayGain reads, sleep timer, director feed, play recorder | session lifetime |
| `playback/PlaybackEngine.kt:230-233` | `geode-session` single daemon thread | `SessionStore.save` | on every transition, timeline change, seek, pause, and every `SessionStore.POSITION_WRITE_INTERVAL_MS` while playing |
| `playback/PlaybackService.kt:48` | service scope, Main.immediate | notification buttons, queue end | service lifetime |
| `playback/PlaybackService.kt:463-466` | `geode-resumption` single daemon thread | library browse, search, resumption, queue building | per browser request |
| `playback/SessionBitmapLoader.kt:22` | `geode-art` single thread (not daemon) | artwork decode | per notification artwork request |
| `playback/HeadphoneAutoplay.kt:27` | `Handler(mainLooper)` | audio device callback | on device connect |
| `playback/AutoDirectorFeed.kt:57-66` | coroutine ticker on session scope | `store.onPosition(player.currentPosition, gate)` | every 1,000 ms while playing |
| `playback/PlayRecorder.kt:137-143` | coroutine ticker on session scope | `saveNow()` | every `SessionStore.POSITION_WRITE_INTERVAL_MS` while playing |
| `playback/SleepTimer.kt:43-67` | coroutine on session scope | countdown, native fade report, pause | every 1..500 ms while a timer runs |
| `playback/ReplayGain.kt:63-64` | `scope.launch` + `withContext(Dispatchers.IO)` | tag read | up to three items per timeline or item change |
| `playback/QueueEnd.kt:54-55` | `scope.launch` + IO | similar-tracks lookup | at queue end |
| `billing/PlayBillingRepository.kt:62` | scope, Main.immediate | purchase handling | on billing events |
| `billing/StoredTrialRepository.kt:91-96` | collector's coroutine | trial re-read | every 60 s while a trial is active |
| `di/PlayerSessionProvider.kt:56` | scope, Main.immediate, never cancelled | entitlement collection, style fallback | per gate emission |
| `ui/theme/GeodeMotionScheme.kt:109` | `ContentObserver(Handler(mainLooper))` | reduced-motion read | on setting change |
| `ui/theme/UiSounds.kt:77` | `withContext(Dispatchers.IO)` | one system-setting read | once per composition of the player |
| `ui/tour/TourViewModel.kt:82,88,243,257` | `viewModelScope` | tour state | UI events |
| `ui/pro/PaywallViewModel.kt:115,117` | `viewModelScope` | billing refresh, trial-ended notice | UI events |
| `ui/pro/PaywallScreen.kt:455-458` | `produceState` loop | trial minutes left | every 15 s |
| ExoPlayer's playback thread (built at `playback/PlaybackEngine.kt:79-111`) | Media3 internal | the whole audio chain, see section 4 | per audio buffer |

### 3. The audio path, from Media3 to the ring and the output

1. **Player build.** `PlaybackSession` builds one `ExoPlayer` with `TapRenderersFactory` (`playback/PlaybackEngine.kt:79-111`): usage media, content music, audio focus handled (`:103-109`), `WAKE_MODE_LOCAL` (`:110`), default extractors plus `AiffExtractor` (`:93-102`). Kotlin decides: these attributes. Native decides: nothing here.
2. **Rings.** The player ring is native, sized by native: `SampleRing(GeodeNative.playoutRingFrames(), 2, GeodeNative.playoutRingRunway())` (`:41-42`). The mic ring is `1 shl 15` frames, mono, a Kotlin constant (`:62`), layered natively with `pcmRingSetLayer` (`:63`). `AudioBus.soundInput = sampleRing` publishes the ring to the wallpaper and dream (`:159`).
3. **Processor chain order.** `audio/TapRenderersFactory.kt:44` builds `MvzAudioProcessorChain(TeeAudioProcessor(sink), dsp)`; the order is fixed in Kotlin at `audio/dsp/MvzAudioProcessorChain.kt:19-20`: tap, `SilenceSkippingAudioProcessor`, `SonicAudioProcessor`, then `NativeDspProcessor`.
4. **Tap.** `PcmTap.handleBuffer` (`engine/audio-android/src/main/kotlin/dev/geode/engine/audioandroid/PcmTap.kt:41-44`) hands each buffer to the native ring with its encoding, channel count and rate; `flush` starts a ring epoch (`:29-39`). Kotlin decides: whether the tap writes at all (`owns`, `:27`, `:42`) and the heap-to-direct staging copy (`:47-54`). Native decides: conversion to float and the ring layout. The tap is before Sonic, so the ring holds the track at its original speed and pitch.
5. **Skip silence, speed, pitch.** These two processors are Media3's Java DSP on the playback thread, not native. Kotlin sets them: `player.playbackParameters = PlaybackParameters(p.speed, semitonesToRatio(p.pitchSemitones))` (`playback/PlaybackEngine.kt:185`), `player.skipSilenceEnabled` (`:186`), applied to Sonic at `audio/dsp/MvzAudioProcessorChain.kt:24-33`. The pitch ratio formula is Kotlin (`PlaybackMath.kt:7`). Native is only told the speed: `dspSetSpeed` (`playback/PlaybackEngine.kt:189`) for the gain timing and `playoutSpeed` (`audio/PlayoutSink.kt:68`) for the clock.
6. **Native DSP.** `NativeDspProcessor.queueInput` (`audio/dsp/NativeDspProcessor.kt:102-116`) copies the block once and calls `GeodeNative.dspProcess` in place. Kotlin decides: whether the chain runs on a format (`runs`, 16-bit or float and 1..2 channels, `:169-172`); the stream name (the item URI, `:175`). Native decides: equalizer, bass, loudness, crossfeed, limiter, every gain.
7. **Sink and output.** `DefaultAudioSink` is built at `audio/TapRenderersFactory.kt:51-66`. For each output Kotlin asks `nativeOutput()` and opens `OboeAudioOutput` if it plays the config, else the platform AudioTrack (`:60-64`). The rule that sets `nativeOutput` is Kotlin: on, and not bit-perfect (`playback/PlaybackEngine.kt:191`). The Oboe stream itself is native (`GeodeNative.audioOut*`, `audio/OboeAudioOutput.kt:38-173`).
8. **Playout clock.** `PlayoutSink` (`audio/PlayoutSink.kt:25-69`) reports each handled buffer (`playoutBufferHandled`, `:33`), the position (`playoutPosition`, `:40`), play, pause, flush and speed to the native clock. Kotlin decides nothing. `pcmRingSetPlayout` binds ring and clock (`playback/PlaybackEngine.kt:158`). The picture delay preference goes across as a value: `playoutSetOffsetMs(playout, p.pictureDelayMs)` (`:190`).
9. **ReplayGain.** Kotlin reads tags for the current, next and previous items on IO and sends the four values with a presence mask (`playback/ReplayGain.kt:51-73`); mode, preamp and clip guard go across unchanged (`:37-41`). Native decides the gain and the sample it applies from (`core/audio/dsp/ReplayGain.hpp`, `GainStage`). Kotlin keeps a 16-item "already asked" set (`:81`) under the native table's 32.
10. **Crossfade.** Kotlin sends the length (clamped to >= 0, `playback/TrackCrossfade.kt:27-28`) and whether something follows the current item (`:39-40`). Native runs the fade and tells a follow-on from a skip.
11. **UI fades.** `rampUiFade` sends a target (0 dB or -inf) and a length (`playback/PlaybackEngine.kt:178-181`). The lengths and when to pause after a fade are decided in `ui/PlaybackFades.kt` (outside my scope; it waits `durationMs + PlaybackEngine.sinkLagMs()` at `:49`). The curve is native.
12. **Sleep timer.** Kotlin owns the countdown, the pause and the wait for the sink lag (`playback/SleepTimer.kt:45-60`). Native owns the fade over the last seconds and tells Kotlin how soon to report again (`:50`).
13. **Bit-perfect.** Kotlin picks the DAC mixer entry and tells native to pass samples through (`playback/BitPerfectOutput.kt:99-103`, `playback/PlaybackEngine.kt:53`). Native latches the bypass at the next flush.
14. **Tickers that feed native.** `AutoDirectorFeed` 1 Hz (position, playing, entitlement, giving `directorSet(on, amount, seed)`); `SleepTimer` up to 2 Hz or faster (`dspSleepFade`). Track boundaries: `AutoDirectorStore.newPlay` calls `soundTrackBoundary(seed)` with a Kotlin `Random.nextLong()` seed (`data/AutoDirectorStore.kt:109-112`). There is no Kotlin sound-feed ticker in my scope; renderers read the ring on their GL threads.

Queue, shuffle and library search (app logic, not audio-to-visual): `PlaybackQueue`, `QueueOps`, `QueueEnd`, `ResumeSource`, `PlaybackErrors`, `LibraryTree.similarTo` are Kotlin. `core/library/` holds only `Search.{hpp,cpp}`, `SearchFold.inc` and `Tags.{hpp,cpp}`: search and tag reading have native twins (`GeodeNative.librarySearch`, `playback/LibraryTree.kt:126`; `NativeTags.read`, `playback/ReplayGain.kt:64`). Queue windowing, play order, queue-end, similar-tracks, error recovery and resumption have no native twin (Grep for queue, shuffle and play order in `core/library` and `core/api` found nothing). Shuffle order itself is Media3's.

### 4. Per-frame and per-audio-block Kotlin paths

Per audio buffer, on ExoPlayer's playback thread, all reached from the player built at `playback/PlaybackEngine.kt:79-111`:

- `DefaultAudioSink` → `TeeAudioProcessor` → `PcmTap.handleBuffer` (`engine/audio-android/.../PcmTap.kt:41`) → `SampleRing.write` → `GeodeNative.pcmRingWrite*`.
- → `SilenceSkippingAudioProcessor` and `SonicAudioProcessor` (Media3 Java DSP).
- → `NativeDspProcessor.queueInput` (`audio/dsp/NativeDspProcessor.kt:102`) → `GeodeNative.dspProcess`.
- `PlayoutSink.handleBuffer` (`audio/PlayoutSink.kt:25`) → two `pcmRingHead` calls and `playoutBufferHandled`; `getCurrentPositionUs` (`:37`) → `playoutPosition`.
- `OboeAudioOutput.write` (`audio/OboeAudioOutput.kt:42-48`) → `audioOutEvents`, `audioOutWrite`, when the native output is on.
- Per stream, not per buffer: `NativeDspProcessor.onFlush` (`:118-124`), `PcmTap.flush`, `BitPerfectOutput.onOutput` (`playback/BitPerfectOutput.kt:91`).

That is four to six JNI calls per audio buffer from Kotlin on the playback thread.

Per video frame: none in my scope on a GL thread. On the main thread, `ui/pro/PaywallScreen.kt:782-810` (`VisualHero` draw block) runs every display frame while the paywall is open and motion is not reduced.

### 5. Duplicated drivers

- **Position tickers.** Three separate loops read `player.currentPosition` on the main thread: `AutoDirectorFeed` 1 Hz (`playback/AutoDirectorFeed.kt:57-66`), `PlayRecorder` (`playback/PlayRecorder.kt:137-143`), and the UI poll in `ui/PlayerSession.kt:975-986` (seen, not in scope), which also drives `TrackBookmarks.onPoll`.
- **Entitlement readers.** The gate is computed twice: as a flow (`pro/GetFeatureGateUseCase.kt:18-27`) and as a synchronous prefs read (`billing/ProSnapshot.kt:24-31`). Callers of the snapshot: `playback/AutoDirectorFeed.kt:31,43,48,61`, `export/VideoExporter.kt:422`, `export/LoopRender.kt:490`, `cast/CastService.kt:203`, `cast/CastController.kt:117`, `cast/ComputerLink.kt:61`, `ui/wallpaper/WallpaperOptionsActivity.kt:85`.
- **The 60 s free taste.** `pro/FreeTier.kt:88` (live, Kotlin) and `core/viz/Director.hpp:42` (export, native). `AutoDirectorPolicyTest` holds them equal.
- **Two outputs.** AudioTrack and Oboe behind one provider (`audio/TapRenderersFactory.kt:59-65`); the choice rule is Kotlin.
- **"Is playback wanted".** The same three-condition test exists at `playback/PlaybackEngine.kt:204-208` and `playback/SleepTimer.kt:80-83`.
- **Reopen the output by seeking in place.** `playback/PlaybackEngine.kt:196-198` and `playback/BitPerfectOutput.kt:85-87`; one `applyPrefs` call can do both.
- **Memory-trim level filter.** `di/MemoryTrim.kt:18-22` filters levels 20 and 40 before native, which acts on the same two.

### 6. Data crossing the JNI boundary (engine and mixed files)

| From | To native | Back | How often |
|---|---|---|---|
| `playback/PlaybackEngine.kt` | ring size request; ring handles (`Long`) for layer and playout binding; `dspRampTo(handle, dB Float or -inf, ms Int)`; `dspSetSpeed(handle, Float)`; `playoutSetOffsetMs(clock, Int)`; `playoutLagMs(clock, nanoTime Long)` | ring frames and runway (`Int`), clock handle (`Long`), lag ms | on session build, on each prefs change, on each UI fade |
| `playback/SleepTimer.kt` (via lambda) | `dspSleepFade(handle, remainingMs Long)`, `-1` cancels | ms until the next report (`Long`) | every 1..500 ms while a non-finish-track timer runs |
| `playback/AutoDirectorFeed.kt` → `data/AutoDirectorStore.kt` | `directorSet(on Boolean, amount Float, seed Long)` only when the triple changes; `soundTrackBoundary(seed Long)` | `Long` from the boundary call, unused here | 1 Hz evaluation; pushes on change and per track |
| `playback/ReplayGain.kt` | `dspSetReplayGain(handle, mode Int, preampDb Float, clipGuard Boolean)`; `dspSetStreamTags(handle, name String, mask Int, 4 Floats)` | nothing | per prefs change; per item read |
| `playback/TrackCrossfade.kt` | `dspSetCrossfade(handle, ms Int)`; `dspSetFollowsOn(handle, Boolean)` | nothing | per prefs change; per timeline, item, repeat or shuffle event |
| `playback/LibraryTree.kt` | `librarySearch(cells Array<ByteArray?> of rows x 3 UTF-8, request Array<ByteArray?> of 5, out IntArray of rows)` | match count; row indices in `out`, best first | per search or voice request; the whole library crosses each time |
| `di/MemoryTrim.kt` | `memoryTrim(level Int)` | nothing | on trim levels 20 and 40 |
| `model/ControlRange.kt`, `pro/FreeTier.kt` | nothing direct; they read tables engine/scenes loads from native (`ParamSchema`, `StyleFamilies`, `SceneCapabilities.SHADER_SCENES`) | n/a | on UI use |

### 7. Saved formats touched

- Prefs file `GeodePrefsFiles.PRO` (`billing/EntitlementStore.kt:189`): `pro_owned`, `pro_verified_at`, `pro_purchase_json`, `pro_purchase_signature`, `pro_missing_since` (`:190-194`); `trial_started_at`, `trial_end_notice_shown`, `trial_last_seen` (`billing/StoredTrialRepository.kt:61-63`).
- General prefs: `auto_director_enabled`, `auto_director_reach` (`data/AutoDirectorStore.kt:168-169`); modulation prefs `locked_params` (`:156`).
- Player prefs read at session build through `PlayerPrefsStore` (`playback/PlaybackEngine.kt:164`): shuffle, repeat mode, speed, pitch semitones, skip silence, pause on noisy, ReplayGain mode (0 off, 1 track, 2 album: `playback/ReplayGain.kt:76-78`), preamp, clip guard, picture delay, native output, bit-perfect, crossfade ms. The key names are in `data/`, not my scope.
- Session file written through `SessionStore.Saved(tracks, index, positionMs)` (`playback/PlayRecorder.kt:132`), read at `playback/PlaybackService.kt:470`.
- Enum names persisted by name: `KnobSize`, `LibraryStartTab`, `ScreenRotation`, `TabLabels`, `TrackTap` (each `fromName`), `TourChapter`, `TourSpot`, `GestureTip` (`model/Tour.kt:77-81`, with the legacy `WELCOME` chapter name `:116`).
- Watermark codes as integers (`model/WatermarkChoice.kt:17-27`), shared with `core/api/geode_api.h:696-707`.
- Preset share link: `geode://preset/<base64url(gzip(json))>`, at most 8,000 characters, at most 4 MiB inflated (`model/PresetLink.kt:8-17`).
- Preset name rule: `" · "` is replaced by `" - "` (`model/PresetShelf.kt:84`).
- Media ids: browse ids `root`, `tracks`, `albums`, `artists`, `playlists`, `favourites`, `recent`, `search`, `<kind>/<name>`, playable `<parent>|<uri>` (`playback/LibraryTree.kt:264-272`); `geode:no-library` (`playback/PlaybackService.kt:431`); `geode:opened:<uuid>` (`model/OpenedAudio.kt:15`). Android Auto may hold these across sessions.
- Session command ids `dev.geode.notification.SHUFFLE`, `.REPEAT`, `.FAVOURITE` (`playback/NotificationButtons.kt:156-158`); extras key `dev.geode.playback.EMBEDDED_ART_URI` (`playback/MediaArtwork.kt:22`).
- Files: `crash-latest.txt` in `filesDir` (`GeodeApp.kt:25`); scratch prefixes `geode_aac_`, `geode_loop_`, `studio-` (`GeodeContainer.kt:248`); MediaStore folders `Movies/BeatRealms`, `Pictures/BeatRealms`, `Movies/MuzViz`, `Pictures/MuzViz` (`:199-203`); the retired `filesDir/analysis` (`:111`).
- Product id `beatrealms3d_pro` (`pro/BillingRepository.kt:44`); notification channel `geode-playback-refused`, id 4712 (`playback/PlaybackService.kt:543-544`).
- Scene ids used as policy: `SceneIds.NEON_CHROME_SEA`, `GLOW_VORTEX`, `HEX_HIVE`, `BLACK_HOLE`, `MANDELBOX_CATHEDRAL` (`pro/FreeTier.kt:29-33,51`).

### 8. Surprises

**Rules in `pro/` (and around it) that pick or limit a picture, all Kotlin:**

- `FreeTier.familyProIds` (`pro/FreeTier.kt:20`): every family style but the opener is Pro.
- `FreeTier.neonOpener`, `neonProIds` (`:29-33`): four named scenes are Pro.
- `FreeTier.freeStyleIds` (`:45`): free = shader scenes minus the two sets above.
- `FreeTier.FIRST_STYLE_ID` and `FALLBACK_STYLE_ID` (`:51`, `:57`): the scene a fresh install opens on and the scene a free user is moved to.
- `FreeTier.freeCustomizeControls`, `customizeSectionFree` (`:67`, `:70`): which Customize controls are free (pass-through of native Basics).
- `FreeTier.FREE_MAX_SHORT_SIDE` 1080, `FREE_MAX_FPS` 30, `FREE_MAX_EXPORT_MS` 60 s (`:76-82`): export size, rate and length caps.
- `FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS` 60 s (`:88`) with `AutoDirectorPolicy.amount` (`pro/AutoDirectorPolicy.kt:19-30`): how long the director drives the look.
- `FeatureGate.allowsStyle`, `blockingExportFeature`, `exportCapMs`, `watermarksExports`, `renderLimits` (`pro/FeatureGate.kt:35-62`).
- `PlayerSessionProvider.leaveLockedStyle` and `chooseUnlockedStyle` (`di/PlayerSessionProvider.kt:70-87`): change the scene on screen on an entitlement change.
- `ProSnapshot.animateUntilMs` (`billing/ProSnapshot.kt:38-45`): until when the wallpaper animates.
- Callers build an allowed-style mask in Kotlin and pass it to native stepping: `GeodeNative.stageStepIndex(BooleanArray { allowsStyle(...) }, ...)` at `ui/PlayerSession.kt:906`, `ui/VisualsStage.kt:567`, `ui/StylesPage.kt:165` (seen by grep).

**Other findings:**

- Speed, pitch and skip-silence are Media3 Java processors on the playback thread (`audio/dsp/MvzAudioProcessorChain.kt:16-20`). They are the only sample-level DSP in the playback path that is not in `core/audio/dsp`, and the pitch ratio is computed in Kotlin.
- The tap sits before Sonic, so the visuals hear the track at original speed while the speaker plays it sped up; native is told the speed separately on two channels (`dspSetSpeed`, `playoutSpeed`).
- The track-boundary seed is a Kotlin `Random.nextLong()` (`data/AutoDirectorStore.kt:109`), so the director's per-play seed is not reproducible from native state.
- The entitlement signature is re-verified on a main-thread tick: `ProSnapshot.gate` runs an RSA verify (`billing/ProSnapshot.kt:27`) once a second for a free user with the director on (`playback/AutoDirectorFeed.kt:60-62`).
- `librarySearch` marshals the whole library (three byte arrays per track) on every search (`playback/LibraryTree.kt:118-126`).
- `PaywallScreen.VisualHero` (`ui/pro/PaywallScreen.kt:757-813`) is a second "visualizer" drawn in Compose with its own formula and colours. It is decorative and not fed by audio.
- `PlaybackErrors.describe` holds user-facing English in code (`playback/PlaybackErrors.kt:31-65`), unlike the rest of the app's string resources.
- Dead: `AutoDirectorPolicy.isTaste` (`pro/AutoDirectorPolicy.kt:33`, no caller by Grep); `model/StyleLook` (no production reference, kept in step with native by tests only).
- `ProFeature.STUDIO` gates Record, not a studio editor (`pro/ProFeature.kt:26-30`).
- `SessionBitmapLoader`'s executor thread is not a daemon (`playback/SessionBitmapLoader.kt:22`), unlike the other two executors; it is shut down in `release`.
- `PlayerSessionProvider`'s collection scope is never cancelled (`di/PlayerSessionProvider.kt:56`); it is a process singleton.

### Tests for these packages

All under `app/src/test/java/dev/geode/`; none under `app/src/androidTest` targets these packages (its folders are `export`, `journey`, `render`, `store`, `thumbs`, `ui`, plus `BuildVariantTest.kt`). I read test names and counts, not bodies.

| Test | Pins |
|---|---|
| `ExportTimeLimitTest.kt` (3) | the time-limit mark is set only while a render runs and expires after the window |
| `ForegroundStartTest.kt` (2) | a failed `startForeground` stops once and reports false |
| `playback/PlayRecorderTest.kt` (3) | what is saved and recorded on transitions, through the `Source` seam |
| `playback/PlaybackQueueTest.kt` (11) | the 1001-item window around a tapped track and its clamping at both ends |
| `playback/ResumeSourceTest.kt` (4) | resume the saved queue or nothing; index and position clamps |
| `playback/VoiceSearchTest.kt` (3) | which Media3 items count as a voice search |
| `model/ControlRangeTest.kt` (3) | every control range lies inside the schema range; every cap names a real param and stops short |
| `model/KnobSizeTest.kt` (1), `LibraryChoicesTest.kt` (2), `ScreenRotationTest.kt` (2), `TabLabelsTest.kt` (3) | saved enum names read back; unknown names fall to the old behaviour |
| `model/OpenedAudioTest.kt` (12) | intent routing, refusal of own providers and ungranted uris, name cleaning |
| `model/PresetLinkTest.kt` (9) | link round trip, length and inflate caps, bad payloads decode to null |
| `model/PresetShelfTest.kt` (8) | preset list order, filters, the applied mark, rename and unique-name rules |
| `model/StyleLookTest.kt` (1) | every native catalogue look is a `StyleLook` and every group has styles |
| `model/StyleShelfTest.kt` (5) | recents order and limit, favourites, the "yours" row |
| `model/TourTest.kt` (23) | the tour's step, skip, resume, replay and card placement rules |
| `pro/AutoDirectorPolicyTest.kt` (6) | the live director amount per entitlement; the taste equals the free export cap and the native constant |
| `pro/FeatureGateTest.kt` (11) | what free, trial and Pro allow; free styles are the shader family; family openers are free; export blocks, resolution first |
| `pro/FreeTierCustomizeTest.kt` (2) | free Customize controls are exactly the Basics |
| `billing/BillingBuildTest.kt` (1) | fake billing needs the flag and a debuggable build |
| `billing/CachedEntitlementTest.kt` (12) | the cache is re-verified on read; revoke only after the grace; no answer never revokes |
| `billing/FakeBillingRepositoryTest.kt` (1) | a cancelled purchase does not leave the status in progress |
| `billing/PurchaseVerifierTest.kt` (6) | signature and package-name checks |
| `billing/StoredTrialRepositoryTest.kt` (3) | the trial ends on the hour and after a sleep; clock set back ends it |
| `ui/theme/GlassContrastTest.kt` (7), `SecondaryTextTest.kt` (3) | text and accent contrast on the glass surfaces |
| `ui/tour/TourViewModelTest.kt` (16) | the tour ViewModel's flow from first run to replay |
| `ui/pro/PaywallViewModelTest.kt` (11) | paywall state, purchase, restore and trial start |

No test exists for: `PlaybackErrors`, `PlaybackErrorRecovery`, `QueueOps`, `QueueEnd`, `LibraryAccess`, `LibraryTree`, `SleepTimer`, `TrackCrossfade`, `ReplayGain`, `AutoDirectorFeed`, `BitPerfectOutput`, `PlaybackSession.applyPrefs`, `PlayerSessionProvider`, `MemoryTrim` (Grep over `app/src/test`, `app/src/androidTest` and `engine/*/src/test`; `SleepTimer` matched only `data/prefs/PrefsSchemaTest.kt` and `data/FavouritesRepositoryTest.kt` by name fragments, not as tests of these classes, which I did not open).


---

## 07 ui-a: `app/src/main/java/dev/geode/ui/*.kt`, first 41 by name (`AboutSettings.kt` .. `FolderSettings.kt`)

41 files, 11,378 lines, all read in full. Paths below are relative to `app/src/main/java/dev/geode/ui/` unless they start with another root.

Counts: shell 26, mixed 14, engine 1 (`CustomizeSummary.kt`), duplicate 0, dead 0 whole files (one dead method: `EditorController.recordRender`).

### 1. Per-file table

| File (lines) | Class | Engine logic: function, lines, what it decides | Thread | Native calls (`GeodeNative.x`) | Native owner in `core/` | What must stay Kotlin and why |
|---|---|---|---|---|---|---|
| AboutSettings.kt (236) | shell | none. `policyLines` 137-204 is a Markdown-to-text parser for the bundled privacy policy (UI text only). | Main; asset reads on `Dispatchers.IO` (73, 213) | none | n/a | Compose dialog, `AssetManager` |
| AppSettingsLink.kt (44) | shell | none | Main | none | n/a | `Intent`/`Settings` launch, picker launcher |
| AppSettingsTab.kt (539) | shell | none. `SettingsGroupKey`/`SettingsSectionKey` enums 70-260 are the Settings page map (UI). | Main | none | n/a | Compose; enum names saved by `rememberSaveable` (281-284) |
| AppShell.kt (1288) | mixed | `paletteAccent` 873-883: computes the UI accent colour from `params.paletteBase`/`paletteRange` (hue = base + range/2, wrapped, S 0.85, L 0.6). `accentTarget` 281-286: `if sceneId == SceneIds.MILKDROP` use fallback (a per-style rule). 229: sets `nativeViz.castSource = true` (marks this renderer as the cast source). 459-465: "keep drawing on every tab while a cast streams" input to the stage host (the rule itself is native `stageHost`). Search debounce 250 ms (1042) and notice timeout 8 s (1288) are UI. | Main; IO for prefs (239) and crash file (259) | none directly (through `VisualizerCanvasHost`, `ConnectedDisplays`) | accent: new: `core/viz/Palette` (an "accent of palette" call beside `core/viz/ArtPalette`, `core/viz/Colour.hpp`); MilkDrop exception belongs with it | Compose shell, navigation, PiP params, insets, `VisualizerView` hosting, clipboard, crash file |
| AppTheme.kt (36) | shell | none | Main | none | n/a | Material shapes |
| ArtworkCache.kt (130) | shell | none for the engine. `placeholderBrush` 121-130 derives a placeholder gradient from the URI hash (UI). Cache budget 24 MB (49). | Main; decode on `Dispatchers.IO.limitedParallelism(2)` (45) | none | n/a | `ImageBitmap` cache, `MediaMetadataRetriever` via `MediaArtwork` |
| AudioSettings.kt (223) | mixed | `PresetMorphRow` 53-64: slider range `0f..VisualsSettingsCodec.PRESET_MORPH_SECONDS_MAX` (Kotlin literal 8 s, `data/VisualsSettings.kt:38`): the range of the preset morph time. `SoundTuningControls` 139-204: slot index arithmetic `GAIN+i`, `THRESHOLD+i`, `ATTACK+i`, `RELEASE+i`, `DRUM+i`, `DRUM_DECAY+i` (164-180) assumes the native slot layout; ranges come from `SoundTuning.range(slot)` which reads `GeodeNative.soundTuningSchema()` (`engine/audio-core/.../SoundTuning.kt:43,68`), so ranges are native. `SoundTuning()` default at 150 is the Kotlin default object (reset). `it.clamped()` at 43. | Main | none directly | morph max: new: `core/signal/EngineSettings` or the params schema; tuning layout already `core/sound/Tuning.cpp` | Compose, mic permission request |
| AutoVisualsController.kt (304) | mixed (mostly engine) | `randomStepNow` 246-281: builds Auto's candidate pool from Kotlin tables (`VisualStyleCatalog.silkIds/lifeIds/mycoIds/acidIds` + `SceneCapabilities.SHADER_SCENES.keys`, 252-257), filters by entitlement, user presets and `.milk` files, finds the entry on screen, then `autoPick`. `onDirectorMoment` 220-231: "was it the user's hand" rule (`leftAs` compare of sceneId+params), playlist vs random branch, `keepLook`. `advanceVizPlaylist` 142-149 and `stepVizPlaylist` 157-167: playlist index stepping (`(i+1) % size`, `floorMod`), `running` rule (enabled, size>=2, not holding). `advanceRandomMode` 233-240: `running` rule. `holding` 83: Auto pauses while Customize is open or nothing plays. `editing` setter 61-69 and `noteHandPick` 76-80: restart both timers. `setVizPlaylistEnabled`/`setRandomEnabled` 126-135, 169-182: the two modes are mutually exclusive. `setVizPlaylistInterval`/`setRandomInterval` 137-140, 184-187: clamp to `AutoVisualsPrefsStore.INTERVAL_SEC` = `5..300` (Kotlin literal, `data/AutoVisualsPrefsStore.kt:57`). `applyVizEntry` 284-303: order scene, preset, milk; `autoMarkChange`. `addToVizPlaylist` 85-94: duplicate rule. | Main (called from `PlayerSession.pollPlaybackState`, every 500 ms, `PlayerSession.kt:974-985,991`; director moments posted to Main, `EnginePlumbing.kt:171`) | `autoTimerRestart`, `autoTimerDue`, `autoPick`, `autoMarkChange`, consts `AUTO_TIMER_PLAYLIST`, `AUTO_TIMER_RANDOM` | existing `core/viz/AutoTimer.hpp`, `core/viz/AutoDraw.hpp`, `core/viz/StylePick.hpp`, `core/viz/StyleSchedule.cpp`, `core/viz/Director.cpp`; the pool, the hold rule, the hand rule and the playlist cursor are new there | The `.milk` file list and the saved presets are storage; entitlement (`allowsStyle`) is Pro; prefs save |
| BackgroundController.kt (167) | mixed | `redecode` 130-159: decides when the underlay image is rebuilt (generation counter, 200 ms debounce at 165) and at which pixel size (`setRenderSize` 95-104); calls `BackgroundImage.decode(..., blurRadius, dim)` (crop, scale, blur, dim done in Kotlin in `viz/BackgroundImage`, outside this scope). `setPrefs` 114-128: blend/amount are re-pushed without a decode. Clamps are native. | Main.immediate for state; decode on `storeScope` (not Main) | `underlayClampAmount`, `underlayClampBlur`, `underlayClampDim` | existing `core/viz/Underlay.cpp`; blur/dim/crop of the pixels is new there | `ContentResolver` persisted URI grants, `BitmapFactory` decode, prefs |
| BackgroundSheet.kt (345) | mixed (light) | Slider ranges as Kotlin literals: Amount `0f..1f` (152), Dim `0f..1f` (182). Blur range is native (`BackgroundPrefsStore.BLUR_RANGE` = `0..GeodeNative.underlayBlurMax()`, 167). | Main | none directly | `core/viz/Underlay.cpp` (ranges) | Compose sheet, `OpenDocument` picker |
| BackupSettings.kt (193) | shell | none | Main | none | n/a | SAF pickers, restart via `exitProcess` |
| BackupViewModel.kt (102) | shell | none | Main (`viewModelScope`) | none | n/a | ViewModel over `BackupRepository` |
| BehaviorSettings.kt (456) | mixed (light) | `TouchSmearRow` 265-270: smear strength slider `0.2f..2f` (Kotlin literal range of a native touch parameter; pushed to native in `EnginePlumbing.kt:143-146`). `gestureChoices` 393-404 and `ONE_SHOT_ACTIONS` 406-414: which action each gesture may take (UI/gesture map, not picture logic). | Main | none | smear range: `core/viz/TouchSmear.hpp` | Compose, `WallpaperManager` intent, window flag `FLAG_KEEP_SCREEN_ON` (368-377), permission request |
| BootIntro.kt (243) | shell | none. Timing constants 58-78 drive a Compose intro over a muted ExoPlayer. | Main (+ ExoPlayer's own threads) | none | n/a | ExoPlayer, `TextureView`, Compose animation |
| CaptureController.kt (295) | mixed | Who writes the PCM ring and when an epoch starts: `stopMic` 85-89 (`micRing.beginEpoch()`), `setMicEnabled` 170-191 (mic gives way to external capture, epoch before first block), `startPlaybackCapture` 205-226 (pause player, stop mic, `resetSound(false)`, start, `resetSound(!externalAudioOwnsRing())`), `stopExternalAudio` 228-235, `shutdown` 285-294, `refreshMicState` 277-283. Background rule `onBackground`/`onForeground` 98-115 (mic stops when not visible unless a cast holds it). `refreshExternalAudio` 252-275: "refused by app" = capture active and quiet while the other app reports playing (263). | Main (session scope); binder reads on `Dispatchers.IO` (259); `MicCapture`/`PlaybackCapture` own reader threads (outside this scope) | none directly (`SampleRing.beginEpoch` wraps native) | ring ownership and epochs: existing `core/audio/ingest` (`PcmRing`), `core/viz/PcmHandoff.hpp`, `core/sound/SoundSource.hpp`; silence test `core/sound/Silence.hpp` | `MediaProjection`, `AudioRecord` playback capture, notification-listener access, foreground services, permission state |
| CloseOnBack.kt (22) | shell | none | Main | none | n/a | NavigationEvent |
| ColorDerive.kt (29) | shell | `scaleSaturation` 6-18 (luma 0.299/0.587/0.114) and `dim` 20-28: colour maths, used only by the UI theme (`ui/theme/GeodeTheme.kt:29`). `dim` has no caller found by Grep. | Main | none | n/a (UI theme); could use `core/viz/Colour.hpp` | UI accent only |
| ColourPicker.kt (324) | shell | none: every conversion is native. `sampleAt` 307-324 maps a touch to image pixels (ContentScale.Fit geometry, 3x3 window) and hands them to native for the mean. Accessibility step 0.1 (98). | Main | `colourFromHsv`, `colourToHsv`, `colourHex`, `colourParseHex`, `colourAverageArgb` | existing `core/viz/Colour.hpp` | Compose canvas, `Bitmap.getPixels` |
| ConnectedDisplays.kt (283) | shell | none: `target` 63-72, `refresh` 92-101 and `flagsOf` 103-113 pack Android display facts into flag bits and native decides. Failure count kept per display in Kotlin (55, 74-77), capped at 0xFF (116). | Main | `stagePresents`, `stagePickDisplay`, `stageDisplayEligible`, consts `STAGE_PRESENT_*`, `STAGE_DISPLAY_*` | existing `core/viz/StageRun.hpp` | `DisplayManager`, `Presentation`, lifecycle |
| Crystal.kt (249) | shell | none: `VisualizerCanvasHost` 204-246 asks native where and how the stage shows; Kotlin maps the answer to alpha 1, 1/255 or 0 (233-238, 249). | Main (layout pass) | `stageHost`, consts `STAGE_HOST_SHOWN/CASTING/OPAQUE/FAINT` | existing `core/viz/StageRun.hpp` | `AndroidView`, Compose theme |
| CrystalControls.kt (282) | shell | none (generic button, slider, switch, tabs) | Main | none | n/a | Compose |
| CustomizeAdvanced.kt (659) | mixed | Kotlin literal ranges of modulation parameters: reaction attack `0f..1f` (208), release `0f..3f` (211), amount `ReactionRoute.MIN_AMOUNT..MAX_AMOUNT` = -2..2 (167; consts in `engine/scenes/.../Reactions.kt:193-194`); LFO depth `0f..1f` (303); ADSR attack `0.005f..1f` (396), decay `0.01f..1.5f` (397), sustain `0f..1f` (398), release `0.02f..2f` (399), amount `0f..1.5f` (400), gate threshold `0.05f..1f` (408); layer mix `0f..1f` (454). `NEW_REACTION` 163-164: default of a new route (ONSET to COLOR_SHIFT, STEP, 0.15). `RateSecondsSlider` 341-364: log mapping `lo*(hi/lo)^t` of the LFO period (bounds native via `ModTable.minRateSeconds/maxRateSeconds`). `ModulatorCard` 286-289: on source change sets `polarity = ModTable.naturalPolarity(source)` (native table, applied in Kotlin); 294-301: timing choices `LfoConfig.BEAT_CHOICES` = 1,2,4,8,16,32 (Kotlin literal, `engine/scenes/.../Lfo.kt:154`) and `ownPeriod = beats == 0 || moment >= 0` (which control shows). `AdsrCard` 378-381: target filter `offered && (chosen || reaches)`. `ReactionsSection` 134-160: route list add/replace/remove, limit `MAX_ROUTES` = 12 (Kotlin const). `LayersPage` 424-454: picks the first other scene as the default layer (425), blend enum by ordinal. `TransitionPicker` 523-556: drops "cut", appends `"random"` (510, 526), built-in ids from Kotlin `TransitionCatalog.BUILT_IN_IDS`. `liquidHotNote` 561-566: maps renderer liquid mode ints 2,3,4 to text. `ShaderCodePage` 574-581: `if SceneCapabilities.isFluid(sceneId)`. `InjectionShaderEditors` 630-634: sends null when the source equals the built-in template. | Main; asset read on IO (596) | `HAND_ALL` const; through wrappers: `ModTable.*`, `BuildStyleReactionsUseCase.playing` (JNI) | existing `core/viz/Lfo.cpp`, `core/viz/Adsr.cpp`, `core/viz/Reactions.cpp`, `core/viz/ModTargets.cpp`, `core/viz/ReactionResolve.hpp`, `core/viz/LayerState.cpp`, `core/viz/TransitionChoice.hpp`; ranges and defaults new: one modulation schema there (as `core/viz/ParamTable.inc` is for params) | Compose widgets, strings |
| CustomizeBrowser.kt (233) | shell | none | Main | none | n/a | Compose |
| CustomizeCategory.kt (164) | shell | none: `CustomizeMapTable.rows` 144-163 splits native text into rows; `SECTION_TITLES` 64-106 are words. `VERSION = 1` (117) must match native `kTextVersion`. | Main (lazy) | `customizeText`, `customizeShown` | existing `core/viz/CustomizeMap.cpp` | Icons, titles |
| CustomizeControls.kt (713) | mixed | `when (key)` dispatch 85-461 maps each native control name to a widget (shell), but with rules inside: show-if rules `PALETTE_2` only if `paletteMix > 0.001` (93, 683); `CYCLE_SPEED` if `colorCycle` (117); `DIVE_SPEED` if `endlessZoom || flowEnabled`, scope switched by `flowEnabled` (217-221); `CATCH_PULL`/`CATCH_RADIUS` if `fluidCatchPoints > 0` (164, 168); `FLOW_*` if `flowEnabled` (312, 318, 324); `FLUID_GLOW`/`GLOW_THRESHOLD` if `fluidBloom` (382, 388); `SUNRAYS_WEIGHT` if `fluidSunrays` (395); `FLUID_CURL` if `SceneCapabilities.isFluid(sceneId)` (362); `@transition_length` if `transitionId != null` (513). Kotlin literal ranges: transition length `0.3f..5f` (517), Rise `0.05f..1f` (664), Fall `0.02f..0.6f` (665). Index clamps as literals: `fluidBeatPattern.coerceIn(0,3)` (174), `liquidBorn.coerceIn(0,3)` (330), `liquidAge.coerceIn(0,2)` (334), `liquidSurface.coerceIn(0,2)` (338). Choice tables in Kotlin: `LIQUID_BORN_CHOICES`, `LIQUID_AGE_CHOICES`, `LIQUID_SURFACE_CHOICES` (711-713), and `SceneParams.PARTICLE_SHAPES/FLUID_PATHS/FLUID_PATTERNS/CAMERA_PATHS/CYMATICS_GEOMETRIES/SYMMETRY_FOLDS`, `FluidQuality.LABELS` (in `engine/scenes`). `KaleidoscopeControl` 592-603: turning it on sets `symmetry` to `DEFAULT_SYMMETRY_FOLDS` (6) when below 2; folds list filtered `>= 2`. `COLOUR_MAP` 99-108: index mapping `i-1`, `NO_PALETTE_LUT`. `CymaticsGeometry` 611-618: forced geometry from `Catalogue.override`. Envelope and wave slot counts `AdsrEngine.COUNT` (2) and `LfoEngine.SLOTS` (3) (532, 549) are Kotlin consts. All `LabeledSlider(key, p, SceneParams::x)` rows take their range from the native schema (see CustomizeTabs). | Main | `paletteColourMapName` (687) | existing `core/viz/CustomizeMap.cpp` (add the show-if rules and choice lists), `core/viz/ParamTable.inc`, `core/viz/Palette.cpp` | Compose widgets, display names |
| CustomizeEvents.kt (334) | mixed (light) | Moment rows are native (`MusicMoments.rows/toggle/withStrength/resetToAuto`). Kotlin holds: `LIGHT_ACTIONS`/`MOTION_ACTIONS` grouping 143-144; "None" = fold of native toggles over every on action (216-219); strength range `0f..MusicMoments.MAX_STRENGTH` (236; `MAX_STRENGTH = 2f` is a Kotlin const in `ui/MusicMoments.kt:68`); Keep = `withStrength(current)` or `resetToAuto` (300-304); recipe on/fits/toggle through `EventRecipe` (112-131). | Main | through `MusicMoments`: `musicActionsFor`, `musicState`, `musicEdit` | existing `core/viz/ReactionText.cpp`, `core/viz/Reactions.cpp`, `core/viz/ReactionDna.cpp` | Compose |
| CustomizeMacros.kt (320) | mixed (light) | Live modulation poll: `produceState` 93-99 calls `store.liveMods()` (JNI `macroBankLiveMods`, text parsed in Kotlin) every 33 ms while the knobs are shown. Knob drag mapping 220-223: `value - dy/unitPx`, clamp -1..1, 110 dp per unit (310). `drawModulation` 172-190 clamps `turn+low/high/add` to -1..1 for drawing. Knob range -1..1 and 39 a11y steps (230, 312) are Kotlin literals. | Main | through `MacroStore` (`macroBankLiveMods` etc.) | existing `core/viz/MacroBank.cpp`, `core/viz/MacroControls.cpp` | Compose canvas, gestures |
| CustomizePanel.kt (451) | shell | none of weight: `canRoll = category == QUICK_KNOBS || "<CAT>:roll" in shown` (137) and the QUICK_KNOBS branch of randomize/reset (227-233, 290) pick which native call runs; the `#<CATEGORY>` keep marker (303) is expanded natively. | Main | none directly (ViewModel and `MacroStore` call native) | `core/viz/CustomizeMap.cpp`, `core/viz/RandomizeAll.cpp` | Compose, Pro gate, prefs for open groups |
| CustomizeSummary.kt (21) | engine | `changedCount` 10-18: how many params differ from default, by diffing `PresetStore.paramsToJson(params)` against the JSON of `SceneParams.DEFAULT` (a Kotlin default object, `engine/scenes/.../SceneParams.kt:212`). Runs on every `vizState` emission (`CustomizePanel.kt:212-214`), so a JSON serialisation per slider tick. | Main (flow map on collector) | none | new: `core/viz/Params.cpp` / `core/viz/ParamsFields.cpp` (count of fields off default) | nothing |
| CustomizeTabs.kt (719) | mixed (light) | Param sliders: range is `ControlRange.of(param)` (613-621) = native schema range (`ParamSchema`) cut by Kotlin caps in `model/ControlRange.kt` (`brightness`/`intensity` 1.25, `bloom` 0.25, `camFly` -2..2): the caps are Kotlin literals. Reset default is `param.get(SceneParams.DEFAULT)` (618), a Kotlin default. `RESET_EPSILON = 1e-4` (595). `LabeledIntSlider` 670-696: `toInt().coerceIn(range)` and step count. Try-then-pay spring back 569-583 (Pro). `visible` 170-178 asks native through `ParamScope.appliesTo` (`Catalogue.reaches`). `lockableKeys` 217-223 from native. | Main | `customizeLockableKeys` | existing `core/viz/ParamTable.inc`, `core/viz/Catalogue.cpp`, `core/viz/ParamRandomizer.cpp`; caps and defaults belong in the schema | Compose widgets, Pro lock |
| DuplicatesTab.kt (104) | shell | none | Main | none | n/a | MediaStore delete request |
| EasterEggs.kt (94) | shell | none: accelerometer samples and tap times go to native, which decides. | Main (sensor callbacks registered without a handler, 67) | `shakeCreate`, `shakeSample`, `shakeDestroy`; `nativeViz.tapAlong`, `nativeViz.requestStill` | existing `core/viz/Shake.hpp`, `core/viz/TapAlong.hpp` | `SensorManager` (NDK `ASensorManager` exists, so this could move), haptics, toast |
| EditorController.kt (161) | shell | none for the engine: project folder bookkeeping. `createWithLook` 90-121 builds a one-clip timeline model for storage. `recordRender` 124-127 has no caller (Grep: only its own definition and KDoc). | Main + `storeScope` | none | n/a | project store |
| EnginePlumbing.kt (181) | mixed | `liveLook` 49-70: the one look builder for live and export; converts `transitionDurationSec * 1000` to ms (68). `LayersUiState` 30-35: layer defaults mix 0.5, blend SCREEN (Kotlin literals); `LayersBus` 37-43 is a process-global bus. `VisualizerEngineBindings` 75-181: every push from UI state to the renderer (see section 2/5/6). Decisions inside: underlay sent with `blend.ordinal` (113); layer scene null when disabled (138); fps cap resolved by native (161); `queueEvent { renderer.warmTransition(id) }` (155); director moment hopped to Main (171). | Main; one `queueEvent` to the GL thread (155); callbacks arrive from the GL thread (`onSurfaceSizeChanged` 101, `onDirectorStyleMoment` 171, `onMilkPresetLoaded` 170, `onShaderError` 95) | `liveFpsCap` (161); through `NativeViz`: `setTouchSmear` | existing `core/signal/Resolver`, `core/signal/EngineSettings`, `core/viz/LayerState.cpp`, `core/viz/frame/LiveFps.hpp`, `core/viz/frame/PushVersions.hpp` | Compose effects reading stores and ViewModels; `View.keepScreenOn` |
| EqualizerSettings.kt (135) | shell | none for the engine. Bass boost and loudness sliders `0f..1000f` (121, 130) are Android `AudioEffect` strength ranges; band ranges come from the effect (112). | Main | none | n/a (platform effects) | Android `Equalizer`/`BassBoost`/`LoudnessEnhancer` state |
| ExportController.kt (579) | mixed | Runs the export jobs off Main on `ExportRun.scope` (`Dispatchers.Default`, `export/ExportRun.kt:12,46`). `startExport` 115-178, `startBatch` 213-260 (track loop 233-251, per-track failure policy, cancel-rest), `renderBatchTrack` 262-294, `renderOneTap` 381-416 (range shorter than `MIN_CLIP_DURATION_MS` is "nothing to render" 391; style lock check 393; `steady = p >= VideoExporter.AUDIO_PROGRESS_SPAN` 407), `startLoopRender` 456-511 (`LoopSpec.of`, `longFormMs = soundtrackMs + loopMs` 480), `finishLoopRender` 514-555, `publishLoopProgress` 557-561 with `RENDER_SPAN = 0.7`/`EXTEND_SPAN = 0.3` (576-577): progress weighting. Look pinning `host.look.pin()` (231, 392, 477); batch replaces only `params` (231). Loudness target and background read from prefs per render (418-435). File names `beatrealms_<ms>.mp4`, `beatrealms_loop_<ms>.mp4` (137, 273, 536). | Main for state; `Dispatchers.Default` for the jobs; IO for prefs (419) and delete (332) | none directly (`VideoExporter`, `LoopRender`, `LoopExtend` are outside this scope) | existing `core/output/RecordPlan.cpp`, `core/output/EncodePlan.cpp`, `core/output/LoopPlan.cpp`, `core/output/LoudnessAdvice.cpp`, `core/output/FrameClock.cpp`; batch sequencing and progress weighting new: `core/output` | foreground service start, MediaStore/SAF destination, UI state, Pro gate |
| ExportHost.kt (282) | shell | none: flattens requests for `rememberSaveable` and launches pickers. | Main | none | n/a | SAF `CreateDocument`, permission |
| ExportPhase.kt (54) | shell | none | any | none | n/a | UI state type |
| ExternalAudioSettings.kt (173) | shell | none | Main | none | n/a | `MediaProjectionManager` consent, permissions |
| FirstRun.kt (24) | shell | none | Main | none | n/a | Compose |
| FolderSettings.kt (187) | shell | none | Main | none | n/a | SAF tree picker |

### 2. Threads started here

- `AppShell.kt:239`: `withContext(Dispatchers.IO)`, one read of the boot-animation pref at cold start.
- `AppShell.kt:259`: `withContext(Dispatchers.IO)`, one read of `crash-latest.txt` (max 64 KiB).
- `AppShell.kt:192-197`: `LaunchedEffect` + `snapshotFlow` on Main, per back-gesture frame (UI animation only).
- `AppShell.kt:1041-1044`: `LaunchedEffect(query)` 250 ms search debounce, Main.
- `AppShell.kt:1249-1252`: 8 s notice timer, Main.
- `AboutSettings.kt:73, 213`: `Dispatchers.IO`, one asset read each.
- `ArtworkCache.kt:45`: `Dispatchers.IO.limitedParallelism(2)`, cover decodes on demand.
- `BackgroundController.kt:59, 120, 140`: `storeScope.launch` (scope passed in by `PlayerSession`): readability check once, pref save per change, image decode per pick/resize/blur/dim change (debounced 200 ms, `:143,165`); result hops to `Dispatchers.Main.immediate` (`:67, 148`).
- `BehaviorSettings.kt:72`: `container.appScope.launch` per pref edit.
- `BootIntro.kt:120-128`: a private `ExoPlayer` (its own playback and codec threads) for the splash clip; `LaunchedEffect` timers at 158-176 on Main.
- `CaptureController.kt:136, 141, 153`: three `scope.launch` collectors (cast mic hold, media projection, projection start failures), Main, event driven. `:257-274`: `scope.launch` + `withContext(Dispatchers.IO)` binder reads, at most once per 500 ms poll and one in flight.
- `CaptureController.kt:62, 68`: constructs `MicCapture` and `PlaybackCapture`, which own the AudioRecord reader threads (their files are outside this scope).
- `CustomizeAdvanced.kt:594-601`: `produceState` + `Dispatchers.IO`, one shader asset read.
- `CustomizeMacros.kt:93-99`: `produceState` loop on Main, `delay(33)` (`LIVE_POLL_MS`, `:127`), runs while Quick knobs is composed and reduced motion is off: about 30 JNI calls per second plus a text parse.
- `EasterEggs.kt:67`: `SensorManager.registerListener(..., SENSOR_DELAY_GAME)` with no handler, so callbacks arrive on Main at the game rate while Visuals is resumed.
- `EditorController.kt:58, 69, 96, 139`: `storeScope.launch`, one per store write.
- `EnginePlumbing.kt:90, 94-180`: `rememberCoroutineScope` and ten `LaunchedEffect` collectors on Main (see section 5); `:155` `visualizerView.queueEvent {}` runs once on the GL thread per transition id change.
- `ExportController.kt:100`: `scope.launch` collector re-attaching to a running export. `:135, 229, 475`: `ExportRun.launch` on `ExportRun.scope` = `SupervisorJob() + Dispatchers.Default` (`export/ExportRun.kt:12`), one job per render, lives for the whole render. `:330-336, 419`: `Dispatchers.IO` for delete and prefs.
- `BackupViewModel.kt:80, 92`: `viewModelScope.launch` per backup step.
- `DuplicatesTab.kt:87`, `FolderSettings.kt:65`: `rememberCoroutineScope().launch` per tap.
- No `Thread`, `HandlerThread`, `Executor` or `Handler` ticker is created in these 41 files.

### 3. Per-frame and per-audio-block Kotlin paths

- None of these files runs once per video frame on the GL thread or once per audio block.
- Per UI frame, Main thread: `Crystal.kt:225-244` `VisualizerCanvasHost` layout lambda calls `GeodeNative.stageHost` each layout pass (layout passes, not render frames; it re-runs when `bounds` or constraints change).
- Per UI frame during a gesture: `AppShell.kt:353` reads the back-gesture fade in `graphicsLayer`.
- Pointer rate (not frame rate): `EnginePlumbing.kt:110-121` rebuilds a `LookSnapshot` (`liveLook`) and assigns `visualizerRenderer.look` on every `vizState`/modulation/tuning/underlay emission, so once per slider tick or pinch sample. Chain: Compose slider `onValueChange` -> `VisualsViewModel.editSceneParams` (`CustomizeControls.kt:63`) -> `vizState` -> `combine` (`EnginePlumbing.kt:118`) -> `renderer.look = it` -> the renderer's `LookApplier` on the GL thread (outside this scope).
- Same rate: `CustomizeSummary.changedCount` (`CustomizePanel.kt:212-214`) serialises the params to JSON twice per `vizState` emission while Customize is open.
- 30 Hz: `CustomizeMacros.kt:93-99` `store.liveMods()` -> `GeodeNative.macroBankLiveMods()` (`data/MacroStore.kt:90`).
- Sensor rate (about 50 Hz): `EasterEggs.kt:56-59` `GeodeNative.shakeSample` per accelerometer event while Visuals is resumed.
- 2 Hz: `PlayerSession.pollPlaybackState` (`PlayerSession.kt:974-985`, 500 ms) -> `CaptureController.refreshExternalAudio` / `refreshMicState` and `AutoVisualsController.advanceVizPlaylist` / `advanceRandomMode` -> `GeodeNative.autoTimerDue`. This poll is the clock that decides when Auto changes the visual, so Auto's timing resolution is 500 ms and it stops when the session's poll stops.

### 4. Duplicated drivers

- Auto style change has three Kotlin entry paths into one apply function: the 500 ms poll (`AutoVisualsController.kt:142-149, 233-240`), the director moment from the GL thread posted to Main (`EnginePlumbing.kt:171` -> `AutoVisualsController.kt:220-231`), and the hand step from a gesture (`:157-167`). The timers and the draw are native (`core/viz/AutoTimer.hpp`, `core/viz/AutoDraw.hpp`), but the pool, the hold rule and the playlist cursor are Kotlin, and only the live session has them: nothing in this scope gives the wallpaper, dream or export the same Auto.
- Look pushes: the live look is built in `EnginePlumbing.kt:110-121`; exports pin the same source (`ExportController.kt:231, 392, 477` through `Host.look`). Besides the look, `EnginePlumbing` pushes five things on separate paths: the layer (`:136-140`, `submitLayer`), touch smear (`:141-147`), performance/fps cap (`:160-162`), param morph (`:166-168`), milk preset and custom shader (`:172-179`), plus transition warm-up (`:151-156`). Fluid injection shaders are pushed from a UI lambda (`CustomizeAdvanced.kt:577-579`), a seventh path.
- Underlay: live decode in `BackgroundController.redecode` (`:130-159`) at the surface size; exports re-read the same prefs and build a `BackgroundExportSpec` (`ExportController.kt:431-435`) that is decoded again elsewhere. Two decode call sites for one image.
- Export drivers: three job bodies with the same begin/try/catch/finish frame: `startExport` (`ExportController.kt:134-177`), `startBatch` (`:228-259`), `startLoopRender` (`:474-510`). Two cancel flags each plus the shared `ExportRun.cancelRequested` (`:93-94, 443-444, 316-319, 564-567`). Loudness target is read two ways (`loadExportDefaults` on IO `:418-419` and `defaultLoudnessTarget` on the job thread `:424-427`).
- Visibility/run rule: the stage's shown/keep-drawing rule is native (`stageHost`), but its inputs are gathered in two places: `AppShell.kt:457-466` (destination, casting) and `ConnectedDisplays.target` (`:63-72`, second screen); `StageRunEffect` (`ui/StageRun.kt:23`, outside this scope) is a third.
- Keep-screen-on has three owners: `EnginePlumbing.kt:148-150` (view flag from `playerPrefs`), `BehaviorSettings.kt:368-377` (window flag while Visuals shows), `ConnectedDisplays.kt:189, 214` (presentation and host view).
- Customize control visibility has two layers: native `customizeShown` + `Catalogue.reaches` (`CustomizeTabs.kt:170-178`) and the Kotlin show-if rules listed for `CustomizeControls.kt`.
- Param defaults exist twice: the native schema and the Kotlin `SceneParams.DEFAULT` used for Reset (`CustomizeTabs.kt:618`) and for the changed count (`CustomizeSummary.kt:8`).

### 5. Data crossing the JNI boundary (engine/mixed files)

- `AutoVisualsController`: `autoTimerRestart(timer:Int, nowMs:Long)` on each hand pick/toggle; `autoTimerDue(timer:Int, nowMs:Long, intervalSec:Int, running:Boolean): Boolean` twice per 500 ms; `autoPick(count:Int, onScreen:Int): Int` per draw; `autoMarkChange(key:String)` once or twice per automatic change. Time base is `SystemClock.elapsedRealtime()` read in Kotlin.
- `BackgroundController`: `underlayClampAmount(Float): Float`, `underlayClampBlur(Int): Int`, `underlayClampDim(Float): Float`, one per slider tick.
- `EnginePlumbing`: `liveFpsCap(fpsCap:Int, refreshRates:FloatArray): Int` on a performance or display change. Per look change the renderer receives a `LookSnapshot` (sceneId, `SceneParams`, LFO list, ADSR list, reaction routes or null, `SoundTuning`, reducedMotion, transitionId, transitionMs, underlay). The underlay carries the decoded pixels array with width, height, blend ordinal and amount (`:113`), re-sent only when the background push changes. `submitLayer(sceneId:String?, mix:Float, blend)`, `setTouchSmear(on:Boolean, strength:Float)`, `warmTransition(id)`, `beginParamMorph(seconds)`, `loadMilkPreset(path)`, `submitShader(sceneId, source)` go through `VisualizerRenderer`/`NativeViz`. Back from native on the GL thread: surface size (w, h), director style moment (no payload), milk preset loaded (name), shader error (String), liquid mode (Int flow).
- `CustomizeAdvanced`: `visualizerRenderer.submitFluidInjectionShaders(force:String?, dye:String?)` per Apply tap; reaction routes through `BuildStyleReactionsUseCase.playing(sceneId, seed, routes, mask)` (packed float array, up to 12 routes) once per recomposition key change; `ModTable` reads are cached tables from native.
- `CustomizeControls`: `paletteColourMapName(index:Int): String` in a loop until empty, once per panel (`:686-687`).
- `CustomizeTabs`: `customizeLockableKeys(): String` (newline list) once per process.
- `CustomizeCategory` (shell, for completeness): `customizeText(): String` once, `customizeShown(styleId): String` per style change.
- `CustomizeEvents`: through `MusicMoments`: `musicActionsFor(sceneId): Int` bitmask, `musicState(sceneId, seed, packedRoutes:FloatArray, mask:Int): FloatArray`, `musicEdit(...)` per tap; several edits in a row for "None" (`:219`).
- `CustomizeMacros`: `macroBankLiveMods(): String` at 30 Hz, parsed into `Map<Int, MacroLiveMod>`; `setValue(id, value, persist)` per drag sample.
- `ColourPicker` (shell): `colourFromHsv(h,s,v): Int`, `colourToHsv(rgb, FloatArray(3))`, `colourHex(rgb): String`, `colourParseHex(String): Int`, `colourAverageArgb(IntArray of up to 9): Int`, per pointer sample; 13 `colourFromHsv` calls once for the hue strip.
- `ConnectedDisplays`/`Crystal` (shell): `stagePickDisplay(ids:IntArray, flags:IntArray, preferredId:Int): Int`, `stageDisplayEligible(flags:Int): Boolean`, `stagePresents(flags:Int): Boolean`, `stageHost(flags:Int, area:IntArray(4), windowW:Int, windowH:Int, outRect:IntArray(4)): Int`.
- `EasterEggs` (shell): `shakeCreate(): Long`, `shakeSample(handle, timestampNs, x, y, z): Boolean`, `shakeDestroy(handle)`; `nativeViz.tapAlong(uptimeNs): Boolean`; `nativeViz.requestStill { still }`.
- `ExportController`, `CaptureController`, `AudioSettings`, `BackgroundSheet`, `BehaviorSettings`: no direct JNI call in the file.

### 6. Saved formats touched

- `AppSettingsTab.kt`: `SettingsGroupKey` and `SettingsSectionKey` names are saved through `rememberSaveable` (`:281-284`, `"GROUP/SECTION"` joined by `,`); `FORMER_GROUPS` (`:204-211`) and `FORMER_SECTIONS` (`:218-223`) keep old names loading.
- `CustomizeCategory.kt`: `CustomizeCategory` enum names and section paths `"<CATEGORY>/<id>"` (`:57`) are what `CustomizeGroupsStore` saves (tab and open groups, `CustomizePanel.kt:113-117`). Native text format `customize|1` and `section|CAT|id|BASICS|..|controls` (`:153-158`), version 1.
- `CustomizePanel.kt:303`: the keep marker `"#<CATEGORY>"` is stored in the locked-params set beside control names.
- `CustomizeControls.kt:690-709` and `CustomizeAdvanced.kt:645-659`: control name strings ("Reaction DNA", "Flow field on", "Ring spacing", "Glide time", "Wave depth", ...) are the lock keys saved in the kept set and the Pro free set; they must match `core/viz/CustomizeMap` spellings.
- `CustomizeEvents.kt:234`: lock/knob key `"Music <moment> strength"` built from `MusicMoment.name.lowercase()`; `MusicMoment.ordinal` is passed to native (`:100-106`).
- `CustomizeAdvanced.kt`: enum ordinals/entries used as stored indices: `ReactMode`, `ModCurve`, `ModSource`, `LfoWave`, `ModPolarity`, `EnvBand`, `BlendMode` (`:202-216, 286-308, 405, 451`); transition ids are strings (built-in names lowercase, `"random"`, library names).
- `AutoVisualsController.kt`: `AutoVisualsPrefsStore` (playlist entries with `sceneId`, `presetName`, `milkPath`, `label`; intervals 5..300 s; the two enable flags; three include flags). Playlist entries find presets by name (`:293-296`), so a rename rewrites them (`:109-124`).
- `BackgroundController.kt`: `BackgroundPrefsStore` (`uri`, `blend` as `UnderlayBlend`, `amount`, `blurRadius`, `dim`); `UnderlayBlend.ordinal` crosses to native (`EnginePlumbing.kt:113`).
- `EnginePlumbing.kt`: `LayersBus.state` is in memory only (not saved in this file).
- `ExportController.kt`: `ExportPrefsStore` (`loudnessTargetId`), `BackgroundPrefsStore`; output names `beatrealms_<ms>.mp4`, `beatrealms_loop_<ms>.mp4`.
- `ExportHost.kt:26-58, 92-133`: saved-instance lists by position (aspect w/h/bitRate, fps, sceneId, range, `ExportCodec.name`; loop fields and clip URIs): process-death only, not on disk.
- `EditorController.kt`: project folders `files/projects/<id>/` through `EditorProjectStore`; default name prefix `"Project"` (`:159`).
- `BehaviorSettings.kt`: `BehaviourPrefs` fields (`startScreen`, `queueEnd`, `gestures` map of `CanvasGesture` to `GestureAction`, `notificationButtons`, dream switches).
- `AudioSettings.kt`: `SoundTuning` slots by index (saved by the settings store), `VisualsSettings.presetMorphSeconds`.
- `BackupSettings.kt`: backup zip via `BackupFormat` (default file name by date).
- `AppShell.kt:260, 520`: `filesDir/crash-latest.txt`.

### 7. Surprises

- Auto visuals is a Kotlin state machine on a 500 ms UI poll. Native owns the timer and the random draw, but which styles are in the pool is a Kotlin concatenation of five Kotlin tables (`AutoVisualsController.kt:252-257`), and the "user touched it since" rule compares whole `SceneParams` objects in Kotlin (`:225-227`).
- `CustomizeSummary.changedCount` (21 lines) serialises the full params to JSON on every slider tick to count differences against a Kotlin default object.
- The param sliders do take their ranges from the native schema, but four caps (`brightness`, `intensity`, `bloom`, `camFly`) are Kotlin literals in `model/ControlRange.kt`, and every modulation range (reaction attack/release, ADSR A/D/S/R/amount/gate, LFO depth, layer mix, transition length, rise/fall, touch smear strength, preset morph max, Auto interval, moment strength max, knob range) is a Kotlin literal with no native schema behind it.
- Reset-to-default and the changed count use Kotlin's `SceneParams.DEFAULT`, not a native default, so Kotlin and native each hold the defaults.
- `CustomizeControls` says "which control is where is native", yet it carries nine show-if rules keyed on other param values and one on the style (`isFluid`), and three choice lists that exist only in Kotlin (`:711-713`).
- `ModulatorCard` writes the natural polarity when the source changes (`CustomizeAdvanced.kt:286-289`): a parameter decision made in a UI lambda.
- `LayersBus` (`EnginePlumbing.kt:37-43`) is a process-global mutable bus written directly from Compose lambdas (`CustomizeAdvanced.kt:424-454`), bypassing the ViewModels; its defaults (mix 0.5, SCREEN) are Kotlin literals and nothing in this scope persists it.
- Renderer callbacks are assigned inside `LaunchedEffect`s (`EnginePlumbing.kt:95-103, 170-171`) and fire on the GL thread; two of them hop to Main by different means (`mainScope.launch` at 102, `view.post` at 171).
- The accent colour of the whole UI is computed in Kotlin from two scene params with a MilkDrop exception (`AppShell.kt:281-286, 873-883`).
- `ColorDerive.dim` has no caller; `EditorController.recordRender` has no caller although its class KDoc says the render path uses it (`EditorController.kt:40-42`).
- `CloseOnBack.kt:13` refers to `rememberCardBack` in `CustomizeBrowser`, which does not exist in `CustomizeBrowser.kt`.
- `EasterEggs` runs the accelerometer on the Main thread at `SENSOR_DELAY_GAME` with a JNI call per sample.
- `BootIntro` builds a second `ExoPlayer` instance for the splash clip.
- `ExportController` holds two styles of the same pref read (suspend on IO and blocking on the job thread) and the KDoc at `:204-211` still speaks of a performance take and editor keyframes that the code no longer has.
- Not verified here (outside scope): what `BackgroundImage.decode`, `VideoExporter`, `LoopRender`, `LoopExtend`, `MicCapture`, `PlaybackCapture`, `MusicMoments`, `EventRecipe`, `MacroStore`, `StageRunEffect` do inside; `SoundTuning.clamped()` (`engine/audio-core/.../SoundTuning.kt:30`) was only seen as a copy, not read in full. Graphify was not used; callers were confirmed with Grep.


---

## 08-ui-b: `app/src/main/java/dev/geode/ui/*.kt`, files 42–82 (`FolderTree.kt` … `PlayerSettingsController.kt`)

41 files, 10,579 lines. All read in full, except the import blocks of `LibraryScreen.kt` (lines 36–74) and `PlayerScreen.kt` (lines 1–75), which hold imports only. All paths below are relative to `app/src/main/java/dev/geode/ui/` unless they start with another root.

Class counts: shell 29, mixed 10, engine 2, duplicate 0 (as a whole file; duplicated pieces are listed in section 5), dead 0.

### 1. Per-file table

| File (lines) | Class | Engine logic: function, line range, what it decides | Thread | Native calls (`GeodeNative.x`) | Native owner in `core/` | What must stay Kotlin and why |
|---|---|---|---|---|---|---|
| `FolderTree.kt` (53) | engine (library logic, not visual) | `rows` 4–17, `widenCollisions` 19–34, `fallBackToWholePath` 36–46: the shortest unique label per folder path and the folder order (case-insensitive). | Main, inside `remember` in composition (`LibraryScreen.kt:1330`) | none | new: `core/library/` beside `Search.cpp` (folder labels) | Nothing. Pure logic. |
| `GeodeAppState.kt` (195) | shell | none (navigation state, start screen) | Main | none | – | Compose state, `rememberSaveable`. |
| `GeodeViewModelFactory.kt` (14) | shell | none | Main | none | – | Hilt. |
| `HelpSettings.kt` (53) | shell | none | Main | none | – | Compose. |
| `KnobLinks.kt` (188) | shell | none. Draws `MacroStore.controls()` / `editor()` (native answers) and calls `addLink`/`removeLink`/`newDraftFor`. | Main | none direct (through `data/MacroStore`) | `core/viz/MacroBank`, `MacroControls.cpp` (exists) | Compose dialog. |
| `LibraryBrowse.kt` (212) | mixed | `nativeSort` 74–81: sort-id table mirroring `geode_library_index_order`. `browse` 162–192: unpacks the packed search result (3 counts, then rows), groups by album/artist/folder, rebuilds album and artist hits by filtering the title-ordered list. `formatDuration` 204–208. | `Dispatchers.Default` (via `LibraryViewModel.kt:166`); `search` also from `PlayerSession.playFromSearch` on Main (`PlayerSession.kt:568`) | `libraryIndexCreate`, `libraryIndexDestroy`, `libraryIndexOrder`, `libraryIndexSearch` | `core/library/Search.cpp` (exists); grouping is new: `core/library/` | `Cleaner` handle ownership, `DeviceTrack` list mapping. |
| `LibraryDuplicates.kt` (55) | engine (library logic) | `find` 24–31, `splitByDuration` 33–44 (2,000 ms slack, line 22), `normalise` 46–54 (regex noise strip): which tracks are the same recording. | `Dispatchers.Default` (`LibraryViewModel.kt:196–200`) | none | new: `core/library/Duplicates` | Nothing. |
| `LibraryEmpty.kt` (177) | shell | none | Main | none | – | Permissions, `MediaProjectionManager`. |
| `LibraryScreen.kt` (1473) | shell | UI-only helpers: `tapTrack` 470–483 (which queue action a tap does), `playlistDropIndex` 1211–1220, `playlistRowShift` 1222–1232 (drag reorder geometry). No parameter, sound or picture decision. | Main | none | – | Compose, activity result launchers. |
| `LibraryViewModel.kt` (312) | shell | none. Debounce 150 ms (41, 156), `mapLatest` pipeline. | Main; browse and duplicates on `Dispatchers.Default` (131, 166, 199) | none | – | ViewModel, `SavedStateHandle`. |
| `ListeningTracker.kt` (111) | shell (data) | `accrueListenTime` 65–78: listen time per track, a tick counted only when 1..5,000 ms (109). Statistics, not sound. | Main (called from the 500 ms poll); writes on `storeScope` | none | – (could move to `core/library` later; not engine) | Media3 `Player`, stores. |
| `LookSettings.kt` (273) | shell | none. UI-theme literals only: accent intensity `0.5f..1.5f` (78), text scale range from `GeneralSettings` (118). `accentFollowsVisual` is only a stored switch here. | Main; one prefs read on `Dispatchers.IO` (263) | none | – | Compose. |
| `LoopRenderRequest.kt` (20) | shell | none (DTO) | – | none | – | `Uri`. |
| `LoopRenderSheet.kt` (266) | mixed | Kotlin literals for the loop render's parameters: default loop length = `LoopSpec.MIN_LOOP_MS` (62), default crossfade (63), drift off by default (64), drift hue default `0.15f` (65), default stops `4f` (66), `warmth = 0f` fixed (90), hue range `-0.5f..0.5f` (222), stops `2f..MAX_STOPS` with `steps = MAX_STOPS - 3` (228–229), loop and crossfade ranges from Kotlin `export/LoopRender.kt:72–76,132` (205, 211). `buildRequest` 81–95 converts seconds to ms and builds `TimeOfDayDrift`. | Main | none | `core/output/LoopPlan.hpp` already has `LoopSpec` and the drift stops (ported from the Kotlin); the UI still reads the Kotlin constants | Dialog, document picker, share intent. |
| `LyricsSettings.kt` (101) | shell | none | Main | none | – | Intents. |
| `MacroEditor.kt` (828) | mixed (thin) | `AmountSlider` range `-1f..1f` literal (527) and percent mapping (512); `curveLabel` 482–489: `when` over native curve wire names `LINEAR`, `EASE_IN`, `EASE_OUT`, `S_CURVE`; `targetLabel` 613–620: looks the wire name up in Kotlin `LfoTarget.entries` for a label; `TargetPicker` 554–559 groups targets by Kotlin `CustomizeCategory`; `AddRandomRow` 360 clamps the count to native `addMax`; name length checked in bytes against native `maxNameBytes` (404, 751). Everything else (caps, reach, canLink, roll, reset) is native via `MacroStore`. | Main | none direct (through `data/MacroStore`) | `core/viz/MacroBank.cpp` (exists); labels and the ±1 range should come from its editor text | Compose. |
| `MacrosDrawer.kt` (179) | shell | none. Lock set from `FreeTier.freeCustomizeControls(CustomizeMapTable.basicControls)` (125); `store.rollValues(sceneId)` (158) is native. | Main | none direct | `core/viz/MacroBank` | Compose, paywall. |
| `MainActivity.kt` (415) | shell | `pipAspectFraction` 79–90 clamps to the platform's PiP range (93–94): a platform rule, not a picture rule. | Main | none | – | Activity, PiP, intents, splash, orientation. |
| `MilkDropPage.kt` (461) | shell | none. `selectMilk` 449–456 calls `viewModel.selectScene(SceneIds.MILKDROP)` then `visualizerRenderer.loadMilkPreset(path)`. Shows the texture card only when `viz.sceneId == SceneIds.MILKDROP` (143). | Main; file reads in `produceState` on `Dispatchers.IO` (109–113) | indirect: `vizLoadMilkPreset` (`engine/scenes/.../bridge/NativeViz.kt:160`) | `core/viz` projectM host (exists) | Compose, SAF pickers. |
| `MilkImportController.kt` (227) | shell (data layer sitting in `ui/`) | `listAsync` 55–84 fixes the preset order (newest first, 73) that the stage step and Auto's pool walk; `importMilkPresetBlocking` 142–164 names files (`stem N.milk`); `openableMilkName` 124–139 is the untrusted-uri check. File handling, no visual decision. | `Dispatchers.IO`, results posted on Main | none | – | `ContentResolver`, `DocumentFile`, files. |
| `ModTriggerRows.kt` (43) | shell | `MomentTriggerRow` 41–42 maps `moment` ↔ dropdown index by `+1/-1` over Kotlin `MusicMoment` ordinals. | Main | none direct | `core/viz/Reactions.hpp` `Moment` | Compose. |
| `ModulationController.kt` (324) | mixed | See section "Heaviest engine items". `editSceneParams` 98–107 and `commitSceneParams` 110–116, `undo`/`redo` 118–134, `pushUndo`/`trim` 292–301: the param undo history (depth 64, 700 ms fold; 313–315). `randomizeParams` 144–162: seed from `kotlin.random.Random` (152), adds `#PRO` to the lock list (151), rule "also roll the waves when the roll is everything or Music, unless basics only" (160). `randomizeAllRoll` 170–182 (same wave rule, 181). `randomizeWaves` 189–216: packs 4-per-slot wave floats and unpacks 11 fields by hand-written offsets and enum ordinals (198–213). `resetCategory` 223–229 (Music also drops routes, 225). `resetAll` 242 uses Kotlin `SceneParams.DEFAULT`. `setAdsr`/`setLfo` 244–268 pad lists to `AdsrEngine.COUNT`/`LfoEngine.SLOTS`. `setHandReactions` 277–284 caps at `ReactionRoute.MAX_ROUTES`, zeroes the mask when there is no list. `rerollReactions` 287–290: `reactSeed + 1`. | Main | `directorKeepSet`, `paramsRandomize`, `customizeRandomize`, `randomizeAllRoll`, `wavesRandomize`, `customizeReset`, `customizeBorrow`, constant `HAND_ALL` | exists: `core/viz/CustomizeMap`, `ParamRandomizer`, `RandomizeAll`, `WaveRandomizer`; new: `core/signal/` param edit history and the "what a roll also rolls" rule | StateFlows for Compose, `SharedPreferences` (`locked_params`), `LfoStore`. |
| `MusicMoments.kt` (147) | mixed | `MusicMoment` 19–32 and `MusicAction` 39–48: Kotlin enums whose ordinals are the native wire values. `MAX_STRENGTH = 2f` (68) copies `kMaxMomentStrength`. Op codes 70–72. `actionsFor` 76–81: falls back to "every action fits" (`bits = -1`) when native throws. `rows` 84–99 and `actions` 143–146 unpack 5 floats per moment and bit sets. `edit` 124–139. | Main | `musicActionsFor`, `musicState`, `musicEdit`, constant `MUSIC_STATE_FLOATS` | `core/viz/Reactions.hpp`, `core/viz/ReactionDna` (exist) | String resources for labels. |
| `MyVideosTab.kt` (228) | shell | none | Main; thumbnails in `produceState` (198–206, one retry after 2 s) | none | – | MediaStore, snackbar. |
| `NavHistory.kt` (81) | shell | none (back-route list) | Main | none | – | Navigation. |
| `NoteResponseSettings.kt` (30) | shell | none (one switch into `container.noteResponse`) | Main | none direct | `core/viz/NoteResponse` | Compose. |
| `OnboardingScaffold.kt` (133) | shell | none | Main | none | – | Compose. |
| `OpenedAudioIntent.kt` (56) | shell | none (route decided by `model/OpenedAudio`) | Main | none | – | Intent. |
| `PaletteMaker.kt` (390) | mixed | `paletteChipIndex` 133–143 and `paletteChipSelected` 145–157: which palette a slot holds, built-ins first then saved ones offset by `SceneParams.PALETTES.size`. `SecondPaletteStepper` 218: steps `palette2` with `floorMod` over the built-in count and clears the custom slot. `startingStops` 235–236 and `STARTING_STOPS = 4` (238): how many stops a hue ramp becomes. `SWATCH_SAMPLES = 24` (159). `YourColoursCard` 260–272: add copies the selected stop, remove drops it, cap `PaletteStore.MAX_STOPS` (358). Colour maths itself is native. | Main; store I/O on `paletteIo` (77); `requestStill` callback posted back with `view.post` (276–277) | `paramsPalettePreview`, `paletteRamp`, `paletteStopsFromHues`, `colourHex`; indirect `vizStillRequest` | `core/viz/Palette.cpp`, `ArtPalette.cpp` (exist); slot-selection rule new: `core/viz/Palette` | Compose, `Bitmap` for the eyedropper, `PaletteStore` files. |
| `PendingDeletes.kt` (58) | shell | none (4 s undo window) | caller's scope (Main) | none | – | Coroutines for UI undo. |
| `PendingExport.kt` (20) | shell | `range` getter 19: zero duration means "whole track". | – | none | – | DTO. |
| `PerformanceSettings.kt` (215) | mixed | `PerformanceModeControls` 73: picking a preset keeps the user's `liquidHot`. `PerformanceDetailControls` 92–96 and 115–123 offer `PerformanceSettings.RENDER_SCALES` and `MARCH_SCALES`, Kotlin lists `0.5, 0.75, 1` in `engine/scenes/src/main/kotlin/dev/geode/render/PerformanceSettings.kt:43–44`; the presets (`PerformanceMode.entries[it].settings`) are Kotlin too. FPS caps are native (103–111, 176–180). | Main | `liveFpsChoices`, `liveFpsCap`, constants `LIVE_FPS_FOLLOW`, `LIVE_FPS_MAX_CHOICES` | `core/viz/frame/LiveFps` (exists for fps); new: `core/signal/EngineSettings` for scale lists and presets | `Display.Mode` read (162–165), Compose. |
| `PermissionRequest.kt` (159) | shell | `statusAfterRefusal` 51–60: Android permission state only. | Main | none | – | Runtime permissions. |
| `PlaybackFades.kt` (95) | mixed | `fadeOutThen` 41–55: ramps to silence, then waits `fadeMs + PlaybackEngine.sinkLagMs()` before pausing or skipping. `togglePlayPauseFaded` 65–79: fade in only after our own pause. `skipFaded` 81–90, `ensureAudibleAfterExternalPlay` 92–94. It decides when audio fades; the ramp is native. | Main (coroutine `delay`) | indirect: `playoutLagMs` (`playback/PlaybackEngine.kt:250`), the DSP ramp through `PlaybackEngine.rampUiFade` (`:178`) | new: `core/audio/dsp` fade sequencer (ramp exists as `geode_dsp_ramp_to`) | Media3 `player.pause()` / play call. |
| `PlaybackRepository.kt` (31) | shell | none | – | none | – | Interface over flows. |
| `PlaybackSettings.kt` (358) | mixed | Audio parameter ranges and steps as Kotlin literals: speed `0.5f..2f`, step `0.05` (226–228); pitch `-6f..6f`, step `0.5` (235–237); fade `0..PlayerPrefs.MAX_FADE_MS` (6,000), step 250 (111–113); crossfade 0–12 s in 1 s steps (192–206, constants in `data/PlayerPrefs.kt:57–61`); ReplayGain preamp ±15 dB, step 0.5 (288–290); picture delay step 10 ms (94), bound native (77); sleep choices `0,15,30,45,60` (26). Snapping is Kotlin `PlaybackMath.snap` (`engine/scenes/src/main/kotlin/dev/geode/analysis/PlaybackMath.kt:14`). | Main | `playoutMaxOffsetMs` | new: `core/audio/dsp` settings schema (ranges, steps, defaults); `core/sound/interp/PlayoutClock` owns the delay bound | Compose. |
| `PlayerModels.kt` (29) | shell | none | – | none | – | UI state. |
| `PlayerPanels.kt` (441) | shell | none. `TrackSeekBar` draws position; `LyricsPanel` 197 reads `lyrics.indexAt(positionMs())`; `queueRowKeys` 403–410; `playlistNameAccepted` 412–415. | Main | none | – | Compose canvas. |
| `PlayerScreen.kt` (1027) | shell | none. `formatClock` 965–971, `formatTimeLeft` 974–977; quick sleep timer literal 30 min (894). | Main | none | – | Compose, permissions. |
| `PlayerSession.kt` (994) | mixed | `pollPlaybackState` 974–987: a 500 ms loop (991) that calls `queueController.enforceAbLoop()`, `autoVisuals.advanceVizPlaylist()`, `autoVisuals.advanceRandomMode()` (the tick that decides when the visual changes), `bookmarks.onPoll`. `captureController` host `resetSound` 125–130: flips `tap.owns`, starts a ring epoch, tells native the source. `randomizeEverything` 716–746: seed from `Random.nextLong()` (729), builds the locked-style list from Kotlin `Catalogue.styles` (731), applies native's pick. Widget style step 901–908: order from Kotlin `stageStyleOrder()` (`VisualsStage.kt:504`), walk native. `exportController` host `look` 789–800: builds the export's look with `liveLook(...)` (`EnginePlumbing.kt:49`). Pro gates on `selectScene`/`applyPreset` 653–706. `reportLiquidMode` 694. | Main (`Dispatchers.Main.immediate`, 95); stores on a single IO lane (100) | `soundSetPlayerSource`, `randomizeAllPick`, `stageStepIndex` | exists: `core/viz/RandomizeAll`, `StageStep`, `AutoTimer.hpp`; new: native Auto tick and A–B loop on the sample clock (`core/output` / `core/sound`) | Media3 player wiring, `Player.Listener`, services, stores, Pro gate. |
| `PlayerSettingsController.kt` (107) | shell | `cycleRepeatMode` 65–74 (off → all → one). `setPlayerPrefs` 43–47 clamps through `PlayerPrefs.coerced()` (`data/PlayerPrefs.kt:44`). | Main; writes on the app scope | none | – | Media3 `Player`, `AudioFxController`. |

### Heaviest engine items

1. `ModulationController.kt:98–134, 292–305`: the parameter undo/redo history (64 entries, 700 ms fold) lives in Kotlin over `SceneParams` objects.
2. `ModulationController.kt:144–216`: the roll orchestration. Kotlin picks the seed, appends `#PRO`, decides that waves roll with "everything" or Music, and unpacks the rolled waves by fixed float offsets (198–213).
3. `PlayerSession.kt:974–987`: the 500 ms poll that drives Auto visuals (`advanceVizPlaylist`, `advanceRandomMode`) and the A–B loop.
4. `PlaybackFades.kt:41–90`: fade sequencing (when to fade, how long to wait for the sink) on a main-thread coroutine.
5. `PlaybackSettings.kt:77–113, 192–206, 222–238, 285–291` and `LoopRenderSheet.kt:62–66, 201–230`: audio and export parameter ranges, steps and defaults as Kotlin literals.

### 2. Threads started here

- `PlayerSession.kt:95` `scope` = `SupervisorJob() + Dispatchers.Main.immediate`. Runs every controller callback and the poll.
- `PlayerSession.kt:100` `storeScope` = `Dispatchers.IO.limitedParallelism(1)`. Prefs, favourites, presets, templates, editor, background writes.
- `PlayerSession.kt:974–987` poll loop on `scope`, every 500 ms (`POLL_INTERVAL_MS`, 991), for the life of the session.
- `PlayerSession.kt:895–900` widget style publish, 400 ms settle (`collectLatest` + `delay`).
- `PlayerSession.kt:901–909` widget step collector.
- `PlayerSession.kt:911` plays-recorded collector.
- `PlayerSession.kt:378, 387` lyrics load/attach on `Dispatchers.IO`.
- `PlayerSession.kt:566–567` voice search: `DeviceTrackQuery.query` on `Dispatchers.IO`, the search itself on Main.
- `PlayerSession.kt:875` `drainStoreWrites` on `container.appScope` + `Dispatchers.IO`, 2 s budget.
- `MilkImportController.kt:59, 90, 107, 117, 170, 215` `scope.launch(Dispatchers.IO)` per import or list; results to Main.
- `PaletteMaker.kt:77` `paletteIo` = `Dispatchers.IO.limitedParallelism(1)`; used at 97, 116 (`NonCancellable`).
- `LibraryViewModel.kt:131, 166, 199` `Dispatchers.Default` for browse and duplicate finding, once per library change or debounced query.
- `LibraryBrowse.kt:84` `Cleaner.create()` (its own daemon thread) frees the native index.
- `PlaybackFades.kt:48` one coroutine per fade-out on the session scope, `delay(fadeMs + sinkLagMs)`.
- `PendingDeletes.kt:40` one coroutine per pending delete, `delay(4_000)`.
- `ListeningTracker.kt:59` favourite toggle on `storeScope`.
- `PlayerSettingsController.kt:40, 45, 54` writes on `container.appScope`.
- `LookSettings.kt:263` one prefs read on `Dispatchers.IO`.
- `MilkDropPage.kt:109–113` `produceState` + `Dispatchers.IO` for texture links.
- `MyVideosTab.kt:198–206` `produceState` per thumbnail, one retry after 2,000 ms.
- `PlayerPanels.kt:267–276` `LaunchedEffect` that resumes list following 5 s after a drag.
- `MainActivity.kt:377` `lifecycleScope` collector for screen rotation; `:397` `decorView.post` to start Cast.
- No `Thread`, `HandlerThread`, `Executor` or `Handler` ticker is created in this scope.

### 3. Per-frame and per-audio-block Kotlin paths

None in this scope. No function here runs per video frame or per audio buffer, and there is no `queueEvent` call.

Closest paths:
- `PlayerSession.pollPlaybackState` (974–987), 2 Hz on Main: `refresh` → `listening.accrueListenTime` → `queueController.enforceAbLoop` → `queueController.refreshQueue` → `captureController.refreshExternalAudio`/`refreshMicState` → `autoVisuals.advanceVizPlaylist` → `autoVisuals.advanceRandomMode` → `bookmarks.onPoll`.
- `PlayerSession.reportLiquidMode` (694) is written from the renderer side (its KDoc calls it "a GL-thread fact"); the caller is in `AppRoot`, outside this scope. Not verified how often it fires.
- `PaletteMaker.kt:276–277`: `nativeViz.requestStill { still -> view.post { ... } }`. The callback arrives off the main thread (hence `view.post`); one shot per tap.

### 4. Duplicated drivers

- Fade and play/pause rule twice: `PlayerSession.togglePlayPause` 608–617 and `PlaybackFades.togglePlayPauseFaded` 65–79 both carry the "pause, or `Util.handlePlayButtonAction`" branch.
- Capture consent flow twice: `LibraryEmpty.kt:131–153` and `PlayerScreen.kt:809–839` (mic permission, `RequestMultiplePermissions`, `MediaProjection` launcher, `PlaybackCaptureService.start`).
- Collapsible card three times: `MilkDropPage.kt:339–411` (`PageCard`), `PlayerScreen.kt:646–702` (`CollapsibleCard`), `LibraryScreen.kt:1366–1473` (`LibraryFoldersCard`).
- Wave (LFO) unpacking twice: `ModulationController.kt:198–213` unpacks by hand what `ModConfigCodec.packLfo` packs (`render/bridge/ModConfigCodec`); the field order is written in two places.
- Seeds for native rolls come from `kotlin.random.Random` in three places: `ModulationController.kt:152`, `:196`, `PlayerSession.kt:729`.
- `#PRO` lock string built in two functions: `ModulationController.kt:151` and `:176`.
- Loop render constants exist in Kotlin (`export/LoopRender.kt:72–76, 132`) and in `core/output/LoopPlan.hpp`; the sheet reads the Kotlin copy.
- Moment and action enums exist in Kotlin (`MusicMoments.kt:19–48`) and in native (`core/viz/Reactions.hpp`, `ReactionDna`), tied only by ordinal. `MAX_STRENGTH` (`MusicMoments.kt:68`) copies `kMaxMomentStrength`.
- Library sort ids: `LibraryBrowse.kt:74–81` mirrors native's ids by number.
- Stage style order: the widget step (`PlayerSession.kt:904`) uses Kotlin `stageStyleOrder()` (`VisualsStage.kt:504`, from the Kotlin `Catalogue`), while the walk itself is native (`stageStepIndex`).
- Look push for export: `PlayerSession.kt:789–800` builds a `LookSource` with `liveLook(...)`; the live view builds its look elsewhere (`EnginePlumbing.kt:49`, outside this scope) from the same inputs.

### 5. Data crossing the JNI boundary (engine and mixed files)

- `LibraryBrowse.kt:48–58`: to native once per library change: `Array<ByteArray?>` of 3 cells per track (title, artist, album UTF-8), `LongArray` durations, `LongArray` added seconds; returns a `Long` handle. `libraryIndexOrder(handle, Int)` → `IntArray` of row numbers. `libraryIndexSearch(handle, ByteArray)` → `IntArray` (3 counts, then rows), once per debounced query.
- `ModulationController.kt:88`: `directorKeepSet(String)` newline-joined keys, per lock toggle.
- `ModulationController.kt:155, 157`: `paramsRandomize` / `customizeRandomize(sceneId: String, category: String?, locked: String, seed: Long, input: FloatArray[SceneParamsCodec.FIELDS], out: FloatArray[FIELDS])` → `Boolean`. Per tap.
- `ModulationController.kt:178`: `randomizeAllRoll(sceneId, locked, seed, true, before FloatArray, base FloatArray, out FloatArray)` → `Boolean`.
- `ModulationController.kt:196`: `wavesRandomize(sceneId, locked, seed, onlySteady: Boolean, params FloatArray, input FloatArray[SLOTS × 11], out FloatArray)` → `Boolean`. Out fields per slot: enabled, source, target, wave, rateSeconds, depth, polarity, curve, beats, knob, moment.
- `ModulationController.kt:227, 238`: `customizeReset(category, params, out)`, `customizeBorrow(category, params, from, out)`.
- `MusicMoments.kt:79`: `musicActionsFor(sceneId)` → `Int` bit set. `:89`: `musicState(sceneId, seed, FloatArray? routes, mask Int)` → `FloatArray` of 5 per moment × 10 moments, per recomposition of the Music page. `:136`: `musicEdit(sceneId, seed, routes, mask, op, moment, action, value, out IntArray[2])` → `FloatArray?` routes; `out[0]` mask, `out[1]` has-list flag.
- `PaletteMaker.kt:170`: `paramsPalettePreview(FloatArray params, second: Boolean, 24)` → `IntArray` of 0xRRGGBB, called in composition for each swatch (including one per saved palette row, 381). `:174` `paletteRamp(IntArray stops, 24)` → `IntArray`. `:236, 286, 308` `paletteStopsFromHues(base, range, 4)` → `IntArray`. `:332` `colourHex(Int)` → `String`.
- `PerformanceSettings.kt:178`: `liveFpsChoices(FloatArray refreshRates, IntArray out[5])` → count. `:110` `liveFpsCap(Int stored, FloatArray)` → `Int`.
- `PlaybackSettings.kt:77`: `playoutMaxOffsetMs()` → number, per composition of the row.
- `PlayerSession.kt:129`: `soundSetPlayerSource(Boolean)` per source change. `:734–741`: `randomizeAllPick(seed Long, chosen String, locked String, presetSceneIds String, milkCount Int, picks IntArray[2])` → `String?` style id. `:906`: `stageStepIndex(BooleanArray allowed, Int index, Int step)` → `Int`.
- `MacroEditor.kt`, `KnobLinks.kt`, `MacrosDrawer.kt`, `ModTriggerRows.kt` cross only through `data/MacroStore` (outside this scope).

### 6. Saved formats touched

- `ModulationController.kt:317–322`: pref key `locked_params` (string set) in the `GeodePrefsFiles.MODULATION` file; the values are Customize control keys.
- `ModulationController.kt:311`: lock token `#PRO` (wire string to native).
- `ModulationController.kt:58–70, 215, 253, 266, 283`: `LfoStore` (general prefs) holds the waves, envelopes, reaction routes and hand mask. Wave fields are stored through enums `ModSource`, `LfoTarget`, `LfoWave`, `ModPolarity`, `ModCurve`; `randomizeWaves` reads them back from native by ordinal (202–208), so their order is a wire format.
- `MusicMoments.kt:19–48`: `MusicMoment` and `MusicAction` ordinals are wire values; LFO/ADSR `moment` fields store `MusicMoment` ordinals, −1 for none (`ModTriggerRows.kt:42`).
- `KnobLinks.kt:185`: section key `@macro_editor`.
- `MacroEditor.kt:484–487`: curve wire names `LINEAR`, `EASE_IN`, `EASE_OUT`, `S_CURVE`.
- `GeodeAppState.kt:133–141`: saved-state names of `GeodeDestination` and `StudioSection`; the removed `"EDIT"` still loads as `RENDER` (57–61). `StudioPrefsStore.lastTab()` (158) stores the same names. `BehaviourPrefs.lastScreen` stores `StartScreen` (168).
- `LibraryViewModel.kt:34–35`: saved-state keys `library_query`, `library_sort`; `LibrarySort` names are also stored in `GeneralSettings.librarySort` (`LibraryBrowse.kt:26`).
- `MilkImportController.kt:31–33, 64`: folders `files/milk`, `files/milk-builtin` (deleted on relink), `files/milk/textures`; file names `<stem>.milk` and `<stem> N.milk` (152); the texture link records (`.links/<stem>`, per `MilkDropPage.kt:108`).
- `PaletteMaker.kt`: `PaletteStore` palettes (id, name, stops, `baseHue`, `hueSpan`); `SceneParams` fields `palette`, `palette2`, `customPaletteId`, `customPalette2Id`, `paletteStops`, `paletteBase`, `paletteRange`. Built-in palette index is stored as an `Int`, so the order of `SceneParams.PALETTES` (native `ParamSchema.paletteNames`) is a saved format.
- `PlayerSettingsController.kt:55` and `PlaybackSettings.kt`: `PlayerPrefs` fields (`shuffle`, `repeatMode` as Media3 ints, `fadeMs`, `crossfadeMs`, `speed`, `pitchSemitones`, `replayGainMode`, `replayGainPreampDb`, `replayGainClipGuard`, `sleepTimerMinutes`, `sleepFinishTrack`, `pictureDelayMs`, `skipSilence`, `nativeOutput`, `bitPerfect`, `keepScreenOn`, `autoResume`, `resumeLongTracks`, `pauseOnNoisy`).
- `PerformanceSettings.kt:73, 149`: `PerformanceMode` and `LiquidHotPolicy` selected by ordinal in the UI; `fpsCap` stores `-1` for Follow device.
- `PlayerSession.kt:320–325`: `RecordPrefsStore`, `WatermarkPrefsCodec`, `ExportPrefsStore` in the general prefs file. `ListeningTracker.kt:89`: the saved session (queue, index, position).
- `LookSettings.kt:259`: `BootAnimationStore` in general prefs.
- UI-only saved keys: card keys `presets`, `links` (`MilkDropPage.kt:458–459`), `up_next`, `quick` (`PlayerScreen.kt:333–334`).

### 7. Surprises

- `ModulationController` is not a modulation engine. It runs no LFO or ADSR. It is the Customize edit path: undo history, lock set, and the calls into the native rolls. The undo stack holds whole Kotlin `SceneParams` values, so the "one live parameter set" has a Kotlin-side history that native does not know about.
- `ModulationController.resetAll` (242) resets to Kotlin `SceneParams.DEFAULT`, while a category reset (227) asks native. Two sources for defaults.
- `ModulationController.rerollReactions` (289) computes the next DNA variant as `reactSeed + 1` in Kotlin.
- `MusicMoments.actionsFor` (78) fails open: if native throws, every action is offered.
- `MusicMoments.rows` calls native in composition for each recomposition of the page; `PaletteMaker.savedSwatch` (172, used at 381) packs a full `SceneParams` and calls native once per saved palette per recomposition, on Main.
- `PlayerSession.pollPlaybackState` is the only clock for Auto visuals and the A–B loop in this scope: 500 ms resolution, main thread, stops when the session is torn down. The wallpaper and dream have no `PlayerSession`, so whatever advances them is a second driver (outside this scope, not verified here).
- `PlaybackFades` waits `fadeMs + sinkLagMs` on a main-thread `delay` before pausing or skipping; the timing of an audible event depends on main-thread scheduling.
- `PlaybackSettings` has the speed, pitch, fade and preamp ranges as literals; `PlayerPrefs.coerced()` (`data/PlayerPrefs.kt:44–53`) clamps fade, crossfade and preamp, but the speed `0.5..2` and pitch `±6` bounds appear only on the sliders (226–237). Not verified whether `coerced()` also clamps speed and pitch (lines 45–49 and 52 not read).
- `LoopRenderSheet` hard-codes `warmth = 0f` (90): the drift's warmth has no control.
- `MacroEditor.targetLabel` (617) and `TargetPicker` (556) depend on Kotlin `LfoTarget` labels and Kotlin `CustomizeCategory` titles (`CustomizeCategory.kt:25–39` holds titles and summaries as Kotlin strings), so a native target without a Kotlin enum entry shows its control key or wire name.
- `MilkImportController`, `PlaybackRepository`, `ListeningTracker`, `PlayerSettingsController` and `PendingDeletes` are data or domain classes that live in `ui/`.
- `MilkImportController.listAsync(relink = true)` deletes `files/milk-builtin` every time Auto or randomize-everything lists presets (63).
- `PerformanceSettingsContent` (`PerformanceSettings.kt:49–57`) is used only by the two `@Preview`s in the same file; not dead in the strict sense, preview-only.
- No `Lfo*`, `Layer*` or `Loop*` engine class exists in this file range; `LoopRender*` here is only the dialog and its request.
- Could not verify: the caller and rate of `reportLiquidMode`; whether `PlayerPrefs.coerced()` clamps speed and pitch; the thread `requestStill`'s callback arrives on (inferred from `view.post`).


---

## 09-ui-c: `app/src/main/java/dev/geode/ui/*.kt`, files 83–123 (`PlayerViewModel.kt` … `VisualsViewModel.kt`)

41 files, 11,642 lines, all read in full. Paths below are relative to `app/src/main/java/dev/geode/ui/` unless they start with another root. `PlayerSession.kt` sorts before `PlayerViewModel.kt` (item 82), so it is not in this slice; where it is the driver of something here it is cited.

Summary of class counts: shell 28, mixed 12, engine 0, duplicate 0, dead 0 whole files (dead members listed under Surprises).

### 1. Per-file table

| File (lines) | Class | Engine logic: function, line range, what it decides | Thread | Native calls (`GeodeNative.x`) | Native owner in `core/` | What must stay Kotlin and why |
|---|---|---|---|---|---|---|
| `PlayerViewModel.kt` (353) | mixed (thin) | `capRange` 324–333: clamps the export range to the free tier's cap (`ExportRange(0, capMs)` or shortens the range). `openPaywallIfLocked` 336–343: `minOf(aspect.width, aspect.height)` as the gated short side. `pinnedWatermark` 315–318: picks the watermark a render keeps. Everything else is a one-line forward to `PlayerSession`. | main | none | new: `core/output` (an "export request" resolver beside `EncodePlan`/`RecordPlan`: range cap, short side, watermark pin) | Hilt ViewModel, `StateFlow` exposure, paywall navigation (billing). |
| `PredictiveDismiss.kt` (49) | shell | UI scale/alpha of a dismissing sheet (0.92, 0.4 at 13–14): UI motion only. | main | none | n/a | Compose `PredictiveBackHandler`. |
| `PreferencesSettings.kt` (158) | shell | none (stores a watermark choice; placement maths is native per KDoc 16–18). | main | none | n/a | Compose rows, prefs. |
| `PresetActions.kt` (410) | mixed | `builtInPresetSceneFamily` 355–360 and `builtInPresetMatchesScene` 362–365: decides which style family a built-in preset belongs to (`isCymatics → CYMATICS`, `isFluid → FLUID`), i.e. a style-id rule in Kotlin. `presetReplaceTarget` 346–352, `folderNameError` 326–338: name rules (data, not engine). | main | none | new: `core/viz/Catalogue` (family-of-style query; `Catalogue.picker` already carries families) | Dialogs, `ClipboardManager`, `FileProvider`, share `Intent`. |
| `PresetLibraryController.kt` (287) | shell (data) | none about a parameter. It builds a `Preset` from `sceneId`, `attack`, `decay`, `params`, shader, milk text (98) and name-uniquing (169–193, 233–247). Rule `sceneId == MILKDROP` → read the .milk (92, 137, 257). | main; `storeScope` + `Dispatchers.IO` for files | none | n/a (preset JSON stays a store) | `DocumentFile` SAF mirror, `ContentResolver`, files. |
| `PresetsPage.kt` (714) | mixed (thin) | 134–149: `onStage`/`looks` = `p.sceneId == viz.sceneId && p.params == viz.params` (which preset is "on stage"; MilkDrop special-cased at 146). 257: reads `visualizerRenderer.customShaderFor(sceneId)` (wraps `GeodeNative.shaderOverride`). | main | indirect `GeodeNative.shaderOverride` (engine/scenes `VisualizerRenderer.kt:149`) | new: `core/viz` "look equals stage" compare (same rule again at `VisualGestures.kt:802–805`) | Compose list, dialogs, SAF picker. |
| `QueueController.kt` (433) | shell (playback glue) | Not visual engine. A–B loop: `cycleAbLoop` 408–418 (`MIN_LOOP_MS = 1000` at 431), `enforceAbLoop` 424–428 seeks the player back at the loop end, driven by `PlayerSession`'s 500 ms poll. `previous` 383–397 uses `PlaybackQueue.PREV_RESTART_MS` (3 s). `seekTo` 403–406 fraction → ms. | main; `resolveScope` (IO) for titles | none | A–B loop is a playback timing decision: new `core/audio` transport rule if the owner counts Media3 transport as engine; otherwise stays | Media3 `Player`, `MediaItem`, `ContentResolver`. |
| `RecordController.kt` (314) | mixed | `start` 118–147: reads the surface size and opens the recorder; ticker 139–145 polls `recorderRecordedMs` every 250 ms. `open` 155–188: creates the native recorder with `prefs.fps`, `prefs.maxLengthSec`, `limits.capMs`, opens `RecordEncoder` (MediaCodec) at `prefs.quality.shortSide`. `tee` 191–201 and `closeTee` 204–213: `queueEvent` onto the stage's GL thread to start/stop the frame copy (`renderer.recordStart/recordStop`), with a 2-owner refcount on the native handle (90–94). `finished` 215–245: `recordWorthKeeping`, stop reason → note. | main; IO for open/place; GL thread (two `queueEvent` blocks); encoder thread callbacks hop back | `recorderCreate`, `recorderDestroy`, `recorderRecordedMs`, `recordNowNs`, `recorderStop`, `recorderStopped`, `recorderLimitMs`, `recordWorthKeeping`, consts `RECORD_STOP_*` | exists: `core/output/Recorder.*`, `RecordPlan.*`. To move: the start/stop sequencing and handle lifetime (new: a native "record session" that owns recorder + tee + watermark) | MediaCodec/MediaMuxer (`record/RecordEncoder`), MediaStore/SAF output (`RecordFiles`), Pro gate, strings. |
| `RecordSaveSheet.kt` (110) | shell | none | main | none | n/a | SAF `OpenDocumentTree`. |
| `RecordSettings.kt` (166) | shell | none: lengths, size estimate and "rate lowered" all come from native (86, 120, 123). Options lists are Kotlin data (`ExportQuality.entries`, `EXPORT_FPS_OPTIONS`, in `data/`/`export/`, out of scope). | main, IO for prefs load | `recordMaxLengths`, `recordEstimateBytes`, `recordRateLowered` | `core/output/RecordPlan` | prefs, paywall. |
| `RenderBar.kt` (255) | shell | none; shows `ExportRun.describeEta` (the wording of native's time left). | main | none | n/a | `Intent`s (view, share, DocumentsContract). `RENDERS_RELATIVE_PATH = "Movies/BeatRealms"` 224. |
| `RenderScopePicker.kt` (204) | shell | none | main | none | n/a | Compose. |
| `SafetyConsent.kt` (58) | shell | none | main | none | n/a | Compose. |
| `ScreenRotationSettings.kt` (30) | shell | none | main | none | n/a | Compose. |
| `SettingsComponents.kt` (303) | shell | none | main | none | n/a | Compose. |
| `SettingsRowKey.kt` (102) | shell | none (enum of row names; not persisted per KDoc 9–11). | – | none | n/a | – |
| `SettingsViewModel.kt` (97) | shell | none (forwards settings; audio FX setters forward to `session.settings`). | main | none | n/a | ViewModel. |
| `ShellChrome.kt` (661) | shell | none about the visual. Chrome sleep timer `LaunchedEffect` 181–187 (`fadeSeconds` → ms, one `delay`). | main | none | n/a | Compose, accessibility timeout. |
| `SmartPlaylistEditor.kt` (241) | shell | none (library rule editor). | main | none | n/a | Compose. |
| `StageKeys.kt` (46) | shell | `stageKeyGesture` 22–46: key code → `CanvasGesture` + step. Input mapping only; decides no parameter. | main | none | n/a | Android `KeyEvent` codes. |
| `StageRun.kt` (80) | shell (thin, correct shape) | `StageRun.restate` 66–79 packs lifecycle/PiP/second-screen bits and asks native whether the stage draws; then `view.onResume()/onPause()`. The rule is native. | main (lifecycle callback and `SideEffect`) | `stageRun(flags)`, consts `STAGE_STARTED`, `STAGE_RESUMED`, `STAGE_PIP`, `STAGE_SECOND_SCREEN`, `STAGE_DRAWS` | exists: `core/viz/StageRun.hpp` | Activity lifecycle, `GLSurfaceView.onResume/onPause`, `CastMirror` statics. |
| `StageWarmNote.kt` (46) | shell | Poll loop 29–42: every 2 s (`WARM_NOTE_POLL_MS` 46) asks native whether to say the warm note; stops after the first. | main | `thermalTakeWarmNote(capFps, fps)` | exists: `core/viz/frame/Thermal.hpp`, `core/viz/ThermalGovernor.*` | `Toast`. Could become a native → shell event instead of a poll. |
| `StudioRender.kt` (872) | mixed | `RenderLength.rangeFor` 678–687: slider fractions × track duration → ms, then `ExportRange.of` (the clamp is native, `core/output/ExportRange`). `ExportDefaults.freeVersion` 513–517: free-tier clamp (4K → 1080p, fps ≤ `FREE_MAX_FPS`). `ExportAspect.of(quality, ratio)` 341, 448, 456 (Kotlin frame-size maths in `export/`). 445: batch scene fallback `look.sceneId in renderer.availableSceneIds()` else current. 326–328: which presets can render offline (`customShader == null && milkPreset == null`). `fpsLabels` 863–872 hard-codes 24/25/30/50/60. | main | none directly; `renderer.exportSceneFactory`, `availableSceneIds` (engine/scenes) | new: `core/output` export request (range from fractions, free clamp, frame size); `EncodePlan.*` exists for encoder side | Compose, prefs stores, paywall, `NavigationBackHandler`. |
| `StudioViewModel.kt` (136) | shell | none (`keepNextRenderAsProject` 108–123 waits on export state and files a project). | main | none | n/a | ViewModel. |
| `StylesPage.kt` (405) | mixed (thin) | 110–114: builds `familyOf` index array from `Catalogue.picker`/`Catalogue.styles` (lists come from native through `engine/scenes` `Catalogue`). 125, 127: list order from native. `shuffle` 164–181: gathers `open[]`, milk count, calls native draw, then acts on `STYLES_ROLL_INSIDE` (rolls a .milk via `autoPick`). `load` 134–145: preview-then-paywall rule for a locked style. `sceneDisplayLabel` 398–402: fallback label by title-casing the id. | main | `stylesLead`, `stylesRandom`, `autoPick`, const `STYLES_ROLL_INSIDE` | exists: `core/viz/StylePick.hpp`. To move: the `familyOf` table (native already knows it) and the "roll inside" branch | Compose, `StyleShelfStore`, Pro gate. |
| `TemplateController.kt` (133) | shell (data) | none; captures `sceneId/attack/decay/params/shader/milk` into a `TemplateLook` 76–84. | main, `storeScope`, IO | none | n/a | files, `ContentResolver`. |
| `TemplatesSheet.kt` (249) | shell | none | main | indirect `shaderOverride` via `customShaderFor` 121 | n/a | SAF, share `Intent`. |
| `TextureController.kt` (77) | shell (data) | none (`TextureStore`, `MilkTextureLinks.relinkAll` in `data/`). | IO, main | none | n/a | files. |
| `TexturesPage.kt` (152) | shell | none; 57 calls `renderer.reloadCurrentMilkPreset()` after an import. | main | indirect | n/a | SAF picker. |
| `TrackColorController.kt` (103) | mixed (thin) | `applyArtworkPalette` 35–61: decodes artwork to 128 px, native extracts, then Kotlin `PaletteStore.applyGradient(params, baseHue, span)` (`data/PaletteStore.kt:82–95`, clamps 0..1 and writes `paletteBaseOverride/RangeOverride`) writes the scene params. `ART_SAMPLE_SIZE = 128`, `ART_PICK_SIZE = 1024` 100–101. | IO then main | `artPaletteExtract` (via `viz/ArtPalette.kt:17`) | exists: `core/viz/ArtPalette.*`. To move: applying the gradient to params (new: param edit in `core/viz` params/Customize) | `MediaMetadataRetriever`, `BitmapFactory`. |
| `TrackInfoEditor.kt` (242) | shell | none (`COMMON_GENRES` 41–42 is UI data). | main | none | n/a | MediaStore write consent. |
| `TutorialSettings.kt` (155) | shell | none | main | none | n/a | Compose. |
| `VisualGestures.kt` (827) | mixed | Gesture recogniser (input, stays): `GestureRead` 470–654, thresholds 808–827 (`SWIPE_DP 72`, `HOLD_FILL_MS 800`, `SWIRL_TURN_RAD 3.6π`, `PINCH_ZOOM 0.2`, `PINCH_TWIST_RAD 0.5`). Engine parts: `publish` 450–467 converts pointers to y-up NDC and caps at `TouchField.MAX_POINTS` (5) before `submitTouchPoints`; `pinchStep` 580–587 turns finger spread/angle into a per-event zoom factor and degrees that become Zoom/Rotation params; `rememberTouchPulse` 704–728 synthesises a touch stir (36 steps × 33 ms, radius 0.35 NDC, two points circling) in a coroutine; `stepPreset`/`openLooks`/`lookOnStage` 784–805 choose the next preset and decide which look is on stage by `sceneId && params` equality. | main (pointer input, coroutine for the pulse) | `stageStepIndex`; indirect `nativeViz.submitTouchPoints` | exists: `core/viz/TouchField.*`, `TouchTransform.hpp`, `StageStep.hpp`. new: `core/viz/TouchField` "pulse" (the synthetic stir) and an NDC entry that takes raw px + size | Compose pointer input, `ViewConfiguration`, insets, haptics, TalkBack actions. |
| `VisualSettingsController.kt` (182) | mixed | `setTransitionDuration` 140–143: clamp `0.3f..5f` (the same literal range is the slider at `CustomizeControls.kt:517`). `setTransitionId` 130–138: resolves `TransitionCatalog.builtIn(id)` into `transitionStyle`. `nudgeTransform` 107–118: applies native zoom/rotation to `SceneParams` (maths native). `applyPreset` 145–170: order of a preset apply (state, morph, shader, milk file job). `emitPresetMorph` 177–181. `sceneAfterPreview` 16–20 / `leavePreview` 81–87: which style stays after a preview. | main; `scope.launch` for the milk file | `touchZoom`, `touchRotation` | exists: `core/viz/TouchTransform.hpp`, `Transition*.hpp`, `TransitionLibrary.hpp`. new: transition length range in the native schema | `VizStateStore` persistence, `SharedFlow`s to the UI. |
| `VisualizerRepository.kt` (23) | shell | none (`liquidMode` ints 0–4 documented at 11). | – | none | n/a | – |
| `VisualsHub.kt` (505) | shell | Layout only (sheet anchors, panel width, icon room). `onPinch = viewModel::nudgeTransform.takeIf { visualsSettings.touchTransform }` 331. `setCustomizing` 177–181 tells Auto to hold while Customize is open (a "when the visual changes" input, decided in `AutoVisualsController`). | main | const `STAGE_STEP_STYLES` | n/a | Compose, Nav3, Toast. |
| `VisualsNavigation.kt` (195) | shell | none | main | none | n/a | Nav3 keys, savers. |
| `VisualsQuickSettings.kt` (763) | mixed (thin) | `TimerRows` 315–352 and `rememberTimerSource` 301–311: which of the two timed modes runs (Random vs playlist) and switching between them. Interval slider range `INTERVAL_RANGE` 752–753 = `AutoVisualsPrefsStore.INTERVAL_SEC` (`data/AutoVisualsPrefsStore.kt:57`, Kotlin literal `5..300`). `REACH_LABELS` 737–738: list position is the native Auto reach level (429). `SHEET_OPACITY_RANGE` 760 and fade slider 566–580 are UI glass values (not engine). | main | none | timer interval range and mode exclusivity: new `core/viz` Auto schedule (`Director`/Auto rules); reach levels should be read from native rather than a 3-item Kotlin list | Compose, stores. |
| `VisualsSheet.kt` (546) | shell | `UiBlurRegion` 198–229: converts the sheet's window bounds to 0..1 view fractions and calls `view.setUiBlurRegion(...)`, which `queueEvent`s to GL (`engine/scenes/.../VisualizerView.kt:106–115`). Strength is 0 or 1. Sheet geometry `sheetAnchors`/`settleHeight`/`panelRange` 471–514 is UI. | main → GL via `queueEvent` | indirect (`nativeViz.setUiBlurRegion`) | n/a (UI feeds a rect) | Compose layout. |
| `VisualsStage.kt` (591) | mixed (thin) | `stageStepTarget` 511–527: gathers flags and asks native. `stepStage` 533–552, `stepStageStyle` 560–569: act on native's pick. `stageStyleOrder` 504: `Catalogue.picker.values.flatten() - MILKDROP` (a style-id rule in Kotlin). `rememberStageActions` 489: `basicsOnly = !gate.allows(ADVANCED_CUSTOMIZATION)` for Randomize all; 486: playlist step falls back to style step. `VisualsPage.stepPage` 582–591 maps pages to native ints. `stageIconRoom` 389–409 is UI geometry. | main | `stageStepTarget`, `stageStepMilk`, `stageStepIndex`, consts `STAGE_STEP_*` | exists: `core/viz/StageStep.hpp`, `RandomizeAll.*`. To move: the style order minus MilkDrop | Compose, focus/keys, Pro gate. |
| `VisualsViewModel.kt` (369) | shell | none of its own: every modulation, randomize, reset, LFO/ADSR, reaction call forwards to `session.modulation` (`ModulationController`, outside this slice) at 143–201. `openMilkFile` 245–256 selects MilkDrop and emits the apply. `offerFor` 336–349 parses share links (data). | main; IO for `saveStill` | none | n/a | ViewModel, share-link parsing, `StillFiles`. |

### 2. Threads started here

- `QueueController.kt:62` `resolveScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)`: one batch of `ContentResolver` title queries per `open`/`playFrom`/`addQuick`; cancelled in `release` (100).
- `RecordController.kt:129` `scope.launch` (session main scope): opens the recorder; `withContext(Dispatchers.IO)` at 162 for native create, file open and `RecordEncoder.open/start`.
- `RecordController.kt:139–145` ticker `Job` on the session scope: `while (isActive)` every `TICK_MS = 250` ms, reads `recorderRecordedMs` + `recordNowNs` into UI state.
- `RecordController.kt:193` and `:207` `stage.queueEvent { … }`: GL thread, once per recording start and once per stop.
- `RecordController.kt:177–178` callbacks from `RecordEncoder`'s own thread (`onStopping` → `closeTee`, `onFinished` → `scope.launch`); the thread itself is created in `record/RecordEncoder` (out of scope).
- `RecordController.kt:303` `scope.launch { delay(4000) }` per note.
- `PresetLibraryController.kt:44–62, 90, 188, 217, 240, 264` `storeScope.launch` (store writes); `:106, :276` `scope.launch(Dispatchers.IO)` (SAF mirror copy/delete); `:160` IO read.
- `TemplateController.kt:69, 101, 108, 116, 126` `storeScope.launch`.
- `TextureController.kt:31, 42, 55, 72` `scope.launch(Dispatchers.IO)`.
- `TrackColorController.kt:41` `scope.launch(Dispatchers.IO)` (artwork decode + native extract); `:66` IO.
- `VisualSettingsController.kt:165` `scope.launch` for the preset's .milk path (cancels the previous job).
- `StudioViewModel.kt:115` `viewModelScope.launch` waiting for the export to end; `:88` `PendingDeletes(viewModelScope…)`.
- `VisualsViewModel.kt:168` `viewModelScope.launch` + IO for a still.
- `SettingsViewModel.kt:46, 82` `viewModelScope.launch`.
- Compose tickers: `ShellChrome.kt:181–187` (one `delay` per wake); `StageWarmNote.kt:29–42` (`while (true)`, 2 s, RESUMED only, ends after the note); `VisualGestures.kt:671–678` `HoldRing` (`withFrameMillis` loop for 800 ms while the ring shows; UI only); `VisualGestures.kt:709–724` touch pulse (36 sends at 33 ms to native); `VisualsSheet.kt:204–225` `snapshotFlow(bounds).collect` → `queueEvent` on each bounds change (every frame of a sheet drag); `StudioRender.kt:139–143` `snapshotFlow` of back progress (UI).
- No `Thread`, `HandlerThread`, `Executor` or `Handler` is created in this slice.

### 3. Per-frame and per-audio-block Kotlin paths

- None runs once per GL frame or per audio block in this slice.
- Per UI frame while active (main thread, Choreographer):
  - Two-finger touch play: pointer event → `visualGestures` `publishTo` (`VisualGestures.kt:235–245`) → `publish` 450–467 → `visualizerRenderer.submitTouchPoints` (`engine/scenes/.../VisualizerRenderer.kt:183`) → `nativeViz.submitTouchPoints`. One JNI call per pointer event (up to the display's touch rate).
  - Pinch: same event → `read.pinchStep()` 237 → `PlayerViewModel.nudgeTransform` 232 → `VisualSettingsController.nudgeTransform` 107–118 → 2 JNI calls (`touchZoom`, `touchRotation`) + a `VizStateStore` state update + `persist()` per pointer event (`setSceneParams` 97–100).
  - Sheet drag: `VisualsSheet.kt:206` → `view.setUiBlurRegion` → one `queueEvent` per changed bounds.
  - Touch pulse: 36 JNI calls over ~1.2 s.
- Polls: `RecordController` 250 ms (2 JNI calls per tick); `StageWarmNote` 2 s (1 JNI call + 2 pacer reads); `QueueController.enforceAbLoop`/`refreshQueue` are driven by `PlayerSession.pollPlaybackState` every 500 ms (`PlayerSession.kt:974–985`, `POLL_INTERVAL_MS` 991), which also drives `autoVisuals.advanceVizPlaylist/advanceRandomMode` (the style timer; outside this slice).

### 4. Duplicated drivers

- "Which look is on stage" (`sceneId == && params ==`) is written twice: `PresetsPage.kt:137` and `VisualGestures.kt:802–805`.
- Transition length range `0.3..5 s` is written twice in Kotlin: clamp at `VisualSettingsController.kt:141`, slider at `CustomizeControls.kt:517` (out of slice). Not found in `core/viz/Transition*`.
- Save-the-current-look capture is written twice: `PresetLibraryController.savePreset` 81–102 and `TemplateController.saveCurrentAsTemplate` 61–95 (same name cleaning, same `MILKDROP → read .milk file` rule, same fields).
- Name cleaning `replace(" · ", " - ").trim()` three times: `PresetLibraryController.kt:87`, `:177`, `TemplateController.kt:66`, beside `PresetFiltering.cleanName` (`model/`).
- Style step walk: `VisualGestures.stepPreset` 784–793 and `VisualsStage.stepStageStyle` 560–569 both build a `BooleanArray` and call `stageStepIndex`; `StylesPage.shuffle` 165 builds the same `open[]` array again for `stylesRandom`.
- Style order from the catalogue is derived three ways: `StylesPage.kt:112` (`Catalogue.styles`), `StylesPage.kt:110–114` (`Catalogue.picker` families + `familyOf`), `VisualsStage.kt:504` (`picker` flattened minus MilkDrop).
- Free-tier clamp of an export exists twice: `PlayerViewModel.capRange` 324–333 (length) and `StudioRender.freeVersion` 513–517 (size, fps), each next to `gate.blockingExportFeature` calls at `PlayerViewModel.kt:340`, `StudioRender.kt:457`, `RecordSettings.kt:61/74/81`.
- Run/visibility rule: one owner here (`StageRun.kt` → native). `VisualsHub.kt:379` separately computes `blurActive` (sheet shown or player on, not PiP) for the native UI blur.
- Recorder progress is polled (`RecordController.kt:139`) while the stop reason arrives by callback (`:178`): two channels for one native object.
- No EGL owner, encoder drain loop, decode loop or sound feed is in this slice (the record encoder is `record/RecordEncoder`).

### 5. Data crossing the JNI boundary (engine/mixed files)

- `RecordController`: in `recorderCreate(width:Int, height:Int, fps:Int, maxLengthSec:Int, capMs:Long)` → `Long` handle, once per recording; `recorderRecordedMs(handle, nowNs)` → `Long` every 250 ms; `recordNowNs()` → `Long`; `recorderStop(handle, reason:Int, nowNs)`; `recorderStopped` → `Int`; `recorderLimitMs` → `Long`; `recordWorthKeeping(ms)` → `Boolean`; `recorderDestroy`. To GL: `renderer.recordStart(handle, Surface, w, h, watermarkHandle:Long)` → `Boolean`, `recordStop()`.
- `RecordSettings`: `recordMaxLengths()` → `IntArray` once; `recordEstimateBytes(bps:Int, seconds:Int)` → `Long`; `recordRateLowered(bps, seconds)` → `Boolean`, per recomposition of the hint.
- `StageRun`: `stageRun(flags:Int)` → `Int`, on each lifecycle/PiP/second-screen change.
- `StageWarmNote`: `thermalTakeWarmNote(capFps:Int, fps:Int)` → `Int`, every 2 s until non-zero.
- `StylesPage`: `stylesLead(count:Int, onScreen:Int)` → `IntArray` (order), per family/scene change; `stylesRandom(familyOf:IntArray[nStyles], open:BooleanArray[nStyles], here:Int, familyOnly:Boolean, milkCount:Int, milkHere:Int)` → `Int`; `autoPick(count, here)` → `Int`; per tap.
- `TrackColorController`: `artPaletteExtract(IntArray ≤ 128×128 ARGB)` → `FloatArray[3]` (baseHue, span, confidence) or null, per button press.
- `VisualGestures`: `submitTouchPoints(FloatArray[10] xy in y-up NDC, n ≤ 5)` per pointer event; `stageStepIndex(open:BooleanArray, here:Int, step:Int)` → `Int` per step.
- `VisualSettingsController`: `touchZoom(zoom:Float, factor:Float)` → `Float`, `touchRotation(rotation:Float, degrees:Float)` → `Float`, per pinch event.
- `VisualsStage`: `stageStepTarget(page:Int, flags:Int, milkCount:Int, lookCount:Int)` → `Int` per recomposition of `VisualsHub` and per step; `stageStepMilk(Array<String> paths, current:String, step:Int)` → `Int` per step.
- `VisualsSheet`: `setUiBlurRegion(left, top, right, bottom, strength, cornerPx: Float×6)` through `queueEvent`.
- `PresetsPage`/`TemplatesSheet`: `shaderOverride(sceneId)` → `String?` on save.

### 6. Saved formats touched

- Preset JSON via `PresetStore.toJson/fromJson` (`PresetLibraryController.kt:145, 170`; `VisualsViewModel.kt:339`): fields written here are `name, sceneId, attack, decay, customShader, params, milkPreset` (`:98`). Preset files are keyed by name; a rename is save + delete (204–227).
- `.milk` side files in `filesDir/milk/` named by `PresetStore.milkFileName(name)` (`PresetLibraryController.kt:130–134`).
- Preset share links `geode://preset` (`PresetLink.encode/decode`, max `PresetLink.MAX_LINK_LENGTH`); template links `geode://template` (`TemplateFormat`).
- SAF mirror folder: JSON as `application/json`, milk as `text/plain`, under the same file names (`PresetLibraryController.kt:124–125`).
- `VideoTemplate` / `TemplateLook` (`TemplateController.kt:76–91`): `sceneId, attack, decay, params, customShader, milkPreset`, `TemplateId.random()`, `createdAtMs`.
- `VizStateStore` state fields written by `VisualSettingsController`: `sceneId`, `attack`, `decay`, `params`, `transitionId`, `transitionStyle`, `transitionDurationSec` (each followed by `persist()`).
- `RecordPrefs` (`quality`, `fps`, `maxLengthSec`) in the general prefs file (`RecordSettings.kt:50–67`); `StudioPrefsStore` last tab by `StudioSection.name` (`StudioRender.kt:118`) and `keepAsProject` tri-state (`RecordSettings.kt:147`); `ExportPrefsStore` per `ExportDestination` (`StudioRender.kt:282–286`); `loudnessTargetId` (741).
- `GeneralSettings`: `librarySort` stored as `LibrarySort.name` (`PreferencesSettings.kt:91`), `libraryStartTab`, `trackTap`, `showAbLoop`, `miniPlayerPrevious`, `stageArrows`, `stageRecord`, `screenRotation`, `barOpacity`, `miniPlayerFadeSeconds`.
- `WatermarkChoice` (`proAdds`, `placement`, `corner` ints).
- `AutoVisuals` prefs: intervals in seconds (clamped `5..300` in `data/AutoVisualsPrefsStore.kt:57`), random include flags; Auto reach as an `Int` level (`VisualsQuickSettings.kt:429–450`).
- `ProjectRender(uri, title, createdMs, durationMs)` written for recordings and kept renders (`RecordController.kt:243, 297`; `StudioViewModel.kt:120`).
- Saved-instance state by enum name (not on disk): `StudioSection.name`, `VisualsPage.name`, `SheetHeight.name`, `RenderScope.name`. `StudioSection.entries[it]` (`StudioRender.kt:203`) and `section.ordinal` (202) index tabs by ordinal in memory only.
- `VizPlaylistEntry(sceneId, presetName, label)` (`PresetsPage.kt:239`, `StylesPage.kt:233`).
- `customizeGroups` keys `"QUICK_KNOBS"` and `"QUICK_KNOBS/knob_editor"` built from `CustomizeCategory.name` (`VisualsHub.kt:466–469`).

### 7. Surprises

- The `Visual*`, `Studio*`, `Preset*` files in this slice hold far less engine logic than their names suggest. Step, random, lead order, stage-run, thermal note, record plan and pinch clamps are already native; Kotlin gathers arrays and acts on an index.
- No `Reaction*.kt`, `Touch*.kt` or `Transition*.kt` file exists in items 83–123 (they sort earlier or live elsewhere). Reactions, LFO, ADSR, randomize and reset are reached only as forwards in `VisualsViewModel.kt:143–201` to `session.modulation` (`ModulationController.kt`, earlier slice).
- A pinch persists on every pointer event: `nudgeTransform` → `setSceneParams` → `vizStateStore.persist()` (`VisualSettingsController.kt:97–100, 117`).
- `rememberTouchPulse` (`VisualGestures.kt:704–728`) is a Kotlin-timed animation that drives the native touch field: a formula (circle, radius 0.35, 36 × 33 ms) deciding picture input.
- Transition length `0.3..5 s` is a Kotlin literal in two files and not in the native schema.
- `builtInPresetSceneFamily` (`PresetActions.kt:355–360`) and `stageStyleOrder` (`VisualsStage.kt:504`) are style-id rules in Kotlin (`isCymatics`, `isFluid`, minus `MILKDROP`).
- `StylesPage.kt:167–168` and `VisualsStage.kt:520` both test `visualizerRenderer.milkdropAvailable && gate.allowsStyle(MILKDROP)` before asking native.
- `PaletteStore.applyGradient` (`data/`) writes palette params in Kotlin after the native extraction (`TrackColorController.kt:52–54`).
- `REACH_LABELS` (`VisualsQuickSettings.kt:737`) fixes Auto's reach to 3 levels by list position; native is the owner of the level meaning.
- Dead or test-only members: `sheetWidthDp` (`VisualsSheet.kt:527`) has no production caller, only `app/src/test/.../VisualsSheetWidthTest.kt`. `VisualsViewModel.applyPreset` (215) has no caller I could tell apart from `PlayerViewModel.applyPreset` by Grep (callers at `PresetsPage.kt:212`, `VisualGestures.kt:792`, `AppShell.kt:1208`, `StudioRender.kt:389` all use the `PlayerViewModel`); treat as probably dead, not confirmed. `VisualsSettingsPage`'s `onOpenPlaylist` is never passed (`VisualsHub.kt:448`), so `PlaylistRow`'s click branch (`VisualsQuickSettings.kt:523–528, 546–548`) is unreachable.
- User-visible English literals bypass string resources: `PresetActions.kt:333, 335, 392, 406, 409`; `PresetsPage.kt:559`; `TrackInfoEditor.kt:41–42`.
- `StudioRender.kt:98–101` and `StudioViewModel.kt:19–23` say Studio has no editor; `session.editor` is used only as the list of projects.
- `RecordController` reads `System.currentTimeMillis()` for file naming and project stamps, but all recording time is native (`recordNowNs`).
- Not verified: the Graphify tools were not used (Grep was enough for caller checks); `TransitionCatalog`, `Catalogue`, `VisualStyleCatalog`, `ExportRange`, `ExportAspect`, `RenderEta`, `RecordEncoder`, `PaletteStore`, `AutoVisualsController`, `ModulationController` were only opened where a line is cited.


---

## core/ (native C++ core), plus `.github/workflows/native-host-tests.yml` and `core/CMakeLists.txt`

Tree scanned at HEAD `5dbfc69` (the brief says `f13229d`; `git log -1` shows a later merge commit). `core/` holds 487 files, 98 263 lines (all file kinds, `wc -l`), 40 078 of them in `core/tests`.

**Read in full:** `core/api/geode_api.h` (1505), `core/viz/RendererFrame.cpp` (700), `core/viz/Renderer.hpp` (497), `core/signal/Resolver.{hpp,cpp}`, `core/signal/EngineSettings.hpp`, `core/viz/frame/{Cadence,PushVersions,Pass,Pacer,RecordTap,CastTee}.hpp`, `core/viz/FrameStep.hpp`, `core/output/Recorder.hpp`, `core/api/geode_{ingest,record}_handles.hpp`, `core/CMakeLists.txt`, the test stubs.
**Read in part (the lines cited):** `core/api/geode_viz_api.cpp` (45-244, 290-381, 494-537, 586-612, 704-803, 836-991 of 991), `core/viz/Renderer.cpp` (20-139, 345-574 of 814), `.github/workflows/native-host-tests.yml` (1-42, 90-100, 196-262, 300-470 of 470; the rest by grep), root `CMakeLists.txt` (by grep).
**Outlined only (declarations and leading comments by grep, not every line):** every other header in `viz/frame`, `output`, `audio`, `cast`, `sound`, and the top-level `viz` headers. Where a claim below rests on a header comment or the ABI comment rather than on the `.cpp`, it says so.
**Not read:** scene implementations (`viz/scenes`, `viz/fluid`, `viz/world`, `viz/pool`, `viz/liquid`, `viz/compute` `.cpp`), `sound/*.cpp`, `library/`, `scene/object/`, `util/`, the test sources, and `app/src/main/cpp/*_jni.cpp` (only grepped).

---

### 1. Directory map

| Directory | Files | Lines | Owns |
|---|---|---|---|
| `core/` (root) | 1 | 242 | `CMakeLists.txt`: one static lib `geode_core`, plus `geode_meshopt`, `geode_basisu` |
| `core/api` | 36 | 5 291 | The C ABI: `geode_api.h` (1505 lines, every exported function) and 32 `geode_*_api.cpp` wrappers; `geode_viz_api.cpp` (991) holds the `geode_viz` handle struct |
| `core/audio/dsp` | 23 | 1 970 | Playback DSP chain (gain, EQ, crossfeed, limiter, ReplayGain, ramps), export resampler, BS.1770 meter, PCM convert and timing |
| `core/audio/ingest` | 11 | 1 321 | `PcmRing` + `Cursor`, `LayerMix`, `SnapshotStitch`, `TrackFeed`, `AiffReader` |
| `core/audio/output` | 5 | 937 | `OboeOutput` (the only Oboe file) and `OutputFifo` |
| `core/cast` | 3 | 310 | `WebRtcLink` (libdatachannel), mbedtls config |
| `core/library` | 5 | 1 442 | Library search and index, TagLib tag I/O |
| `core/output` | 17 | 1 286 | Export and Record planning: `FrameClock`, `PreRoll`, `LoopPlan`, `EncodePlan`, `EncoderLadder`, `RecordPlan`, `Recorder`, `Chapters`, `LoudnessAdvice` |
| `core/scene/object` | 4 | 1 441 | glTF import and model textures (no caller, see §9) |
| `core/signal` | 5 | 447 | `Resolver` (the params chain) and `EngineSettings` / `SettingsLatch` |
| `core/sound` | 25 | 4 574 | `SoundEngine` (FFT, filter bank, Rhythm, Harmony, Form, Emotion, LUFS gate), `PlayerSignals`, `SoundSource`, `Silence`, `Tuning` |
| `core/sound/interp` | 4 | 374 | `PlayoutClock`, `SpeakerSync` |
| `core/tests` (+`fakegl`, `stub`) | 109 | 40 220 | 102 host test sources, fixtures (`*.txt`), a fake GLES3 header, android stubs |
| `core/util` | 5 | 635 | `Json`, `QrCode`, `Log.hpp` (6 lines) |
| `core/viz` | 143 | 21 681 | `Renderer`, params, modulation stages, Director, safety, catalogue, Customize, passes, GL plumbing, pure shell-port rules (StageRun, SurfaceRun, AutoTimer, StylePick…) |
| `core/viz/frame` | 29 | 4 919 | Frame-driver rules: Pacer, Thermal, Cadence, PushVersions, Quality, LiveFps, FixedRateClock, RecordTap, CastTee, export passes, LoopSeam, HLS, Link/WebRTC rules, `Pass` interface |
| `core/viz/scenes` | 26 | 4 227 | Scene classes: Shader, Milkdrop (projectM), Fluid, CurlFlow, Cymatics, Silk, Life, Myco, Acid; `StyleCatalog` |
| `core/viz/fluid` | 17 | 2 744 | Fluid sim, flow field, particles |
| `core/viz/world` | 6 | 2 190 | Morph World genome and driver |
| `core/viz/liquid` | 5 | 936 | Liquid mode pass and maths |
| `core/viz/compute` | 6 | 584 | Sim pass on compute or fragment ping-pong |
| `core/viz/pool` | 2 | 492 | Pool style maths |

---

### 2. The per-frame chain in native

#### 2.1 Ownership

- One `geode_viz` handle = one `geode::viz::Renderer` plus per-surface glue: `GyroLook`, `FrameCadence`, `PushVersions`, `ReactionResolve`, the ring `Cursor` and `LayerMix`, the `Recorder` share and `RecordTap`, the cast tee share, the still buffer, `TapAlong` (`core/api/geode_viz_api.cpp:45-158`).
- `Renderer` owns: one `sound::SoundEngine` (`core/viz/Renderer.hpp:330`), one `signal::Resolver` (`:301`), `SettingsLatch` (`:308`), `ThermalGovernor` (`:298`), `CameraRig` (`:326`), `TouchField` (`:289`), `SceneRegistry` and the built scenes (`:280-282`), all passes (`TrailPass`, `CompositePass`, `UiBlurPass`, `EdgeGlowPass`, `LiquidPass`, `DivePass`), `Overlays` (flow field), FBOs A/B/born, `PcmHandoff`, `StyleSchedule`.
- `Resolver` owns the modulation state: `LfoEngine`, `AdsrEngine`, `ReactionEngine`, `MomentWatch`, `Director`, `DirectorReach`, `NoteResponse`, `MotionField`, the faded params (`core/signal/Resolver.hpp:104-126`).
- Process-wide (not per renderer): thermal state `ThermalState::process()` (`core/viz/frame/Thermal.hpp:93`), director/reach/keep, note switch, hand mask, macro bank, layer (`core/api/geode_viz_api.cpp:708-720`, `:612`; `geode_macro_bank_set` at `core/api/geode_api.h:602`), shader overrides (`core/viz/ShaderOverrides.cpp:11`), player signals (`core/sound/PlayerSignals.hpp:42-45`), the memory-trim signal (`core/viz/SceneResidency.hpp:86`), the current cast tee (`core/viz/frame/CastTee.cpp:64`), auto timers / auto draw / stage step history.

#### 2.2 Call order on one live frame (C ABI down)

1. **Vsync → pace.** `geode_pacer_on_vsync(state, frame_time_ns, cap_fps)`: unpacks the caller-owned nine int64s, applies the process thermal rung to the cap, runs `Pacer::onVsync`, returns dt or 0 (skip) (`core/api/geode_viz_api.cpp:494-508`; `core/viz/frame/Pacer.hpp` `onVsync`). The caller owns the vsync source and the state array.
2. **`geode_viz_frame_begin`** (`core/api/geode_viz_api.cpp:740-762`): sets paced/cap fps on the governor (`:745`), applies Settings › Performance only when changed (`:746-750`), `FrameCadence::step` claims the process thermal slot and takes the Liquid label (`:751-752`), a clear-only first frame clears to black and returns (`:753-757`), then the error bit (`:759`) and `PushVersions::offer` for params/LFO/ADSR/tuning (`:760`).
3. **Caller pushes what the answer asked for:** `geode_viz_set_params` (`:228-240`, latched under `stateLock_`, `core/viz/Renderer.cpp:48-51`), `geode_viz_set_lfo` (`:410`), `geode_viz_set_adsr` (`:430`), `geode_viz_set_sound_tuning` (`:377`); on `GEODE_FRAME_THERMAL_DUE` the caller reads PowerManager and calls `geode_thermal_push` (`:517-519`).
4. **`geode_viz_set_scene`** (`:868-875`): records the scene in `ReactionResolve`, latches `requestedSceneId_` (`core/viz/Renderer.cpp:524-532`).
5. **`geode_viz_feed_ring`** (`:311-375`): binds/rebinds the cursor and layer mix, reads the playout limit (`:346-349`), up to 4 takes (`:352-365`), each `Renderer::writeAudio` (`core/viz/Renderer.cpp:91-108`: builds a `SoundEngine` on a new rate, `sound_->write`, then `PcmHandoff::write`), resets the engine on a restart (`:354-357`), tees the same block into the recorder (`:364`).
6. **`geode_viz_render(v, time_seconds, target_fbo)`** (`:909-921`): `syncReactions` re-resolves routes if scene/seed/hand changed (`:591-595`), asks the recorder whether this frame is kept (`:912-915`), `Renderer::render`, then feeds `TapAlong` from the sound frame (`:917-920`).
7. **`Renderer::render`** (`core/viz/RendererFrame.cpp:402-461`):
   1. `beginFrame` (`:38-107`): dt from `frameStepSeconds` (fixed 1/fps in export, measured and clamped live; `core/viz/FrameStep.hpp`), `thermal_.onFrame(dt)` (`:43`), latch `EngineSettings` (`:48`), player boundary/fade/source for live only (`:52-67`), **`sound_->step(dt, soundFrame_)`** (`:69`), music rate and the picture clock (`:76-77`), `applyStyleSchedule` (`:78`; `core/viz/Renderer.cpp:542-569`), layer sync and PCM hand-off take under `stateLock_` (`:81-90`), MilkDrop and underlay requests (`:94-95`), warm the next random transition (`:96-105`).
   2. Render scale on a tier or user-scale change (`:405-407`), memory-trim poll (`:411`).
   3. `resolveActiveScene` (`:109-146`): staged build via `readyScene`, starts a transition, marks the switch.
   4. Settling note to the governor (`:415-416`); clear-and-return when no scene or no targets (`:418-424`).
   5. **`resolveParams`** (`:148-212`): latches requested params, morph, LFO/ADSR/route configs into the resolver (`:152-170`), then **`resolver_.step`** (`:194-195`), publishes knob mods (`:198`), applies device masks (Liquid mode by tier `:204-206`, post-FX `:207`), integrates post rotation and colour cycle (`:209-210`).
   6. `resolveLayerScene` (`:214-233`), still-frame test (`:430-437`).
   7. `stepOverlays` (touch field, Liquid, smear; `:352-357`) and `stepCamera` (`:384-400`).
   8. `drawSecondaryTargets` (layer and outgoing scene into FBO B; `:256-283`).
   9. `drawSceneTarget` (trail pass or clear, `scene.setParams`, PCM, `scene.update(sound, dt)`, `scene.draw`; `:291-306`).
   10. `drawTrails` or `drawLiquid` (`:455`, `:600-626`, `:660-698`), `drawDive` (`:456`, `:628-658`).
   11. `composite` to `targetFbo` (`:308-350`), `drawEdgeGlow` (`:473-491`), **composite hook** (`:459`) = record tap, cast tee publish, still read-back (`core/api/geode_viz_api.cpp:55-59`, `:65-112`), then `drawUiBlur` (`:463-471`).
8. **Resolver chain inside step 7.5** (`core/signal/Resolver.cpp:53-154`), in this order: param fade/morph (`:62-71`) → director step (`:75-87`) → `DirectorReach::step` (`:88-103`) → `NoteResponse::step` (`:107-117`) → `ReactionEngine::tick` + `MomentWatch` (`:120-121`) → ADSR tick, LFO tick (`:122-124`) → knob mods (`:127`) → `directorReach.applyShuffle` → `director.apply` → `director.applyAxes` (`:136`) → `directorReach.applyModulation` (`:137`) → `macrobank::apply` (`:138`) → `lfo_.apply` (`:139`) → `AdsrEngine::apply` (`:140`) → `reactions_.apply` (`:143`) → `motionField_.step/apply` (`:148-149`) → `safety::apply` (`:150`).
9. **Caller swaps buffers.** Core never swaps the stage surface; it only swaps the encoder surface inside `RecordTap::present` (EGL calls only in `core/viz/frame/RecordTap.cpp`, see §5).

#### 2.3 The implicit protocol the caller must keep

Per surface, once: `geode_viz_create(assets, cache_dir)` (any thread) → with the GL context current: `geode_viz_surface_created` (`core/api/geode_viz_api.cpp:857-862`; reprobes GL, releases every old name, rebuilds passes, resets resolver and camera, `core/viz/Renderer.cpp:348-441`) → `geode_viz_surface_changed(w, h)`.

Per live frame, on the GL thread, in this order: `geode_pacer_on_vsync` (skip on 0) → `geode_viz_frame_begin` (if `CLEAR_ONLY`: swap and return) → push the inputs named by `GEODE_FRAME_PUSH_*`, and `geode_thermal_push` on `THERMAL_DUE`, read `geode_viz_last_error` on `ERROR_CHANGED` → `geode_viz_set_scene` → `geode_viz_feed_ring` → `geode_viz_render(time, fbo)` → swap. The order is stated in the ABI comments (`core/api/geode_api.h:148-161`, `:1252-1256`).

Poll after frames: `geode_viz_take_director_style_moments`, `geode_viz_take_milk_preset_loaded`, `geode_macro_bank_live_mods`, `geode_thermal_take_warm_note`, `geode_viz_still_size/_read`.

Export, on its own GL thread: `geode_viz_prepare_offscreen` (`core/api/geode_viz_api.cpp:969-978`) → `geode_viz_set_frame_rate(fps)` → `geode_viz_pin_engine_settings` → `geode_viz_set_style_schedule` → pre-roll (`geode_export_pre_roll`, `geode_viz_hear_track`, `geode_viz_end_pre_roll`) → per frame: `geode_track_feed_target_frame` + `geode_viz_hear_track` until 1, `geode_viz_render`, optional `geode_export_seam_capture` or `geode_export_seam_blend`, `geode_export_watermark_draw`, caller stamps PTS and swaps. Export never calls `frame_begin` (`core/api/geode_api.h:161`).

Pause/teardown: `geode_viz_trim_spares` with the context current (`core/api/geode_viz_api.cpp:984-989`), `geode_viz_release_scenes`, `geode_viz_record_stop`, `geode_viz_destroy`.

---

### 3. One engine or several?

**Verdict from the code: one renderer class, one sound engine class, one params chain.** `Renderer` is the only frame driver in `core/` (`core/viz/Renderer.hpp:58`); the only production `SoundEngine` construction is `core/viz/Renderer.cpp:97`; the only `Resolver` is `Renderer::resolver_`. Live, wallpaper, dream, cast fallback and export all run the same `Renderer::render`, switched by flags (`offscreen_`, `fixedFps_`, `blockingBuilds_`, the settings pin). What is multiple is the **number of instances** (one `geode_viz` per surface, each with its own sound engine and its own cursor on a shared ring) and the **frame protocol**, which lives in the caller: core has no loop, so each caller re-implements steps 1-9 of §2.3.

Overlaps found inside core:

| # | Overlap | Paths | Live / dead |
|---|---|---|---|
| 1 | **Two renderers under one frame graph: the native scenes and projectM.** `MilkdropScene` wraps a projectM instance rendering into a scene FBO, with its own preset engine and its own analysis of the raw PCM it is handed. | `core/viz/scenes/MilkdropScene.hpp:22-24`; PCM path `core/viz/PcmHandoff.hpp:9-16`; `libprojectM` linked at `core/CMakeLists.txt:168` | Both live. projectM is a scene family (`SceneFamily::Milkdrop`, `core/viz/Scene.hpp`), not a parallel driver. |
| 2 | **Two sound analysis paths.** `SoundEngine` produces the `GeodeSoundFrame`; projectM analyses the same samples again from `PcmHandoff` for MilkDrop presets. | `core/sound/SoundEngine.hpp:56`, `core/viz/RendererFrame.cpp:245-248` | Both live, same samples (#360 comment). |
| 3 | **Modulation outside the resolver.** The resolver is documented as the only chain (`core/signal/Resolver.hpp:21-32`), but the renderer also moves the picture from the sound frame after it: band trims (`RendererFrame.cpp:195`), the picture clock scaled by `motionRate` (`:76-77`), Liquid swirl by mids (`:548`), born clock on phrase/section (`:535-536`), shimmer/light (`:693-694`), dive follow (`:630`), camera step with the sound frame (`:398-399`), edge-glow envelope (`:487`), trail motion (`:609`), and every scene's own `update(sound, dt)` (`:304`). | as cited | All live. None is user-routable through the reaction matrix. |
| 4 | **Two thermal layers.** Process-wide platform tier `ThermalState` plus per-renderer measured fallback `PerformanceMonitor` inside `ThermalGovernor`. | `core/viz/frame/Thermal.hpp:67`, `core/viz/ThermalGovernor.hpp:12,48` | Both live; the governor reads the state (header comment `Thermal.hpp`). |
| 5 | **Several frame clocks.** vsync pacer (`core/viz/frame/Pacer.hpp`), renderer dt (`core/viz/FrameStep.hpp`), cast's `FixedRateClock` (`core/viz/frame/FixedRateClock.hpp:22`), export's `output::FrameClock` (`core/output/FrameClock.hpp:14`), record's `Recorder` slot clock (`core/output/Recorder.hpp`), the playout clock (`core/sound/interp/PlayoutClock.hpp:59`). The dt clamp is shared (`FrameStep.hpp` uses `frame::clampDt`). | as cited | All live, one per driver kind. `FixedRateClock` pts(i) and `FrameClock` pts(n) are the same formula in two classes (header comments). |
| 6 | **Monotonic "now" read in four places.** `steady_clock` in `geode_viz_api.cpp:73`, `:191-193`, `:344`, `geode_record_api.cpp:22-24`, `geode_ingest_api.cpp:31`, `Renderer.cpp:136-138`, `clock_gettime` in `OboeOutput.cpp:28`; while `time_seconds`, `frame_time_ns`, `now_ms`, `now_ns` of the pure rules come from the caller. | as cited | All live. Two time sources per frame (caller's and native's). |
| 7 | **Two loudness meters.** `sound::LufsMeter` (engine, per hop) and `audio::LoudnessAnalyser` (export, whole programme). | `core/sound/LufsMeter.hpp:19`, `core/audio/dsp/Loudness.hpp:32` | Both live; the K-weighting constants are shared by comment only (`LufsMeter.hpp:10`). |
| 8 | **Two resamplers.** `sound::HalfBand` decimator inside the engine and `audio::PcmResampler` for export/record. | `core/sound/Decimate.hpp`, `core/audio/dsp/PcmResampler.hpp:28` | Both live, different jobs. |
| 9 | **Two silence rules.** The engine's LUFS gate (`LufsMeter.hpp` gate constants) and `sound/Silence.hpp` (wallpaper/dream rest, capture quiet) with its own -42 dBFS / 6 s thresholds (`core/api/geode_api.h:746-757`). | as cited | Both live. |
| 10 | **Two Customize maps / randomizers.** `randomizer::Tab` (old tabs: `geode_params_randomize`, `geode_params_reset_tab`) and `customize::Category` (`geode_customize_randomize/_reset/_borrow`), plus `RandomizeAll`, `WaveRandomizer`, the director's shuffle, and Liquid's born re-roll calling `randomizer::randomize` directly (`RendererFrame.cpp:562-563`). | `core/viz/ParamRandomizer.hpp:57`, `core/viz/CustomizeMap.hpp:118,122` | Tabs still live (`geode_params_api.cpp:48`, JNI calls `geode_params_randomize`); `geode_params_reset_tab` has no JNI caller (grep of `app/src/main/cpp`). |
| 11 | **Several params representations on the wire.** `SceneParams` float array (`geode_viz_set_params`), LFO 11-float and ADSR 12-float arrays, routes 8-float arrays, text tables (catalogue, params schema, config schema, customize, mod targets, macro bank, palette stack). Requested params vs displayed (faded) vs final vs `lastFinalParams_`. | `core/api/geode_api.h:18-26`, `:413-442`, `:517-523`; `core/signal/Resolver.hpp:117` | All live. One struct natively (`SceneParams`, `core/viz/Params.hpp:84`). |
| 12 | **`core/signal` vs `core/viz`.** `signal/` holds only the resolver and settings; every stage it runs still lives in `core/viz` (`Resolver.hpp:45-47`). | | Not a duplicate: a partial move. |
| 13 | **`core/scene` vs `core/viz/scenes`.** `core/scene/object` is glTF import, unrelated to scenes drawn. | `core/CMakeLists.txt:183,233` | `scene/object` has no caller outside its own files and tests (§9). |
| 14 | **Two tees at the same hook.** `RecordTap` (EGL surface on the encoder window, stage context) and `CastTee` (AHardwareBuffer mailbox, two contexts). | `core/viz/frame/RecordTap.hpp`, `core/viz/frame/CastTee.hpp` (the header explains why they differ) | Both live. |
| 15 | **Two trail implementations.** `TrailPass` (in-target, particle styles) and trails through `LiquidPass` for shader styles. | `RendererFrame.cpp:296-301`, `:444-455`, `:600-626` | Both live. |
| 16 | **Three style tables.** `Catalogue`, `scenes/StyleCatalog` ("Port of VisualStyleCatalog.kt"), `SceneRegistry`. | `core/viz/Catalogue.hpp:7`, `core/viz/scenes/StyleCatalog.hpp:7`, `core/viz/SceneRegistry.hpp:14` | All live. |

Dead or caller-less in core: `core/viz/ConfigMath.hpp` (only `core/tests/config_schema_test.cpp:19` includes it; its own comment says Kotlin still runs its copies, `ConfigMath.hpp:10-11`); `Renderer::pendingShaders_` (comment at `Renderer.hpp:381-383`: nothing queues into it); `core/scene/object/*` (§9).

---

### 4. Handles and lifetimes

Thread expectations are from ABI comments and member comments; **there is no thread assert anywhere in `core/` or the JNI** (grep for `this_thread`, `get_id`, thread asserts: one hit, a sleep in `OboeOutput.cpp:254`).

| Handle | Create / destroy | Owner today | Thread contract (source) |
|---|---|---|---|
| `geode_viz` | `geode_viz_create` / `_destroy` (`geode_viz_api.cpp:210-217`) | one per surface | Setters any thread, latched (`geode_api.h:16-17`); `surface_*`, `set_scene`, `frame_begin`, `feed_ring`, `write_audio`, `render`, gyro, still read, `trim_spares`, `record_start` on the GL thread with the context current (`geode_api.h:220`, `:148`, `:237`). Locks: `stateLock_`, `soundLock_` (`Renderer.hpp:329,349`), `configLock`, `reactLock`, `tapLock` (`geode_viz_api.cpp:116,127,151`); many atomics. |
| `geode_pcm_ring` | `geode_pcm_ring_create` / `_destroy` | shell; shared with every cursor (`geode_ingest_handles.hpp`) | One writer thread; any readers; `set_layer`, `begin_epoch`, `set_playout` any thread (`geode_api.h:1186-1230`). Lock-free ring; `layerLock` mutex for the layer pointer. |
| `geode_pcm_cursor` | `geode_pcm_cursor_create` / `_destroy` | shell (Cast audio) and each `geode_viz` internally | One thread per cursor (`geode_api.h:1235-1236`). |
| `geode_playout` | `geode_playout_create` / `_destroy` | one per process | Playback thread writes, any GL thread reads (`geode_api.h:1149-1154`). |
| `geode_dsp` | `geode_dsp_create` / `_destroy` | playback | Setters any thread; configure/flush/process on the playback thread; process never allocates, blocks or logs (`geode_api.h:296-300`). |
| `geode_audio_out` | `geode_audio_out_open` / `_release` | Media3 sink | One control thread; Oboe's callback only reads the FIFO (`geode_api.h:1300-1303`). Release closes on a detached thread (`OboeOutput.cpp:272`). |
| `geode_recorder` | `geode_recorder_create` / `_destroy`, shared_ptr inside (`geode_record_handles.hpp`) | shell encoder thread + stage renderer | `frame`, `hear` GL thread; `take_audio`, `video_encoded`, `wrote` encoder thread; `stop` and readers any thread (`Recorder.hpp` "Threads"). One mutex + atomics. |
| `geode_export_pass` | `geode_export_watermark_create`, `geode_export_seam_create` / `geode_export_pass_destroy` | exporter, or Record for the watermark | Needs the export GL context current, destroy included (`geode_api.h:686-691`). |
| `geode_cast_tee` | `geode_cast_tee_open` / `_close` | cast thread | Cast thread with its own EGL context current; stage publishes on its GL thread; one mutex mailbox (`CastTee.hpp` "Threads"). |
| `geode_track_feed` | `geode_track_feed_create` / `_destroy` | exporter | One thread per handle (`geode_api.h:1272-1275`). |
| `geode_aiff` | `geode_aiff_open_fd` / `_close` | exporter | One thread at a time; takes and closes the fd (`geode_api.h:1258-1262`). |
| `geode_resampler` | `_create` / `_destroy` | exporter | Any one thread at a time (`geode_api.h:641-645`). |
| `geode_loudness` | `_create` / `_destroy` | exporter | Any one thread at a time (`geode_api.h:656-657`). |
| `geode_tags` | `geode_tags_read(fd)` / `_destroy` | library | Owns and closes the fd (`geode_api.h:360-361`). |
| `geode_library_index` | `_create` / `_destroy` | library | Built once, read from any thread (`geode_api.h:399-401`). |
| `geode_shake` | `_create` / `_destroy` | sensor listener | One thread per handle (`geode_api.h:278-280`). |
| WebRTC | `geode_webrtc_open` → `int64_t` / `geode_webrtc_close` | cast | Any thread; `answer` blocks up to 2 s (`geode_api.h:1464-1468`). |
| Caller-owned state arrays (no handle) | pacer 9 × int64, frame clock 3, cast audio 3 + 3, sound source 8, link state 2, HLS decode times 8 | shell | Pure functions; the shell holds the state (`geode_api.h:955-958`, `:794-797`, `:1366-1374`, `:776`, `:1409`, `:1352`). |

---

### 5. Threads and platform in native today

- **Threads core starts (3, none of them a render or frame thread):** the program-binary cache's writer (`core/viz/ProgramBinaryCache.hpp:62-66`), a detached close thread per released Oboe stream (`core/audio/output/OboeOutput.cpp:272`), and whatever libdatachannel starts behind `WebRtcLink` (a `condition_variable` waits for ICE gathering, `core/cast/WebRtcLink.cpp:117`). Oboe's audio callback thread calls `onAudioReady` (`OboeOutput.hpp:22-26`). No `pthread` use.
- **Hand-over points:** params/scene/transition/underlay/schedule under `Renderer::stateLock_` latched in `beginFrame`/`resolveParams` (`RendererFrame.cpp:81-90`, `:152-170`); sound under `soundLock_` (`:46`); atomics for flags (`Renderer.hpp:273-366`); the lock-free `PcmRing`; `Recorder::audioLock_`; `CastTee::lock_` + EGL fences; `ThermalState` mutex + atomics; `DspProcessor` setters via atomics.
- **`__ANDROID__` blocks: none** (grep). The Android dependency is by include and link instead: `<android/asset_manager.h>` in `geode_api.h:4` and `Renderer.hpp:3`, `__android_log_print` through `core/util/Log.hpp:4-6`, `<EGL/egl.h>` in `geode_viz_api.cpp:13`, `RecordTap.hpp`, `CastTee.hpp`, `AHardwareBuffer` in `CastTee`, Oboe in `OboeOutput`. Link line: `projectM tag GLESv3 EGL log android nativewindow oboe` (`core/CMakeLists.txt:168-170`).
- **EGL/GL context:** core never creates a display, context or the stage's surface. Every `Renderer` GL call assumes the caller made a context current. EGL calls exist in exactly three files: `geode_viz_api.cpp:987` (`eglGetCurrentContext` guard), `RecordTap.cpp` (`eglChooseConfig`, `eglCreateWindowSurface` for the encoder window, `eglMakeCurrent`, `eglPresentationTimeANDROID`, `eglSwapBuffers` on that surface only), `CastTee.cpp` (`eglGetCurrentContext/Display`, `eglGetProcAddress` for images and fences). `eglCreateContext`, `eglInitialize`, `eglCreatePbufferSurface`: absent from `core/` and from `app/src/main/cpp` (grep).
- **Logging:** `GEODE_LOGI/W/E` → `__android_log_print` (`core/util/Log.hpp`); `OboeOutput.cpp` calls it directly.
- **What the host tests stub:** `core/tests/stub/android/asset_manager.h` (3 lines: a forward declaration of `AAssetManager`, for tests that only include `geode_api.h`); `core/tests/fakegl/GLES3/gl3.h` (91 lines: a declarations-only fake GL the test defines) with `core/tests/fakegl/android/{asset_manager,log}.h`. Only three tests use fakegl (`edge_glow_pass_test`, `transition_programs_test`, `style_reads_test`; workflow lines 170, 176, 311). **`Renderer`, `RendererFrame`, `CompositePass`, the scenes, `RecordTap`, `CastTee.cpp`, `OboeOutput` and `WebRtcLink` are never compiled on the host**: no g++ line names them (grep of the workflow). The frame order in §2.2 step 7 is therefore not host-tested; only the resolver's order is (`resolver_test`).

---

### 6. What already exists natively per driver

All rows are pure or handle-based rules the caller steps; none owns a thread.

| Driver piece | File : class | Public surface | Host test |
|---|---|---|---|
| Pacer / vsync maths | `core/viz/frame/Pacer.hpp` : `PacerState`, `Pacer` | `onVsync(state, frameTimeNs, cap) → dt or 0`, `resume`, `divisorFor`, 9-slot wire | `pacer_test` |
| Live fps cap, Follow device | `core/viz/frame/LiveFps.hpp:70,87,108,134` | `liveFpsChoices`, `liveFpsCap`, `thermalStepCapFps`, `followCapFps` | `live_fps_test` |
| Live frame dt | `core/viz/FrameStep.hpp` : `frameStepSeconds` | fixed 1/fps or measured+clamped | covered by `sound_rate_test` / `pacer_test` (dt clamp); no test of its own |
| Fixed-rate clock (Cast) | `core/viz/frame/FixedRateClock.hpp:22` : `FixedRateClock`, `CastFormat` | start, wait, take → PTS, audio cut; 3-slot wire | `cast_clock_test` |
| Export frame clock | `core/output/FrameClock.hpp:14` : `FrameClock` | total frames, pts(n), time(n), samplesThrough | `output_test`, `sound_rate_test` |
| Export pre-roll | `core/output/PreRoll.hpp:24` : `PreRoll` | `preRollFor`, `endPreRoll(engine, plan)` | `export_preroll_test` |
| Per-frame cadence | `core/viz/frame/Cadence.hpp` : `FrameCadence` | `surfaceCreated`, `offerQuality`, `step`, `offerError` | `renderer_cadence_test` |
| Look / push versions | `core/viz/frame/PushVersions.hpp` : `PushVersions` | `surfaceCreated`, `offer(versions) → bits` | `push_versions_test` |
| Surface run rule (wallpaper, dream) | `core/viz/SurfaceRun.hpp` : `surface::RunMode`, `Reason` | `runMode`, `listening:97`, `settleMs:115`, fps choices | `surface_run_test`, `surface_motion_test` |
| Stage run rule | `core/viz/StageRun.hpp:45,70,100` : `stage::run`, `host`, `pickDisplay` | flags → draws / cast leads auto; host rect; display pick | `cast_same_visual_test` |
| Thermal | `core/viz/frame/Thermal.hpp:67` : `ThermalState`; `core/viz/ThermalGovernor.hpp:12,48` | `push`, `headroomDue`, `tier`, `capFor`, warm note; per-renderer `onFrame`, `noteSettling` | `thermal_test` |
| Quality tiers | `core/viz/frame/Quality.hpp:15,41` : `QualityTier`, `modeForDevice` | mode → scale, cap, march, post FX, adaptive | `quality_test` |
| Recorder (clock + audio hold) | `core/output/Recorder.hpp` : `record::Recorder` | `frame`, `hear`, `takeAudio`, `stop`, `videoEncoded`, `wrote`; `frameSize`, `videoBps`, `encoderAttempt` | `recorder_test` |
| Record limits | `core/output/RecordPlan.hpp` | max lengths, estimate bytes | `record_plan_test` |
| Record picture tee | `core/viz/frame/RecordTap.hpp` : `RecordTap` | `start(window, w, h)`, `begin`, `present(pts)`, `stop` | no test (EGL) |
| Encoder ladder | `core/output/EncoderLadder.hpp:42` : `EncoderRung`, `EncoderAttempt` | attempts from fps, bit rate, codec ceilings | `encoder_ladder_test` |
| Encode plan | `core/output/EncodePlan.hpp` : `encode::` | capped bps, audio bytes, frame size, clamp fps | `output_test` |
| Export passes | `core/viz/frame/ExportPasses.hpp:33,69` : `WatermarkPass`, `SeamPass`; maths in `ExportPassMath.hpp` | create, draw, capture, blend | `export_passes_test` (maths); the GL passes: no test |
| Loop plan | `core/output/LoopPlan.hpp:11,94` : `LoopSpec`, `LoopExtendPlan` | spec, budget, extend plan, drift stops | `output_test` |
| Loop seam driver | `core/viz/frame/LoopSeam.hpp:38,70` : `loopseam::step`, `budgetedCrossfadeMs` | per-frame stash/blend decision, PTS, progress | `loop_seam_test` |
| HLS segments | `core/viz/frame/HlsSegments.hpp` : `hls::` | fragment seconds, playlist text, fragment rewrite | `hls_segments_test` |
| Cast tee | `core/viz/frame/CastTee.hpp` : `CastTee`; rules `CastTeeRule.hpp` : `tee::Mailbox`, `Placement` | `publish`, `draw`, `current/setCurrent`; `awaitStage` | `cast_tee_test` (rules only) |
| Cast audio pump | `core/viz/frame/CastAudio.hpp:25,94` : `CastAudio`, `CastAudioFeed` | start, step → counts; feed add/take | `cast_audio_test` |
| Link / WebRTC rules | `core/viz/frame/LinkShare.hpp`, `WebRtcRule.hpp`; `core/cast/WebRtcLink.hpp:28` | routes, heads, state step, SDP rules; the peer link | `link_share_test`, `webrtc_rule_test`, `qr_code_test`; `WebRtcLink`: no test |
| Track feed | `core/audio/ingest/TrackFeed.hpp:21` : `TrackFeed` | push decoded / AIFF, target ms or frame, deliver | `track_feed_test`, `export_preroll_test` |
| AIFF reader | `core/audio/ingest/AiffReader.hpp:52` : `AiffReader` | open fd, read s16, progress | `aiff_header_test` (the header), `track_feed_test`, `export_preroll_test` |
| Resampler | `core/audio/dsp/PcmResampler.hpp:28` : `PcmResampler` | streaming s16 process | `resampler_test` |
| Loudness | `core/audio/dsp/Loudness.hpp:32` : `LoudnessAnalyser`; advice `core/output/LoudnessAdvice.hpp` | feed, finish → report; window, advise | `loudness_test`, `output_test` |
| PCM convert / timing | `core/audio/dsp/PcmConvert.hpp`, `PcmTiming.hpp` | downmix, float→s16, AAC rate, trim | `export_pcm_test` |
| PCM ring + cursor | `core/audio/ingest/PcmRing.hpp:41,137` | write, epoch, cursor take(limit) | `pcm_ring_test`, `speaker_sync_test` |
| Mic layer, snapshots | `LayerMix.hpp:27`, `SnapshotStitch.hpp:31` | mixed block, alone; stitched writes | `layer_mix_test`, `snapshot_stitch_test` |
| Playout clock | `core/sound/interp/PlayoutClock.hpp:59`, `SpeakerSync.hpp` | buffer handled, position, speaker frame, listen limit | `playout_test`, `speaker_sync_test` |
| Audio out | `core/audio/output/OboeOutput.hpp:34`, `OutputFifo.hpp:54` | open, write, position, events | `output_fifo_test`; `OboeOutput`: no test |
| Playback DSP | `core/audio/dsp/DspProcessor.hpp:43`, `GainStage.hpp:35` | configure, flush, process, ramps | `dsp_handoff_test`, `bit_perfect_test`, `gain_ramp_test` |
| Sound-source choice | `core/sound/SoundSource.hpp:56` : `source::step` | state + facts → use/start/stop capture bits | `sound_source_test` |
| Silence / rest | `core/sound/Silence.hpp` | heard-at, resting, capture quiet | `surface_run_test` / `sound_source_test` (not verified which one covers it) |
| Player signals | `core/sound/PlayerSignals.hpp` | track boundary, fade gain, player source | `sound_test` family (not verified) |
| Auto timer | `core/viz/AutoTimer.hpp:96,103` | `restart`, `due`, hold seconds | `auto_timer_test` |
| Auto draw / style pick | `core/viz/AutoDraw.hpp:57`, `StylePick.hpp:52,63` | `pick`, `lead` | `style_pick_test`, `auto_timer_test` |
| Stage step | `core/viz/StageStep.hpp` | target, index, milk step | `stage_step_test` |
| Style schedule (export) | `core/viz/StyleSchedule.hpp:43` | `set`, `advance(dt, rate, beatCount)` | `style_schedule_test` |
| Customize map | `core/viz/CustomizeMap.hpp:118,122` | text, shown, randomize, reset, borrow | `customize_test`, `style_reads_test` |
| Randomize | `ParamRandomizer.hpp:57`, `RandomizeAll.hpp`, `WaveRandomizer.hpp:38` | seeded rolls | `params_test`, `randomize_all_test`, `lfo_random_test` |
| Look / params | `core/viz/Params.hpp:84` : `SceneParams`; `Config.hpp` | wire, schema text, lerp | `params_schema_test`, `config_schema_test`, `presets_test` |
| Modulation | `Lfo.hpp:82`, `Adsr.hpp:34`, `Reactions.hpp:183`, `MacroBank.hpp:230`, `Director.hpp:57`, `DirectorReach.hpp:53`, `NoteResponse.hpp:51`, `MotionField.hpp:36`, `VisualSafety.hpp:22` | tick/apply each | `viz_math_test`, `resolver_test`, `director_test`, `director_reach_test`, `note_response_test`, `macro_bank_test`, `reaction_resolve_test`, `mod_targets_test`, `hidden_hits_test` |
| Sound engine | `core/sound/SoundEngine.hpp:56` | `write`, `step(dt, frame)`, `reset`, `trackBoundary`, tuning | `sound_test`, `sound_rate_test`, `sound_golden_test`, `musical_test`, `meter_test`, `absolute_level_test` |
| Scene residency, staged builds | `SceneResidency.hpp:60`, `ScenePrep.hpp:67`, `ProgramStager.hpp:30` | budget, trim, staged attempts | `scene_residency_test`; `ScenePrep`/`ProgramStager`: no test found by name |
| Gyro, shake, tap along | `GyroLook.hpp:22`, `Shake.hpp`, `TapAlong.hpp:18` | sample → look; shake detect; tap match | `gyro_look_test`, `easter_eggs_test` |
| Wallpaper window, edge glow | `frame/WallpaperWindow.hpp`, `EdgeGlowMath.hpp`, `EdgeGlowPass.hpp:21` | window rect, fps; pass | `wallpaper_window_test`, `edge_glow_test`, `edge_glow_pass_test` |

---

### 7. Host tests

- **Workflow:** `.github/workflows/native-host-tests.yml`, one job `host-tests` on `ubuntu-latest`, 20 min timeout, checkout with `submodules: recursive` (lines 27-35). Triggers: `pull_request`, `workflow_dispatch`, `workflow_call` (lines 8-11).
- **No CMake and no test runner are involved.** Each test is a hand-written pair of shell lines inside a `run: |` block: one `g++` line listing the test source and exactly the core `.cpp` files it links, then `./name_test` (pattern at lines 39-41). `core/CMakeLists.txt` builds only the Android static lib `geode_core` and has no test target (lines 1-243).
- **To wire a new test:** (1) add `core/tests/<name>_test.cpp` with its own `main`; (2) add two lines to `native-host-tests.yml`, either inside the big "Native host tests" step (line 37 on) or as a new named step like those at lines 303-470: `g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/<name>_test.cpp <each core .cpp it needs> -o <name>_test` and `./<name>_test [fixture args]`; (3) separately add any new production `.cpp` to `core/CMakeLists.txt` `add_library(geode_core …)` (lines 1-159) so the APK builds it. Missing a source in the g++ line is a link error; missing it in CMake only fails the APK build.
- **Compiler and flags:** the runner's `g++`, `-std=c++20 -O1 -Wall -Wextra -Werror -Icore`, plus `-Icore/tests/stub` (most), or `-Icore/tests/fakegl` without the stub dir (3 tests), `-pthread` on 3 (`cache_sentinels_test` line 91, `shader_overrides_test` line 219, `recorder_test` line 399) and the glTF test; `sound_bench` uses `-O2` (line 468). **No sanitizers** (`-fsanitize` absent), no clang, no ctest. The Android build uses `-std=c++20 -fvisibility=hidden -Wall -Wextra`, `-O2 -flto` in Release, no `-Werror` (root `CMakeLists.txt:12-22`).
- **Executables (96 `g++` lines, 94 `-o` names plus a 3-test loop):** viz_math, params_schema, colour, palette, touch_transform, touch_smear, fluid_math, liquid, pool, feedback_motion, resolver, director_reach, scene_residency, thermal, cache_sentinels, sound, absolute_level, musical, meter, director, note_response, pcm_handoff, pcm_ring, track_feed, layer_mix, snapshot_stitch, output_fifo, layer_state, sound_source, gyro_look, style_schedule, hidden_hits, cast_clock, export_passes, edge_glow, wallpaper_window, edge_glow_pass, transition_library, transition_programs, quality, surface_run, underlay, surface_motion, auto_timer, stage_step, style_pick, milk_shader_note, milk_shown, transition_choice, pacer, live_fps, renderer_cadence, shader_overrides, library_search, playout, speaker_sync, encoder_ladder, gltf_import, push_versions, loop_seam, cast_audio, world_genome, hls_segments, cast_tee, cast_same_visual, link_share, webrtc_rule, qr_code, config_schema, reaction_resolve, catalogue, style_reads, visual_gate (built, not run; line 316-317), presets, mod_targets, reaction_text, lfo_random, macro_bank, resampler, loudness, dsp_handoff, bit_perfect, gain_ramp, output / record_plan / export_pcm (loop, lines 389-392), recorder, params, customize, randomize_all, easter_eggs, art_palette, sound_rate, export_preroll, sound_golden, sound_bench (`--quick`).
- Fixtures the tests compare against and the JVM tests also read: `core/tests/{catalogue,customize,mod_targets,palettes,params_schema,macro_editor,sound_golden,style_param_gaps,visual_gate_baseline}.txt`.
- `core/tests` is also referenced by `android.yml`, `arch-map.yml`, `render-thumbs.yml`, `tools/no-bpm-bias-gate.sh`, `tools/store-stats.sh` (grep; not read).

---

### 8. Gaps for a native render thread (absent, verified by grep over `core/` and `app/src/main/cpp/`)

- **EGL bring-up:** no `eglInitialize`, `eglCreateContext`, `eglCreatePbufferSurface`; the only `eglCreateWindowSurface` is the encoder surface in `RecordTap.cpp`. No stage window surface, no config choice for it, no context loss handling beyond `surface_created`.
- **Window management:** no `ANativeWindow_*` in `core/` (only `ANativeWindow_fromSurface` in `app/src/main/cpp/geode_viz_jni.cpp`); no `ASurfaceControl`, no `ANativeWindow_setFrameRate`.
- **A thread and a loop:** no render thread, no `ALooper`, no message queue, no command channel. Every setter is a latch read by whichever thread calls `render`.
- **Vsync source:** no `AChoreographer`. The pacer needs `frame_time_ns` from outside.
- **Sensors:** no `ASensor*`. Gyro and shake take samples from the caller.
- **Thermal listener:** no `AThermal*`. `ThermalState` is pushed by the caller.
- **Performance hints:** no `APerformanceHint`.
- **Media:** no `AMediaCodec`, `AMediaMuxer`, `AMediaExtractor`, `AMediaFormat`, `AImageReader`. Encoders, muxers and decoders are entirely the caller's; core holds only their clocks and plans.
- **Audio in:** no `AAudio` or Oboe input; capture (mic, output mix) is the caller's. Audio out exists (Oboe).
- **JVM attach:** no `JavaVM` / `AttachCurrentThread` anywhere, so no native thread can call up into Kotlin today; all native→shell data is polled.
- **One surface kind per renderer:** `Renderer` has no surface kind; it infers "wallpaper" from the edge glow being on (`RendererFrame.cpp:483-486`).
- **Host-testable frame driver:** nothing compiles `Renderer` on the host, so a native loop would have no host test without a wider fake GL.

---

### 9. Surprises

- **Core is already one engine.** The "3 engines" are not in `core/`: there is one `Renderer`, one `SoundEngine` class, one `Resolver`. What is repeated is the caller-side protocol of §2.3, which every surface driver has to carry because core has no loop of its own.
- **`geode_viz_frame_begin` is live-only** and export bypasses it (`geode_api.h:161`), so live and export already diverge in protocol: live uses versions and cadence, export uses `prepare_offscreen` + pin + schedule.
- **Two time bases in one frame.** `geode_viz_render` takes the caller's `time_seconds`, while the same call stamps the recorder and tap-along with native `steady_clock` (`geode_viz_api.cpp:912-920`), and `feed_ring` reads `steady_clock` for the speaker limit (`:344-345`).
- **`core/scene/object` (1 441 lines, glTF import and Basis textures, plus the `geode_meshopt` and `geode_basisu` libraries) is compiled into `geode_core` but has no caller**: no function in `geode_api.h`, no reference in `core/api`, `core/viz` or `app/src/main/cpp` (grep for `GltfImport|ModelTextures`). Only `gltf_import_test` uses it.
- **`core/viz/ConfigMath.hpp` is test-only**; its comment says the Kotlin copies still run (`ConfigMath.hpp:10-11`).
- **Comments name another project's source files**: `core/sound/LufsMeter.hpp:6`, `core/sound/Decimate.hpp` and `core/sound/SoundEngine.hpp:29` cite "fosfora" crate paths; the workflow does too (line 95).
- **`Renderer`'s default scene id is a Kotlin constant copied by hand** (`Renderer.hpp:351`, "the app's first style (FreeTier.FIRST_STYLE_ID)").
- **Liquid mode reseeds from `std::random_device` on every switch-on**, exports included (`RendererFrame.cpp:499-504`), while `pinEngineSettings` promises a deterministic export (`geode_api.h:209`). The transition RNG is pinned for export (`:99-100`), Liquid is not.
- **The composite hook is a `std::function` set by the API layer** (`geode_viz_api.cpp:55-59`): record, cast and still read-back are not `frame::Pass`es, and only `EdgeGlowPass`, `WatermarkPass` and `SeamPass` implement `frame::Pass` (`Pass.hpp` comment says the others "are to").
- **ABI functions with no JNI caller** (grep of `app/src/main/cpp`): `geode_params_reset_tab`, `geode_export_seam_capture`, `geode_pcm_cursor_take`, `geode_silence_heard_at`. The last three may be reached through sibling calls (`seam_frame`, `take_s16`, `ring_heard_at`); not traced.
- **No thread is asserted anywhere.** Every "GL thread only" rule is a comment.

### Not verified

- The frame order inside scenes (`Scene::update/draw`) and inside `SoundEngine::step`: `.cpp` not read.
- Thread claims taken from header and ABI comments, not from every `.cpp`.
- Which Kotlin driver calls which sequence: outside this scope; only the JNI symbol presence was grepped.
- Test-to-class mapping in §6 is by workflow comment and link line, not by reading the test sources; rows marked "not verified" are guesses by name.
- Headers in `viz/frame`, `output`, `audio`, `cast`, `sound` other than those listed as read in full were outlined, not read line by line.


---

## Shader assets A: `app/src/main/assets/shaders/`, files 1..150 by name (`acid_show_frag.glsl` .. `hollow_sinew_frag.glsl`)

150 files in scope: 148 `.glsl` files (23,514 lines) and 2 binary tables (`blue_noise_64.bin`, 4,096 bytes; `cyclic_palettes.bin`, 3,840 bytes). Tree read at HEAD `57fdf4a` (the brief names `f13229d`; the working tree is what was read). All 148 `.glsl` files were read in full; the two `.bin` files were sized, not decoded.

Counts by kind: 109 style fragments, 32 passes (2 Acid, 1 composite, 1 Curl Flow field, 1 Cymatics, 1 Endless zoom, 1 edge glow, 1 trails fade, 24 fluid solver/look/particle fragments), 2 export fragments, 5 vertex shaders (1 of them export), 2 data tables. No `lib_*` include and no `ui_*` shader falls in this range (they sort after `hollow_sinew`).

How source is assembled (needed to read the table):

- The include marker is a line `//#include <name>`; `core/viz/ShaderSource.cpp:27-44` parses it, `:67-111` pastes `shaders/<name>.glsl` recursively with a cycle guard, and `:113-118` accepts only names on the allow-list `core/viz/ShaderIncludes.inc` (pasted into the array at `core/viz/ShaderSource.cpp:15-17`).
- `lib_scene_uniforms` itself includes `lib_sound` (`app/src/main/assets/shaders/lib_scene_uniforms.glsl:30`), so every style that lists `scene_uniforms` also gets `uSound`, `uSoundTex`, `uSoundDrive`, `uSoundEvents`, `uSoundCounts` and the `SND_*` names.
- A style id becomes a file name by string concatenation: `loader_.assets.load(id + "_frag.glsl", &error)` at `core/viz/SceneRegistry.cpp:31`. The ids come from `core/viz/Catalogue.cpp`.
- `#define VIEW_OWNS_SHAPE` before the includes switches off `view()`'s Morph, Turbulence and Warp screen distortions (`lib_scene_uniforms.glsl:146-162,182-184,196-202`).

### 1. Inventory

Column notes. "Uniforms declared in the file" are the file's own `uniform` lines. "Shared uniforms named directly" are names the file body uses but an included lib declares; a family style reaches many more through its family lib's helpers (`arbChaos()`, `bulbEnergy()`, `diveSetup()`, `foamSetup()`, `flameSetup()`, `hollowSetup()`, `neonKick()` ...), which live in the lib files outside this range. "Knobs" are indices of `uKnob[6]` (k0 PULSE, k1 TEXTURE, k2 FLOW, k3 TONE, k4 BUILD, k5 SPACE; `lib_scene_uniforms.glsl:68-86`) read in the file body, directly or through a named helper.

| File (lines) | Kind | Family | `//#include` (lib_ prefix dropped) and defines | Uniforms and samplers declared in the file | Shared uniforms named directly in the file (declared by an included lib) | Sound reads (SND_ names; snd* helpers) | Knobs read in the file | Who names and loads it |
|---|---|---|---|---|---|---|---|---|
| `acid_show_frag.glsl` (113) | pass (Acid present) | acid | sound, relief ; defines RELIEF_BUMP, RELIEF_EPS | uState, uRes, uScanline, uCurve, uSat, uFloorHue, uOverdrive, uHit, uDepth, uLookBase, uLookAlt | - | HAT_ENV, HELD_SURGE, KICK_ENV, PRESENCE, TREBLE_PRESENCE | - | native: core/viz/scenes/AcidScene.cpp:64 |
| `acid_step_frag.glsl` (313) | pass (Acid feedback) | acid | none ; defines TOUCH_MAX_POINTS | uPrev, uRes, uStyle, uSource, uZoom, uRotate, uHueShift, uFeedback, uModulate, uClock, uBass, uMid, uTreble, uStrike, uDrive, uSpokes, uBaseHue, uHueSpan, uLiquid, uCentre, uTwist, uDrift, uWobble, uMelody, uTouchPoints, uTouchCount | - | - | - | native: core/viz/scenes/AcidScene.cpp:58 |
| `arbor_bolt_frag.glsl` (352) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uIntensity, uKnob, uSteps, uTime, uTwist | BASS_PRESENCE, LEVEL, ONSET_COUNT, ONSET_ENV ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `arbor_bolt_frag.glsl` from the id (core/viz/Catalogue.cpp:104) |
| `arbor_bonsai_frag.glsl` (164) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uHarmony, uIntensity, uSoundDrive, uSteps, uTime, uTwist | CUT_COUNT, ENERGY_TREND, HELD_HARD_CUT, KICK_ENV | - | native: core/viz/SceneRegistry.cpp:31 builds `arbor_bonsai_frag.glsl` from the id (core/viz/Catalogue.cpp:104) |
| `arbor_canopy_frag.glsl` (165) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uIntensity, uKeyHue, uKeyStrength, uSoundDrive, uSteps, uTime, uTwist | INTENSITY, SNARE_ENV | - | native: core/viz/SceneRegistry.cpp:31 builds `arbor_canopy_frag.glsl` from the id (core/viz/Catalogue.cpp:104) |
| `arbor_coral_frag.glsl` (176) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uIntensity, uSteps, uTime, uTwist | ENERGY_TREND, HELD_SURGE, KICK_ENV ; sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `arbor_coral_frag.glsl` from the id (core/viz/Catalogue.cpp:104) |
| `arbor_frost_frag.glsl` (300) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uIntensity, uSoundDrive, uSteps, uTime, uTwist | CUT_COUNT, HELD_HARD_CUT, INTENSITY, KICK_ENV, TREBLE_PRESENCE | - | native: core/viz/SceneRegistry.cpp:31 builds `arbor_frost_frag.glsl` from the id (core/viz/Catalogue.cpp:104) |
| `arbor_grove_frag.glsl` (309) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uIntensity, uKnob, uSteps, uTime, uTwist | BASS_PRESENCE, KICK_ENV, LEVEL ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `arbor_grove_frag.glsl` from the id (core/viz/Catalogue.cpp:104) |
| `arbor_roots_frag.glsl` (337) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uCamBasis, uIntensity, uKnob, uSteps, uTime, uTwist | BASS_PRESENCE, KICK_ENV, MID_PRESENCE, SUB_ATT ; sndLevel | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `arbor_roots_frag.glsl` from the id (core/viz/Catalogue.cpp:104) |
| `arbor_rosette_frag.glsl` (330) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uIntensity, uKeyHue, uKeyStrength, uKnob, uSoundDrive, uSteps, uTime, uTwist | HAT_ENV, HELD_NOTE_CHANGE, LEVEL | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `arbor_rosette_frag.glsl` from the id (core/viz/Catalogue.cpp:105) |
| `arbor_sakura_frag.glsl` (323) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uCamFocal, uIntensity, uKnob, uSteps, uTime, uTwist | HIT_TIME, INTENSITY, ONSET_ENV, TREBLE_ATT ; sndLevel | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `arbor_sakura_frag.glsl` from the id (core/viz/Catalogue.cpp:105) |
| `arbor_spire_frag.glsl` (178) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uIntensity, uMotionBright, uSoundDrive, uSteps, uTime, uTwist | BASS_PRESENCE, HAT_ENV, KICK_ENV, MID_PRESENCE | - | native: core/viz/SceneRegistry.cpp:31 builds `arbor_spire_frag.glsl` from the id (core/viz/Catalogue.cpp:105) |
| `arbor_synapse_frag.glsl` (173) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uFlowTime, uIntensity, uSteps, uTime, uTwist | ENERGY_TREND, FLUX, HELD_SURGE ; sndLevel | - | native: core/viz/SceneRegistry.cpp:31 builds `arbor_synapse_frag.glsl` from the id (core/viz/Catalogue.cpp:105) |
| `arbor_willow_frag.glsl` (217) | style fragment | arbor | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, arbor | - | uIntensity, uSoundDrive, uSteps, uTime, uTwist | BASS_PRESENCE, HAT_ENV, KICK_ENV, MID_PRESENCE | - | native: core/viz/SceneRegistry.cpp:31 builds `arbor_willow_frag.glsl` from the id (core/viz/Catalogue.cpp:104) |
| `aurora_frag.glsl` (237) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 ; defines VIEW_OWNS_SHAPE | - | uKnob, uMorph, uSoundTimeSped, uSteps, uTime, uTurbulence, uWarp | ENERGY_TREND, HELD_NOTE_CHANGE, LOW, MID_ATT, TREBLE_ATT, VOL_ATT ; sndLevel, sndShaped | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `aurora_frag.glsl` from the id (core/viz/Catalogue.cpp:19) |
| `aurora_ocean_frag.glsl` (200) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 | - | uSteps, uTime | HAT_ENV, HELD_SURGE, INTENSITY, LOW, SNARE_ENV ; sndShaped, sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `aurora_ocean_frag.glsl` from the id (core/viz/Catalogue.cpp:29) |
| `bars_frag.glsl` (328) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 ; defines VIEW_OWNS_SHAPE | - | uKnob, uMorph, uSteps, uTime, uTurbulence, uWarp | BASS_ATT, ENERGY_TREND, HIGH, KICK_TOGGLE, MID_ATT, SNARE_ENV ; sndEq, sndLevel, sndShaped, sndSpectrum | k0,k1,k2,k3,k4,k5 via barsKnob | native: core/viz/SceneRegistry.cpp:31 builds `bars_frag.glsl` from the id (core/viz/Catalogue.cpp:18) |
| `bead_vortex_frag.glsl` (352) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, touch | - | uCamFocal, uFlowPhase, uIntensity, uKnob, uMorph, uResolution, uSoundDrive, uSoundTimeSped, uSteps, uTime, uTurbulence, uWarp | ENERGY_TREND, HAT_ENV, HELD_NOTE_CHANGE, HELD_SURGE, KICK_ENV, SNARE_ENV | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `bead_vortex_frag.glsl` from the id (core/viz/Catalogue.cpp:23) |
| `black_hole_frag.glsl` (350) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, sdf3 ; defines VIEW_OWNS_SHAPE | - | uSteps, uTime | BASS_TIME, CHORD_HUE, CUT_COUNT, SNARE_ENV ; sndShaped | k0,k1,k2,k3,k4,k5 via neonKnob | native: core/viz/SceneRegistry.cpp:31 builds `black_hole_frag.glsl` from the id (core/viz/Catalogue.cpp:37) |
| `blacklight_bloom_frag.glsl` (439) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, touch | - | uCamFocal, uKaleido, uKnob, uMorph, uResolution, uSteps, uSymmetry, uTime, uTurbulence | BASS_PRESENCE, CUT_COUNT, ENERGY_TREND, HAT_ENV, HELD_NOTE_CHANGE, HIGH, KICK_ENV ; sndShaped | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `blacklight_bloom_frag.glsl` from the id (core/viz/Catalogue.cpp:25) |
| `blue_noise_64.bin` (4096 bytes) | data table | - | - | - | - | - | - | native: core/viz/BlueNoise.cpp:6, core/viz/BlueNoise.hpp:11 |
| `bulb_cavern_frag.glsl` (160) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, bulb ; defines VIEW_OWNS_SHAPE | - | uCamBasis, uCamFocal, uCamPos, uCamTravel, uSoundDrive, uSteps | BASS_PRESENCE, HELD_SURGE, MID_PRESENCE | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_cavern_frag.glsl` from the id (core/viz/Catalogue.cpp:108) |
| `bulb_chrome_frag.glsl` (159) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, bulb ; defines VIEW_OWNS_SHAPE | - | uCamDolly, uCamTravel, uSteps, uTurbulence | BASS_PRESENCE, HAT_ENV, MID_PRESENCE ; sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_chrome_frag.glsl` from the id (core/viz/Catalogue.cpp:109) |
| `bulb_classic_frag.glsl` (132) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, bulb ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength, uSteps | INTENSITY, LOW ; sndShaped, sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_classic_frag.glsl` from the id (core/viz/Catalogue.cpp:108) |
| `bulb_contour_frag.glsl` (171) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, bulb ; defines VIEW_OWNS_SHAPE | - | uCamDolly, uCamTravel, uMorph, uSteps | CURVED_TIME, LEVEL ; sndPhase, sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_contour_frag.glsl` from the id (core/viz/Catalogue.cpp:109) |
| `bulb_glow_frag.glsl` (127) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, bulb ; defines VIEW_OWNS_SHAPE | - | uSteps | CUT_COUNT, FLUX, HELD_HARD_CUT, LEVEL ; sndLevel | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_glow_frag.glsl` from the id (core/viz/Catalogue.cpp:108) |
| `bulb_hybrid_frag.glsl` (187) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, bulb ; defines VIEW_OWNS_SHAPE | - | uCamTravel, uDrift, uHarmony, uSoundDrive, uSteps | ENERGY_TREND, KICK_TOGGLE | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_hybrid_frag.glsl` from the id (core/viz/Catalogue.cpp:109) |
| `bulb_ink_frag.glsl` (147) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, bulb ; defines VIEW_OWNS_SHAPE | - | uSoundDrive, uSteps | INTENSITY, SNARE_ENV | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_ink_frag.glsl` from the id (core/viz/Catalogue.cpp:108) |
| `bulb_julia_frag.glsl` (145) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, bulb ; defines VIEW_OWNS_SHAPE | - | uCamDolly, uCamTravel, uKeyHue, uSteps, uTurbulence | HELD_NOTE_CHANGE, HIGH, LEVEL ; sndShaped | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_julia_frag.glsl` from the id (core/viz/Catalogue.cpp:108) |
| `bulb_lotus_frag.glsl` (177) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, bulb ; defines VIEW_OWNS_SHAPE | - | uCamDolly, uCamTravel, uSteps | ENERGY_TREND, HAT_ENV, HIGH, MIDDLE ; sndShaped | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_lotus_frag.glsl` from the id (core/viz/Catalogue.cpp:108) |
| `bulb_prism_frag.glsl` (166) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, bulb ; defines VIEW_OWNS_SHAPE | - | uDrift, uSteps | HIT_TIME, INTENSITY, LOW, ONSET_ENV, TREBLE_PRESENCE ; sndShaped | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_prism_frag.glsl` from the id (core/viz/Catalogue.cpp:109) |
| `bulb_shell_frag.glsl` (181) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, bulb ; defines VIEW_OWNS_SHAPE | - | uCamDolly, uCamTravel, uKeyHue, uKeyStrength, uSteps | BASS_PRESENCE, ENERGY_TREND ; sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_shell_frag.glsl` from the id (core/viz/Catalogue.cpp:108) |
| `bulb_slice_frag.glsl` (143) | style fragment | bulb | scene_uniforms, scene_motion, camera, palette, scene_grade, bulb ; defines VIEW_OWNS_SHAPE | - | uCamDolly, uOrbit, uSoundDrive, uSteps | BASS_PRESENCE, KICK_ENV, MID_PRESENCE | - | native: core/viz/SceneRegistry.cpp:31 builds `bulb_slice_frag.glsl` from the id (core/viz/Catalogue.cpp:108) |
| `chroma_orb_frag.glsl` (330) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, touch ; defines VIEW_OWNS_SHAPE | - | uFlowPhase, uIntensity, uKaleido, uKnob, uMorph, uSteps, uSymmetry, uTime, uTurbulence, uWarp | BASS_ATT, BASS_PRESENCE, ENERGY_TREND, HELD_NOTE_CHANGE, KICK_ENV, MID_PRESENCE, SNARE_ENV, TREBLE_PRESENCE ; sndLevel | k0,k1,k2,k3,k4,k5 via knob | native: core/viz/SceneRegistry.cpp:31 builds `chroma_orb_frag.glsl` from the id (core/viz/Catalogue.cpp:23) |
| `chrome_nebula_frag.glsl` (168) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 | - | uFlowPhase, uSteps, uTime | BASS_PRESENCE, HELD_NOTE_CHANGE, HIT_TIME, MID_PRESENCE, ONSET_ENV ; sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `chrome_nebula_frag.glsl` from the id (core/viz/Catalogue.cpp:29) |
| `chrys_bloom_frag.glsl` (322) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uCamBasis, uCamDolly, uCamFocal, uCamTravel, uDrift, uIntensity, uKnob, uMorph, uSoundDrive, uSteps, uTime, uTurbulence, uTwist, uWarp | BASS_PRESENCE, ENERGY_TREND, KICK_ENV, MID_PRESENCE | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_bloom_frag.glsl` from the id (core/viz/Catalogue.cpp:112) |
| `chrys_corolla_frag.glsl` (212) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uDrift, uIntensity, uMorph, uSteps, uTime, uTurbulence, uTwist, uWarp | BASS_ATT, ENERGY_TREND, HELD_SURGE ; sndLevel | - | native: core/viz/SceneRegistry.cpp:31 builds `chrys_corolla_frag.glsl` from the id (core/viz/Catalogue.cpp:112) |
| `chrys_dahlia_frag.glsl` (276) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uDrift, uIntensity, uKnob, uMorph, uSteps, uTime, uTurbulence, uTwist, uWarp | INTENSITY, LOW, MID_PRESENCE, TREBLE_PRESENCE ; sndShaped, sndSpectrum | k0,k1,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_dahlia_frag.glsl` from the id (core/viz/Catalogue.cpp:112) |
| `chrys_fall_frag.glsl` (275) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, chrys ; defines VIEW_OWNS_SHAPE | - | uFlowPhase, uIntensity, uKnob, uMorph, uSteps, uTime, uTurbulence, uTwist, uWarp | ENERGY_TREND, HAT_ENV ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_fall_frag.glsl` from the id (core/viz/Catalogue.cpp:113) |
| `chrys_glass_frag.glsl` (444) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uCamFocal, uIntensity, uKeyHue, uKeyStrength, uKnob, uMorph, uResolution, uSteps, uSymmetry, uTime, uTurbulence, uTwist, uWarp | HELD_NOTE_CHANGE, INTENSITY ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_glass_frag.glsl` from the id (core/viz/Catalogue.cpp:112) |
| `chrys_ink_frag.glsl` (444) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uDrift, uIntensity, uKnob, uMorph, uResolution, uSteps, uTime, uTurbulence, uTwist, uWarp | FLUX, ONSET_COUNT ; sndLevel, sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_ink_frag.glsl` from the id (core/viz/Catalogue.cpp:112) |
| `chrys_kaleido_frag.glsl` (368) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uBarOsc, uCamBasis, uCamFocal, uFlowPhase, uIntensity, uKnob, uMorph, uResolution, uSteps, uSymmetry, uTime, uTurbulence, uTwist, uWarp | INTENSITY, KICK_ENV, MID_HIGH_ATT ; sndLevel | k0,k1,k2,k3,k4,k5 via ckKnob | native: core/viz/SceneRegistry.cpp:31 builds `chrys_kaleido_frag.glsl` from the id (core/viz/Catalogue.cpp:113) |
| `chrys_lotus_frag.glsl` (378) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uBarOsc, uIntensity, uKnob, uMorph, uSteps, uTime, uTurbulence, uTwist, uWarp | BASS_ATT, LEVEL, SNARE_ENV ; sndLevel | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_lotus_frag.glsl` from the id (core/viz/Catalogue.cpp:112) |
| `chrys_meadow_frag.glsl` (341) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, chrys ; defines VIEW_OWNS_SHAPE | - | uCamBasis, uCamFocal, uIntensity, uKeyHue, uKeyStrength, uKnob, uMorph, uResolution, uSteps, uTime, uTurbulence, uTwist, uWarp | BASS_PRESENCE, MID_PRESENCE ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_meadow_frag.glsl` from the id (core/viz/Catalogue.cpp:112) |
| `chrys_neon_frag.glsl` (325) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uDrift, uIntensity, uKnob, uMorph, uSteps, uTime, uTurbulence, uTwist, uWarp | BASS_PRESENCE, CUT_COUNT, HELD_HARD_CUT, MID_PRESENCE ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_neon_frag.glsl` from the id (core/viz/Catalogue.cpp:113) |
| `chrys_phyllo_frag.glsl` (349) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uBreath, uFlowPhase, uIntensity, uKnob, uMorph, uSteps, uTime, uTurbulence, uTwist, uWarp | ENERGY_TREND, ONSET_COUNT, ONSET_ENV ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_phyllo_frag.glsl` from the id (core/viz/Catalogue.cpp:112) |
| `chrys_tunnel_frag.glsl` (308) | style fragment | chrys | scene_uniforms, scene_motion, camera, palette, scene_grade, chrys ; defines VIEW_OWNS_SHAPE | - | uIntensity, uKnob, uMorph, uSteps, uTime, uTurbulence, uTwist, uWarp | CUT_COUNT, HELD_HARD_CUT, LEVEL ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `chrys_tunnel_frag.glsl` from the id (core/viz/Catalogue.cpp:113) |
| `composite_frag.glsl` (443) | pass (final composite, transitions, Layers, underlay, dither) | loose | none | uTexA, uTexB, uNoise, uDither, uRatio, uProgress, uStyle, uLayerMix, uBlendMode, uTime, uChroma, uVignette, uScanline, uGrain, uFisheye, uGateA, uGateB, uCamFlat, uCamTilt, uCamGate, uPostWarp, uPostRipple, uPostSymmetry, uPostKaleido, uPostPixelate, uPostTile, uPostTwist, uPostMirror, uPostBloom, uPostPosterize, uPostInvert, uPostDriftX, uPostDriftY, uPostSway, uPostTemp, uPostSolarize, uPostZoom, uPostRotation, uPostSat, uPostBright, uPostContrast, uPostGamma, uPostHue, uFlow, uFlowStrength, uUnderlay, uUnderlayBlend, uUnderlayAmount | - | - | - | native: core/viz/Transition.cpp:77, core/viz/CompositePass.hpp:15, core/viz/CompositePass.hpp:22; **Kotlin: engine/scenes/src/main/kotlin/dev/geode/render/scene/ParamScope.kt:15, engine/scenes/src/main/kotlin/dev/geode/render/UnderlayBlend.kt:9** |
| `crystal_canyon_frag.glsl` (220) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 | - | uSteps, uTime | ENERGY_TREND, HELD_NOTE_CHANGE, KICK_ENV, LEVEL ; sndShaped, sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `crystal_canyon_frag.glsl` from the id (core/viz/Catalogue.cpp:29) |
| `curl_bloom_frag.glsl` (227) | style fragment | loose | scene_uniforms, scene_motion, palette, scene_grade, sdf3, touch, dmt | - | uFlowPhase, uFormPhase, uMoveDir, uSpawnSeed, uSteps, uTime | BASS_PRESENCE, HIGH, LEVEL ; sndShaped | - | native: core/viz/SceneRegistry.cpp:31 builds `curl_bloom_frag.glsl` from the id (core/viz/Catalogue.cpp:27) |
| `curl_field_frag.glsl` (66) | pass (Curl Flow velocity field) | loose | psrdnoise2 | uTime, uFreq, uDetail, uAmp, uPeriod | - | - | - | native: core/viz/scenes/CurlFlowScene.cpp:23 |
| `cyclic_palettes.bin` (3840 bytes) | data table | - | - | - | - | - | - | native: core/viz/Renderer.cpp:423, core/viz/PaletteTable.inc:30 |
| `cymatics_field_frag.glsl` (544) | pass (Cymatics scene, 11 substyles) | loose | none ; defines TOUCH_MAX_POINTS | uResolution, uTime, uStyle, uModes, uModeCount, uGeometry, uScale, uHeightNorm, uLine, uGlow, uIridescence, uCaustic, uFill, uSwirlPhase, uTravelPhase, uDriftShift, uFieldLive, uBaseHue, uHueSpan, uEnergy, uTreble, uExposure, uTouchPoints, uTouchCount, uTouchK, uTouchPhase | - | - | - | native: core/viz/scenes/CymaticsScene.cpp:19 |
| `dive_accordion_frag.glsl` (68) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | BASS_PRESENCE, LOW, MID_PRESENCE ; sndShaped | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_accordion_frag.glsl` from the id (core/viz/Catalogue.cpp:121) |
| `dive_cycle_frag.glsl` (56) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | LEVEL | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_cycle_frag.glsl` from the id (core/viz/Catalogue.cpp:121) |
| `dive_frag.glsl` (89) | pass (Endless zoom) | (none: Endless zoom pass, not the dive_ family) | none | uScene, uTexel, uAspect, uPhase, uMix, uInner, uBase, uStep, uTwist, uCentre, uShape, uBlend, uFog, uLens | - | - | - | native: core/viz/EndlessZoom.hpp:15, core/viz/EndlessZoom.hpp:125, core/viz/DivePass.cpp:9 |
| `dive_gyro_frag.glsl` (58) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | BASS_PRESENCE, MID_PRESENCE | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_gyro_frag.glsl` from the id (core/viz/Catalogue.cpp:120) |
| `dive_helix_frag.glsl` (68) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | HAT_ENV, HELD_SURGE, LEVEL, ONSET_COUNT | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_helix_frag.glsl` from the id (core/viz/Catalogue.cpp:120) |
| `dive_jewel_frag.glsl` (67) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | HAT_ENV, HIT_TIME, INTENSITY | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_jewel_frag.glsl` from the id (core/viz/Catalogue.cpp:120) |
| `dive_lattice_frag.glsl` (57) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | ENERGY_TREND | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_lattice_frag.glsl` from the id (core/viz/Catalogue.cpp:120) |
| `dive_mandala_frag.glsl` (62) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | ENERGY_TREND | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_mandala_frag.glsl` from the id (core/viz/Catalogue.cpp:120) |
| `dive_mushroom_frag.glsl` (64) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | LEVEL | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_mushroom_frag.glsl` from the id (core/viz/Catalogue.cpp:120) |
| `dive_neon_frag.glsl` (67) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | uSteps | INTENSITY, ONSET_ENV | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_neon_frag.glsl` from the id (core/viz/Catalogue.cpp:121) |
| `dive_orbit_frag.glsl` (60) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | ENERGY_TREND | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_orbit_frag.glsl` from the id (core/viz/Catalogue.cpp:121) |
| `dive_serpent_frag.glsl` (59) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | INTENSITY, LOW ; sndShaped | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_serpent_frag.glsl` from the id (core/viz/Catalogue.cpp:120) |
| `dive_show_frag.glsl` (80) | style fragment | dive | scene_uniforms, scene_motion, camera, palette, scene_grade, dive ; defines VIEW_OWNS_SHAPE | - | - | BASS_PRESENCE, HAT_ENV, HELD_SURGE, HIT_TIME, LOW, MID_PRESENCE ; sndShaped | - | native: core/viz/SceneRegistry.cpp:31 builds `dive_show_frag.glsl` from the id (core/viz/Catalogue.cpp:121) |
| `edge_glow_frag.glsl` (26) | pass (wallpaper edge) | loose | none | uWindow, uWidth, uRadius | - | - | - | native: core/viz/frame/EdgeGlowMath.hpp:145, core/viz/frame/EdgeGlowPass.cpp:8 |
| `export_seam_frag.glsl` (12) | export pass | export | none | uTex, uAlpha | - | - | - | native: core/viz/frame/ExportPasses.cpp:117 |
| `export_watermark_frag.glsl` (19) | export pass | export | none | uTexture, uClip, uDim, uAlpha | - | - | - | native: core/viz/frame/ExportPasses.cpp:48 |
| `export_watermark_vert.glsl` (16) | export vertex | export | none | uTiles, uOrigin | - | - | - | native: core/viz/frame/ExportPasses.cpp:48 |
| `fade_frag.glsl` (14) | pass (trails fade) | fade | none | uFadeAlpha | - | - | - | native: core/viz/TrailPass.cpp:13 |
| `fade_vert.glsl` (8) | vertex (fullscreen triangle) | fade | none | - | - | - | - | native: core/viz/scenes/MilkdropScene.cpp:117, core/viz/Renderer.cpp:405 |
| `flame_aurora_frag.glsl` (45) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `flame_aurora_frag.glsl` from the id (core/viz/Catalogue.cpp:145) |
| `flame_helix_frag.glsl` (45) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | uTime | - | - | native: core/viz/SceneRegistry.cpp:31 builds `flame_helix_frag.glsl` from the id (core/viz/Catalogue.cpp:145) |
| `flame_knot_frag.glsl` (53) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | uTime | - | k5 via neonKnob | native: core/viz/SceneRegistry.cpp:31 builds `flame_knot_frag.glsl` from the id (core/viz/Catalogue.cpp:145) |
| `flame_lotus_frag.glsl` (49) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | uTime | - | k5 via neonKnob | native: core/viz/SceneRegistry.cpp:31 builds `flame_lotus_frag.glsl` from the id (core/viz/Catalogue.cpp:144) |
| `flame_nova_frag.glsl` (48) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | uTime | - | k5 via neonKnob | native: core/viz/SceneRegistry.cpp:31 builds `flame_nova_frag.glsl` from the id (core/viz/Catalogue.cpp:145) |
| `flame_orb_frag.glsl` (54) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | uTime | - | k5 via neonKnob | native: core/viz/SceneRegistry.cpp:31 builds `flame_orb_frag.glsl` from the id (core/viz/Catalogue.cpp:144) |
| `flame_spiral_frag.glsl` (46) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | uTime | - | - | native: core/viz/SceneRegistry.cpp:31 builds `flame_spiral_frag.glsl` from the id (core/viz/Catalogue.cpp:144) |
| `flame_star_frag.glsl` (48) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | uTime | - | - | native: core/viz/SceneRegistry.cpp:31 builds `flame_star_frag.glsl` from the id (core/viz/Catalogue.cpp:144) |
| `flame_veil_frag.glsl` (45) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `flame_veil_frag.glsl` from the id (core/viz/Catalogue.cpp:144) |
| `flame_web_frag.glsl` (50) | style fragment | flame | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, flame, flame_march ; defines VIEW_OWNS_SHAPE | - | uTime | - | - | native: core/viz/SceneRegistry.cpp:31 builds `flame_web_frag.glsl` from the id (core/viz/Catalogue.cpp:145) |
| `fluid_advect_frag.glsl` (46) | pass (fluid solver/look) | fluid | none | uVelocity, uSource, uSrcInvRes, uVelInvRes, uDt, uRdx, uDecay | - | - | - | native: core/viz/fluid/FluidSim.cpp:17 |
| `fluid_base_vert.glsl` (22) | vertex (fluid grid) | fluid | none | uInvRes, uAspect | - | - | - | native: core/viz/scenes/CurlFlowScene.cpp:23, core/viz/fluid/FluidSim.cpp:44, core/viz/fluid/FluidParticles.cpp:29 |
| `fluid_bloom_blur_frag.glsl` (21) | pass (fluid solver/look) | fluid | none | uTexture | - | - | - | native: core/viz/fluid/FluidLook.cpp:23 |
| `fluid_bloom_final_frag.glsl` (20) | pass (fluid solver/look) | fluid | none | uTexture, uIntensity | - | - | - | native: core/viz/fluid/FluidLook.cpp:24 |
| `fluid_bloom_prefilter_frag.glsl` (23) | pass (fluid solver/look) | fluid | none | uTexture, uCurve, uThreshold | - | - | - | native: core/viz/fluid/FluidLook.cpp:22 |
| `fluid_blur_frag.glsl` (20) | pass (fluid solver/look) | fluid | none | uTexture, uDirection | - | - | - | native: core/viz/fluid/FluidLook.cpp:27 |
| `fluid_clear_frag.glsl` (19) | pass (fluid solver/look) | fluid | none | uTexture, uValue | - | - | - | native: core/viz/fluid/FluidSim.cpp:26 |
| `fluid_copy_frag.glsl` (12) | pass (fluid solver/look) | fluid | none | uTexture | - | - | - | native: core/viz/fluid/FluidSim.cpp:27 |
| `fluid_curl_frag.glsl` (19) | pass (fluid solver/look) | fluid | none | uVelocity, uHalfRdx | - | - | - | native: core/viz/fluid/FluidSim.cpp:19 |
| `fluid_display_frag.glsl` (266) | pass (fluid solver/look) | fluid | none | uDye, uBloom, uSunrays, uDither, uDitherScale, uTexelSize, uTime, uAudio, uLookHue, uTemperature | - | - | - | native: core/viz/fluid/FluidSim.cpp:28, core/viz/fluid/FluidLook.hpp:23, core/viz/fluid/FluidLook.cpp:28 |
| `fluid_divergence_frag.glsl` (33) | pass (fluid solver/look) | fluid | none | uVelocity, uHalfRdx, uInvRes | - | - | - | native: core/viz/fluid/FluidSim.cpp:21 |
| `fluid_gradient_frag.glsl` (34) | pass (fluid solver/look) | fluid | none | uPressure, uVelocity, uHalfRdx, uInvRes | - | - | - | native: core/viz/fluid/FluidSim.cpp:25 |
| `fluid_maccormack_frag.glsl` (66) | pass (fluid solver/look) | fluid | none | uVelocity, uSource, uForward, uSrcInvRes, uVelInvRes, uDt, uRdx, uDecay | - | - | - | native: core/viz/fluid/FluidSim.cpp:18 |
| `fluid_mg_prolong_frag.glsl` (27) | pass (fluid solver/look) | fluid | none | uP, uE | - | - | - | native: core/viz/fluid/FluidSim.cpp:24 |
| `fluid_mg_restrict_frag.glsl` (30) | pass (fluid solver/look) | fluid | none | uP, uRhs, uH2 | - | - | - | native: core/viz/fluid/FluidSim.cpp:23 |
| `fluid_mg_smooth_frag.glsl` (35) | pass (fluid solver/look) | fluid | none | uP, uRhs, uH2, uOmega | - | - | - | native: core/viz/fluid/FluidSim.cpp:22 |
| `fluid_particle_frag.glsl` (51) | pass (fluid solver/look) | fluid | particle_common, particle_shade | uHueBase, uHueSpan, uBrightness, uShape, uGlow, uTime | - | - | - | native: core/viz/fluid/FluidParticles.cpp:32 |
| `fluid_particle_seed_frag.glsl` (54) | pass (fluid solver/look) | fluid | none | uAspect, uSpawns, uSpawnCount, uLife | - | - | - | native: core/viz/fluid/FluidParticles.cpp:30 |
| `fluid_particle_streak_frag.glsl` (46) | pass (fluid solver/look) | fluid | particle_common, particle_shade | uHueBase, uHueSpan, uBrightness, uShape, uGlow, uTime | - | - | - | native: core/viz/fluid/FluidParticles.cpp:45 |
| `fluid_particle_streak_vert.glsl` (67) | vertex (fluid tracers, streak quads) | fluid | particle_common | uState, uMeta, uAspect, uPointScale, uViewport, uStretchSeconds, uMaxStretch | - | - | - | native: core/viz/fluid/FluidParticles.cpp:45 |
| `fluid_particle_update_frag.glsl` (141) | pass (fluid solver/look) | fluid | psrdnoise2 | uState, uMeta, uVelocityField, uAspect, uDt, uDrag, uFlowScale, uTime, uLife, uSpawns, uSpawnCount, uCatches, uCatchCount, uStir, uStirFreq, uStirTime | - | - | - | native: core/viz/fluid/FluidParticles.cpp:31 |
| `fluid_particle_vert.glsl` (48) | vertex (fluid tracers, points) | fluid | particle_common | uState, uMeta, uAspect, uPointScale | - | - | - | native: core/viz/fluid/FluidParticles.cpp:32 |
| `fluid_splat_batch_frag.glsl` (74) | pass (fluid solver/look) | fluid | none | uTarget, uSeg, uVal, uCount, uMode, uCeiling, uHeat, uMaxHeat | - | - | - | native: core/viz/fluid/FluidSim.cpp:16, core/viz/fluid/FluidMath.hpp:29, core/viz/fluid/FluidMath.hpp:42 |
| `fluid_splat_frag.glsl` (66) | pass (fluid solver/look) | fluid | none | uTarget, uPrev, uCur, uRadius, uValue, uMode, uCeiling | - | - | - | native: core/viz/fluid/FluidSim.cpp:15; **Kotlin: engine/scenes/src/main/kotlin/dev/geode/render/fluid/FluidShaderTemplate.kt:6** |
| `fluid_sunrays_frag.glsl` (32) | pass (fluid solver/look) | fluid | none | uTexture, uWeight | - | - | - | native: core/viz/fluid/FluidLook.cpp:26 |
| `fluid_sunrays_mask_frag.glsl` (18) | pass (fluid solver/look) | fluid | none | uTexture | - | - | - | native: core/viz/fluid/FluidLook.cpp:25 |
| `fluid_vorticity_frag.glsl` (44) | pass (fluid solver/look) | fluid | none | uVelocity, uCurl, uCurlStrength, uDx, uDt, uTemperature, uBuoyancy, uMaxHeat | - | - | - | native: core/viz/fluid/FluidSim.cpp:20 |
| `foam_chrome_frag.glsl` (53) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_chrome_frag.glsl` from the id (core/viz/Catalogue.cpp:140) |
| `foam_coral_frag.glsl` (51) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | - | BASS_TIME, MID_TIME | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_coral_frag.glsl` from the id (core/viz/Catalogue.cpp:141) |
| `foam_ember_frag.glsl` (53) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_ember_frag.glsl` from the id (core/viz/Catalogue.cpp:140) |
| `foam_fall_frag.glsl` (36) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_fall_frag.glsl` from the id (core/viz/Catalogue.cpp:140) |
| `foam_ghost_frag.glsl` (58) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | uSteps | sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_ghost_frag.glsl` from the id (core/viz/Catalogue.cpp:141) |
| `foam_ice_frag.glsl` (49) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_ice_frag.glsl` from the id (core/viz/Catalogue.cpp:140) |
| `foam_ink_frag.glsl` (53) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_ink_frag.glsl` from the id (core/viz/Catalogue.cpp:141) |
| `foam_kaleido_frag.glsl` (49) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | uKaleido | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_kaleido_frag.glsl` from the id (core/viz/Catalogue.cpp:141) |
| `foam_neon_frag.glsl` (39) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_neon_frag.glsl` from the id (core/viz/Catalogue.cpp:140) |
| `foam_orb_frag.glsl` (48) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | uBreath | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_orb_frag.glsl` from the id (core/viz/Catalogue.cpp:140) |
| `foam_show_frag.glsl` (51) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_show_frag.glsl` from the id (core/viz/Catalogue.cpp:141) |
| `foam_soap_frag.glsl` (38) | style fragment | foam | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, foam ; defines VIEW_OWNS_SHAPE | - | uHarmony | - | - | native: core/viz/SceneRegistry.cpp:31 builds `foam_soap_frag.glsl` from the id (core/viz/Catalogue.cpp:140) |
| `fractal_dive_frag.glsl` (153) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 | - | uOrbit, uSoundDrive, uSteps, uTime | ENERGY_TREND, HELD_HARD_CUT, HELD_SURGE, KICK_ENV | - | native: core/viz/SceneRegistry.cpp:31 builds `fractal_dive_frag.glsl` from the id (core/viz/Catalogue.cpp:29) |
| `fractal_temple_frag.glsl` (430) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3, touch ; defines VIEW_OWNS_SHAPE | - | uCamFocal, uFlowTime, uKnob, uMorph, uResolution, uSteps, uTime, uTurbulence, uWarp | CUT_COUNT, ENERGY_TREND, HELD_HARD_CUT, HELD_SURGE, INTENSITY, KICK_ENV, LOW, TREBLE_PRESENCE ; sndShaped | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `fractal_temple_frag.glsl` from the id (core/viz/Catalogue.cpp:25) |
| `fractal_tunnel_frag.glsl` (371) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 | - | uCamFocal, uIntensity, uKaleido, uKeyHue, uKeyStrength, uMorph, uMotionBright, uResolution, uSteps, uSymmetry | BASS_TIME, CURVED_TIME, CUT_COUNT, ENERGY_TREND, FLUX, HAT_ENV, HELD_HARD_CUT, HELD_SURGE, HIT_TIME, KICK_ENV, MID_TIME, PITCH_CLASS, PITCH_CONFIDENCE, SNARE_ENV, SUB_ATT, TREBLE_TIME ; sndLevel | - | native: core/viz/SceneRegistry.cpp:31 builds `fractal_tunnel_frag.glsl` from the id (core/viz/Catalogue.cpp:34) |
| `glass_bubbles_frag.glsl` (342) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength, uKnob, uSteps | CURVED_TIME, ONSET_COUNT, ONSET_ENV | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `glass_bubbles_frag.glsl` from the id (core/viz/Catalogue.cpp:124) |
| `glass_caustic_frag.glsl` (392) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uCamBasis, uCamFocal, uCamPos, uKeyHue, uKeyStrength, uKnob, uSteps | - | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `glass_caustic_frag.glsl` from the id (core/viz/Catalogue.cpp:124) |
| `glass_cubes_frag.glsl` (189) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength, uSteps | ONSET_COUNT, ONSET_ENV, SNARE_TOGGLE | - | native: core/viz/SceneRegistry.cpp:31 builds `glass_cubes_frag.glsl` from the id (core/viz/Catalogue.cpp:125) |
| `glass_drops_frag.glsl` (384) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uCamBasis, uKeyHue, uKeyStrength, uKnob, uSteps | ONSET_COUNT, ONSET_ENV, SNARE_ENV | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `glass_drops_frag.glsl` from the id (core/viz/Catalogue.cpp:124) |
| `glass_gem_frag.glsl` (204) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength | ONSET_COUNT, ONSET_ENV | - | native: core/viz/SceneRegistry.cpp:31 builds `glass_gem_frag.glsl` from the id (core/viz/Catalogue.cpp:125) |
| `glass_ice_frag.glsl` (205) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength, uSteps | ONSET_COUNT | - | native: core/viz/SceneRegistry.cpp:31 builds `glass_ice_frag.glsl` from the id (core/viz/Catalogue.cpp:125) |
| `glass_knot_frag.glsl` (193) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength, uSteps | CURVED_TIME, ONSET_COUNT, SNARE_ENV | - | native: core/viz/SceneRegistry.cpp:31 builds `glass_knot_frag.glsl` from the id (core/viz/Catalogue.cpp:125) |
| `glass_lens_frag.glsl` (518) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uCamFocal, uKeyHue, uKeyStrength, uKnob, uResolution, uSteps | CURVED_TIME, SNARE_TOGGLE ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `glass_lens_frag.glsl` from the id (core/viz/Catalogue.cpp:124) |
| `glass_marble_frag.glsl` (283) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength, uKnob, uSteps | ONSET_COUNT, ONSET_ENV | k0,k3,k5 | native: core/viz/SceneRegistry.cpp:31 builds `glass_marble_frag.glsl` from the id (core/viz/Catalogue.cpp:124) |
| `glass_orb_frag.glsl` (387) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength, uKnob, uSteps | sndEq | k0,k1,k2,k3,k4,k5 via knob | native: core/viz/SceneRegistry.cpp:31 builds `glass_orb_frag.glsl` from the id (core/viz/Catalogue.cpp:124) |
| `glass_pool_frag.glsl` (395) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uCamBasis, uKeyHue, uKeyStrength, uKnob, uSteps | KICK_ENV, ONSET_COUNT | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `glass_pool_frag.glsl` from the id (core/viz/Catalogue.cpp:124) |
| `glass_prism_frag.glsl` (552) | style fragment | glass | scene_uniforms, scene_motion, camera, palette, scene_grade, glass ; defines VIEW_OWNS_SHAPE | - | uKeyHue, uKeyStrength, uKnob, uSteps | SNARE_ENV | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `glass_prism_frag.glsl` from the id (core/viz/Catalogue.cpp:124) |
| `glow_vortex_frag.glsl` (415) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, neon ; defines VIEW_OWNS_SHAPE | - | uCamFocal, uResolution, uSteps, uTime | BASS_TIME, CHORD_HUE, CUT_COUNT, MID_TIME, SNARE_ENV ; sndShaped | k0,k1,k2,k3,k4,k5 via neonKnob | native: core/viz/SceneRegistry.cpp:31 builds `glow_vortex_frag.glsl` from the id (core/viz/Catalogue.cpp:37) |
| `grid_frag.glsl` (249) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 ; defines VIEW_OWNS_SHAPE | - | uCamFocal, uKnob, uMorph, uResolution, uSoundTimeSped, uSteps, uTime, uTurbulence, uWarp | ENERGY_TREND, KICK_ENV ; sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `grid_frag.glsl` from the id (core/viz/Catalogue.cpp:18) |
| `gyroid_cavern_frag.glsl` (251) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 | - | uCamFocal, uResolution, uSteps, uTime | CUT_COUNT, HAT_ENV, HELD_HARD_CUT, HELD_NOTE_CHANGE, HELD_SURGE, HIT_TIME, INTENSITY, KICK_ENV, MID_HIGH_ATT, ONSET_ENV, SNARE_ENV, TREBLE_ATT ; sndLevel, sndSpectrum | - | native: core/viz/SceneRegistry.cpp:31 builds `gyroid_cavern_frag.glsl` from the id (core/viz/Catalogue.cpp:31) |
| `hex_hive_frag.glsl` (275) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, neon ; defines VIEW_OWNS_SHAPE | - | uSteps, uTime | CHORD_HUE, CUT_COUNT, MID_TIME, SNARE_ENV ; sndShaped, sndSpectrum | k0,k1,k2,k3,k4,k5 via neonKnob | native: core/viz/SceneRegistry.cpp:31 builds `hex_hive_frag.glsl` from the id (core/viz/Catalogue.cpp:37) |
| `hexgrid_frag.glsl` (279) | style fragment | loose | scene_uniforms, scene_motion, camera, palette, scene_grade, sdf3 ; defines VIEW_OWNS_SHAPE | - | uKnob, uMorph, uSteps, uTime, uTurbulence, uWarp | ENERGY_TREND, HELD_NOTE_CHANGE, HIGH, KICK_ENV, VOL_ATT ; sndLevel, sndShaped, sndSpectrum | k0,k1,k2,k3,k4,k5 | native: core/viz/SceneRegistry.cpp:31 builds `hexgrid_frag.glsl` from the id (core/viz/Catalogue.cpp:19) |
| `hollow_bone_frag.glsl` (44) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_bone_frag.glsl` from the id (core/viz/Catalogue.cpp:153) |
| `hollow_cave_frag.glsl` (44) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_cave_frag.glsl` from the id (core/viz/Catalogue.cpp:152) |
| `hollow_coral_frag.glsl` (45) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_coral_frag.glsl` from the id (core/viz/Catalogue.cpp:152) |
| `hollow_hive_frag.glsl` (42) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_hive_frag.glsl` from the id (core/viz/Catalogue.cpp:153) |
| `hollow_neuron_frag.glsl` (46) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_neuron_frag.glsl` from the id (core/viz/Catalogue.cpp:153) |
| `hollow_ribs_frag.glsl` (47) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_ribs_frag.glsl` from the id (core/viz/Catalogue.cpp:152) |
| `hollow_roots_frag.glsl` (50) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_roots_frag.glsl` from the id (core/viz/Catalogue.cpp:152) |
| `hollow_shell_frag.glsl` (46) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_shell_frag.glsl` from the id (core/viz/Catalogue.cpp:153) |
| `hollow_sinew_frag.glsl` (47) | style fragment | hollow | scene_uniforms, scene_motion, camera, palette, scene_grade, neon, hollow, hollow_march ; defines VIEW_OWNS_SHAPE | - | - | - | - | native: core/viz/SceneRegistry.cpp:31 builds `hollow_sinew_frag.glsl` from the id (core/viz/Catalogue.cpp:152) |

### 2. Who loads each file

Method: for every one of the 150 names, a fixed-string search of `core/`, `app/src/main/cpp/`, `app/src/main/java/`, `engine/`, `tools/`, `app/src/test`, `app/src/androidTest`, `CMakeLists.txt` and `app/build.gradle.kts` for the file name, then for the quoted stem, then (for `*_frag.glsl`) for the quoted style id, because `core/viz/SceneRegistry.cpp:31` builds style file names at run time from `id + "_frag.glsl"`.

**Native is the only code that reads, assembles and compiles these files, with one exception (`fluid_splat_frag.glsl`, below).**

Native loaders and assemblers (the one-owner candidates already in `core/`):

| What | Where |
|---|---|
| Asset read, `//#include` resolution, allow-list | `core/viz/ShaderSource.cpp:48-118`, `core/viz/ShaderIncludes.inc` |
| Style fragments (109 files here): name built from the id | `core/viz/SceneRegistry.cpp:28-36`; ids at `core/viz/Catalogue.cpp:18-37` (loose styles), `:104-153` (family id lists), `:155-165` (family table with prefix and lib name) |
| User shader override replacing a style's source | `core/viz/scenes/ShaderScene.cpp:52` (`setFragmentSource`), `core/viz/RendererFrame.cpp:92`, `core/viz/Renderer.cpp:326-330` |
| `acid_step_frag`, `acid_show_frag` | `core/viz/scenes/AcidScene.cpp:58`, `:64` |
| `composite_frag` and its per-transition variants (`#define MV_TRANSITION 1` inserted after `#version`, the transition source substituted for `// __GL_TRANSITION_SOURCE__`) | `core/viz/Transition.cpp:12`, `:53`, `:77`; described at `core/viz/CompositePass.hpp:15-22` |
| `curl_field_frag` + `fluid_base_vert` | `core/viz/scenes/CurlFlowScene.cpp:23` |
| `cymatics_field_frag` | `core/viz/scenes/CymaticsScene.cpp:19` |
| `dive_frag` (Endless zoom pass, not a `dive_` family style) | `core/viz/DivePass.cpp:9` |
| `edge_glow_frag` | `core/viz/frame/EdgeGlowPass.cpp:8` |
| `export_watermark_vert/frag`, `export_seam_frag` | `core/viz/frame/ExportPasses.cpp:48`, `:117` |
| `fade_frag` | `core/viz/TrailPass.cpp:13` |
| `fade_vert` (the fullscreen triangle most passes share) | `core/viz/Renderer.cpp:405`, `core/viz/scenes/MilkdropScene.cpp:117` |
| Fluid solver passes (`fluid_splat`, `splat_batch`, `advect`, `maccormack`, `curl`, `vorticity`, `divergence`, `mg_smooth`, `mg_restrict`, `mg_prolong`, `gradient`, `clear`, `copy`, `display`) | `core/viz/fluid/FluidSim.cpp:15-28`, vertex at `:44` |
| Fluid look passes (`bloom_prefilter`, `bloom_blur`, `bloom_final`, `sunrays_mask`, `sunrays`, `blur`, `display`) and the `display` keyword variants (`#define LOOK n`, `SHADING`, `BLOOM`, `SUNRAYS` inserted after `#version`) | `core/viz/fluid/FluidLook.cpp:22-28`, `:210-215` |
| Fluid particle passes | `core/viz/fluid/FluidParticles.cpp:29-32`, `:45` |
| User fluid injection shaders (compiled in place of `fluid_splat_frag`) | `core/viz/fluid/FluidSim.cpp:182-200`, `core/viz/Renderer.cpp:140-144`, `core/api/geode_viz_api.cpp:264` |
| `blue_noise_64.bin` | `core/viz/BlueNoise.cpp:6`, `core/viz/BlueNoise.hpp:11` |
| `cyclic_palettes.bin` | `core/viz/Renderer.cpp:423` (also described at `core/viz/PaletteTable.inc:30`) |

**Kotlin sites that read a shader asset, build shader source, or know a shader file name (the port targets):**

| Site | What it does | Class |
|---|---|---|
| `engine/scenes/src/main/kotlin/dev/geode/render/fluid/FluidShaderTemplate.kt:6-14` | Opens `shaders/fluid_splat_frag.glsl` with `context.assets.open` and returns its text. The only Kotlin read of a shader asset. | engine asset access in Kotlin |
| `app/src/main/java/dev/geode/ui/CustomizeAdvanced.kt:594-600` (caller, `FluidShaderTemplate.splat` at `:597`) | Reads that template on `Dispatchers.IO`, seeds two text editors with it, and hands the edited force and dye sources back through `visualizerRenderer.submitFluidInjectionShaders` (`:577-578`). | shell editor around a Kotlin asset read |
| `engine/scenes/src/main/kotlin/dev/geode/render/VisualizerRenderer.kt:158-163` and `engine/scenes/src/main/kotlin/dev/geode/render/bridge/NativeViz.kt:152` | Pass the two source strings to native (`core/api/geode_viz_api.cpp:264`). | shell pass-through |
| `engine/scenes/src/main/kotlin/dev/geode/render/VisualizerRenderer.kt:143-149` | `GeodeNative.shaderOverrideSet(sceneId, fragmentSrc)` and `shaderOverride(sceneId)`: the user's fragment source for a style, stored natively. | shell pass-through |
| `engine/scenes/src/main/kotlin/dev/geode/render/scene/SceneCapabilities.kt:27-30` | Builds `Map<styleId, "shaders/${id}_frag.glsl">` for every SHADER style: a second copy of the naming rule at `core/viz/SceneRegistry.cpp:31`. No production code reads the map's values (file names): `app/src/main/java/dev/geode/ui/AutoVisualsController.kt:257` and `app/src/main/java/dev/geode/pro/FreeTier.kt:45` use only `.keys`; only `engine/scenes/src/test/kotlin/dev/geode/render/scene/StyleFamiliesTest.kt:48-49` reads a value. | duplicate of a native rule |
| `engine/scenes/src/main/kotlin/dev/geode/render/scene/StyleFamilies.kt:19-22` | `StyleFamily.library` carries the `lib_<family>` include name (filled from native `Catalogue.families`); used by `StyleFamiliesTest.kt:60`. | data from native |
| `engine/scenes/src/main/kotlin/dev/geode/render/BlendMode.kt` | Enum whose ordinals are `composite_frag.glsl`'s `uBlendMode` switch (`composite_frag.glsl:28-29,337-361`); native has the same enum at `core/viz/VisualSafety.hpp:10`. | duplicate ordinal table |
| `engine/scenes/src/main/kotlin/dev/geode/render/UnderlayBlend.kt:7-17` | Enum whose ordinals are `uUnderlayBlend` (`composite_frag.glsl:119,425-431`). | duplicate ordinal table |
| `engine/scenes/src/main/kotlin/dev/geode/render/TransitionStyle.kt` | Enum CUT, FADE, MELT, SLIDE, ZOOM whose ordinals are `composite_frag.glsl`'s `uStyle` 0..4 (`:390-414`); native copy at `core/viz/VisualSafety.hpp:9`. | duplicate ordinal table |
| `engine/scenes/src/main/kotlin/dev/geode/render/TouchField.kt:11-12` | `MAX_POINTS = 5`, documented as mirroring `uTouchPoints` (`lib_scene_uniforms.glsl:101`); native has `kMaxPoints = 5` at `core/viz/TouchField.hpp:10`. | duplicate constant |

No Kotlin file compiles GLSL: a search of `app/src/main` and `engine` for `glCreateShader`, `glShaderSource`, `glCompileShader`, `glLinkProgram`, `#version 300`, `MV_TRANSITION` and `__GL_TRANSITION_SOURCE__` in `.kt`/`.java` returned nothing.

**Unreferenced files: none.** Every one of the 150 names is reached: 39 by a literal file name in `core/`, 109 by a style id present in `core/viz/Catalogue.cpp` (which `SceneRegistry.cpp:31` turns into the file name), 2 binaries by literal name. Not checked: whether every catalogue id is reachable from the UI (that is the catalogue scanner's question).

### 3. The uniform contract

#### 3a. The block every fragment style shares (set by `ShaderScene` per draw)

| Group | Uniforms | Declared | Native sets it |
|---|---|---|---|
| Clock, frame | `uTime`, `uResolution` | `lib_scene_uniforms.glsl:32-33` | `core/viz/scenes/ShaderScene.cpp:165`, `:138`. `uTime` is the Speed-integrated clock wrapped at `kTimeWrapSeconds = 7100` (`core/viz/scenes/ShaderScene.hpp:48`, `ShaderScene.cpp:86`). |
| View params | `uZoom`, `uRotation`, `uMirrorX`, `uSymmetry`, `uKaleido`, `uMorph`, `uPixelate`, `uSway`, `uDriftX`, `uDriftY`, `uTile`, `uTwist`, `uTurbulence`, `uWarp`, `uRipple` | `lib_scene_uniforms.glsl:34-35,42-43,53-64` | `ShaderScene.cpp:166-167,175-176,197-208` |
| Grade params | `uColorShift`, `uHueRange`, `uSat`, `uBright`, `uInvert`, `uIntensity`, `uContrast`, `uGamma`, `uDuotone`, `uBloom`, `uPosterize`, `uTemperature`, `uSolarize` | `lib_scene_uniforms.glsl:36-41,46-47,51-52,59,65-66` | `ShaderScene.cpp:169-174,195-196,203,209-212` |
| Palette | `uPalBase`, `uPalRange`, `uPal2Base`, `uPal2Range`, `uPaletteMix` | `lib_scene_uniforms.glsl:44-45,48-50` | `ShaderScene.cpp:177-181` |
| Palette LUT and stops | `uPalLut` (sampler, unit 2), `uPalLutMix`, `uPalLutRow`, `uPalStops[5]`, `uPalStopCount` | `lib_palette.glsl:30-41` | `ShaderScene.cpp:144,147-148,192-193`; `kMaxStops = 5` at `core/viz/Colour.hpp:17` |
| Style knobs | `uKnob[6]` | `lib_scene_uniforms.glsl:86` | `ShaderScene.cpp:223` |
| March budget | `uSteps` | `lib_scene_uniforms.glsl:124` | `ShaderScene.cpp:150`; 64..128 from Detail (`ShaderScene.cpp:20-26,395-402`), times the host's march scale, floor 24 |
| Touch | `uTouchAnchor`, `uTouchPoints[5]`, `uTouchCount`, `uTouchGesture`, `uTouchAxis`, `uTouchSpin` | `lib_scene_uniforms.glsl:101-114` | `ShaderScene.cpp:268-274`; the simulation scenes use `uploadSceneTouch` at `core/viz/scenes/SceneCommon.hpp:64-69` |
| Sound frame | `uSound[29]`, `uSoundTex` (R32F 128x4, unit 3), `uSoundDrive`, `uSoundEvents`, `uSoundCounts` | `lib_sound.glsl:17-28` | `core/viz/SoundUniforms.cpp:43,51,53,55,57`. `uSound[29]` = `GEODE_SOUND_SCALARS` 113 rounded up to vec4s (`core/sound/SoundFrame.h:26`, `core/viz/SoundUniforms.hpp:32`). The `SND_*` names are `#define`s onto `uSound[i].xyzw` (`lib_sound.glsl:30-142`), i.e. the field order of `GeodeSoundFrame` written a second time in GLSL. |
| Motion field | `uEnergyRel`, `uBassRel`, `uMidRel`, `uTrebRel`, `uMotionBright`, `uHarmony`, `uKeyHue`, `uKeyStrength`, `uBarOsc`, `uOrbit`, `uDrift`, `uBreath`, `uFlowPhase`, `uFlowTime`, `uSoundTimeSped` | `lib_scene_motion.glsl:43-96` | `ShaderScene.cpp:235-249` |
| Legacy motion (held constant) | `uMoveDir`, `uSpawnSeed`, `uSpawnAge`, `uFormPhase` | `lib_scene_motion.glsl:109-118` | `ShaderScene.cpp:254-257`: fixed at 0, 1000, 0 and (1, 0) |
| Camera rig | `uCamPos`, `uCamBasis`, `uCamFocal`, `uCamTravel`, `uCamDolly` | `lib_camera.glsl:17-25` | `ShaderScene.cpp:281-285` (only when a camera pose is set) |

Styles outside this range add `uWorld[24]`, `uWorldShapes`, `uWorldTex` (`lib_world.glsl:70,185-186`) and the pool uniforms; no file in this range reads them.

#### 3b. Pass-private uniforms (each pass declares its own; native file that sets them)

| Pass | Uniforms | Native |
|---|---|---|
| `acid_step_frag.glsl:33-64,77-79` | `uPrev`, `uRes`, `uStyle`, `uSource`, `uZoom`, `uRotate`, `uHueShift`, `uFeedback`, `uModulate`, `uClock`, `uBass`, `uMid`, `uTreble`, `uStrike`, `uDrive`, `uSpokes[12]`, `uBaseHue`, `uHueSpan`, `uLiquid`, `uCentre`, `uTwist`, `uDrift`, `uWobble`, `uMelody`, `uTouchPoints[5]`, `uTouchCount` | `core/viz/scenes/AcidScene.cpp:228-253` |
| `acid_show_frag.glsl:22-32` (+ `lib_sound`, `lib_relief`) | `uState`, `uRes`, `uScanline`, `uCurve`, `uSat`, `uFloorHue`, `uOverdrive`, `uHit`, `uDepth`, `uLookBase`, `uLookAlt` | `AcidScene.cpp:229,265-275` |
| `composite_frag.glsl:12-120` | `uTexA`, `uTexB`, `uNoise`, `uDither`, `uRatio`, `uProgress`, `uStyle`, `uLayerMix`, `uBlendMode`, `uTime`, `uChroma`, `uVignette`, `uScanline`, `uGrain`, `uFisheye`, `uGateA`, `uGateB`, `uCamFlat`, `uCamTilt`, `uCamGate`, 23 `uPost*`, `uFlow`, `uFlowStrength`, `uUnderlay`, `uUnderlayBlend`, `uUnderlayAmount` | `core/viz/CompositePass.cpp:89-165` |
| `curl_field_frag.glsl:28-32` | `uTime`, `uFreq`, `uDetail`, `uAmp`, `uPeriod` | `core/viz/scenes/CurlFlowScene.cpp:102-106` |
| `cymatics_field_frag.glsl:50-100` | `uResolution`, `uTime`, `uStyle`, `uModes[8]`, `uModeCount`, `uGeometry`, `uScale`, `uHeightNorm`, `uLine`, `uGlow`, `uIridescence`, `uCaustic`, `uFill`, `uSwirlPhase`, `uTravelPhase`, `uDriftShift`, `uFieldLive`, `uBaseHue`, `uHueSpan`, `uEnergy`, `uTreble`, `uExposure`, `uTouchPoints[5]`, `uTouchCount`, `uTouchK`, `uTouchPhase` | `core/viz/scenes/CymaticsScene.cpp:79-104` |
| `dive_frag.glsl:18-31` | `uScene`, `uTexel`, `uAspect`, `uPhase`, `uMix`, `uInner`, `uBase`, `uStep`, `uTwist`, `uCentre`, `uShape`, `uBlend`, `uFog`, `uLens` | `core/viz/DivePass.cpp:34-48` |
| `edge_glow_frag.glsl:13-16` | `uWindow`, `uWidth`, `uRadius` | `core/viz/frame/EdgeGlowPass.cpp:50-52` |
| `export_watermark_vert/frag`, `export_seam_frag` | `uTiles`, `uOrigin`, `uTexture`, `uClip`, `uDim`, `uAlpha`, `uTex` | `core/viz/frame/ExportPasses.cpp:91-96,184` |
| `fade_frag.glsl:7` | `uFadeAlpha` | `core/viz/TrailPass.cpp:81` |
| fluid solver (`fluid_*`) | `uVelocity`, `uSource`, `uForward`, `uSrcInvRes`, `uVelInvRes`, `uDt`, `uRdx`, `uHalfRdx`, `uDecay`, `uInvRes`, `uAspect`, `uTexture`, `uValue`, `uPressure`, `uCurl`, `uCurlStrength`, `uDx`, `uTemperature`, `uBuoyancy`, `uMaxHeat`, `uP`, `uE`, `uRhs`, `uH2`, `uOmega`, `uTarget`, `uPrev`, `uCur`, `uRadius`, `uMode`, `uCeiling`, `uSeg[16]`, `uVal[16]`, `uCount`, `uHeat` | `core/viz/fluid/FluidSim.cpp:266-294,343-358,387-414,452-461,484-486` |
| fluid look (`fluid_display`, bloom, sunrays, blur) | `uDye`, `uBloom`, `uSunrays`, `uDither`, `uDitherScale`, `uTexelSize`, `uTime`, `uAudio`, `uLookHue`, `uTemperature`, `uIntensity`, `uCurve`, `uThreshold`, `uWeight`, `uDirection` | `core/viz/fluid/FluidLook.cpp:105-106,129,145,149,168-177`, `FluidSim.cpp:496,514,518` |
| fluid particles | `uState`, `uMeta`, `uVelocityField`, `uAspect`, `uPointScale`, `uViewport`, `uStretchSeconds`, `uMaxStretch`, `uDt`, `uDrag`, `uFlowScale`, `uTime`, `uLife`, `uSpawns[8]`, `uSpawnCount`, `uCatches[4]`, `uCatchCount`, `uStir`, `uStirFreq`, `uStirTime`, `uHueBase`, `uHueSpan`, `uBrightness`, `uShape`, `uGlow` | `core/viz/fluid/FluidParticles.cpp:101-131,153-165` |

#### 3c. Mismatches

Checked two ways: every uniform declared by a file in this range against the quoted `"uName"` strings in `core/` and `app/src/main/cpp/`; and, for each pass, each declared name against the native file that loads the pass.

- **Declared in a shader of this range and never set by native: none.** Every declared name has a matching string in the native file that owns the program (`acid_step` and `cymatics_field` get `uTouchPoints`/`uTouchCount` through `uploadSceneTouch`, `core/viz/scenes/AcidScene.cpp:253`, `CymaticsScene.cpp:104`).
- **Set by native, declared by no shader asset at all:** `uBeat` (`core/viz/fluid/FluidSim.cpp:501`). It is uploaded only to a user-written injection program, together with `uDt`, `uDx`, `uTime`, `uBass`, `uMid`, `uTreble` and `uEnergy` (`core/viz/fluid/FluidSim.cpp:494-501`). The template the editor shows, `fluid_splat_frag.glsl:12-18`, declares none of those eight, so this part of the injection-shader contract exists only in C++. `uMax`, `uSimSize`, `uSimState` are also not in any asset, because their shaders are GLSL strings inside C++ (`core/viz/frame/CastTee.cpp:260-271`, `core/viz/compute/SimGlsl.hpp:20-21`).
- **Set by native, declared only by shaders outside this range:** the pool, world, Life, Silk, Myco and liquid-feedback names (for example `uWorld`, `uPoolWaves`, `uSensorAngle`, `uZoomExp`); 169 names in total, of which the shared-block names above are the ones files in this range read through the libs. Not a mismatch for this range; listed so the second shader scanner can close it.
- **Set to a constant, still read by a style here:** `uFormPhase`, `uSpawnSeed`, `uMoveDir` (constants at `core/viz/scenes/ShaderScene.cpp:254-257`) are read by `curl_bloom_frag.glsl:140` (`gMorph = uFormPhase`), `:148` (`uMoveDir.y`), `:193` (`uSpawnSeed`) and `:195` (`spawnGrow`, which reads `uSpawnAge` in the lib). See Surprises.
- **Uploaded but, by the native comment, read by no bundled style:** `uColorShift` (`ShaderScene.cpp:168-169`). No file in this range names it; `uHueRange`, `uBright`, `uSat` etc. are read only inside `lib_palette`/`lib_scene_grade`.
- **Same name, different meaning in different programs.** `uZoom`, `uTwist`, `uDrift`, `uTime`, `uStyle`, `uSat`, `uGlow`, `uShape`, `uIntensity`, `uBloom`, `uTemperature`, `uCentre`, `uPrev`, `uSource`, `uMode`, `uDither` each name unrelated things: for example `uTwist` is the Twist param in a style (`lib_scene_uniforms.glsl:64`), a per-frame vortex in Acid (`acid_step_frag.glsl:58`), and radians per octave in Endless zoom (`dive_frag.glsl:26`); `uDrift` is a float accumulated angle (`lib_scene_motion.glsl:70`) and a `vec2` resample offset (`acid_step_frag.glsl:60`); `uTemperature` is the colour-temperature param (`lib_scene_uniforms.glsl:65`) and the fluid heat sampler (`fluid_vorticity_frag.glsl:21`, `fluid_display_frag.glsl:56`); `uBloom` is a float param (`lib_scene_uniforms.glsl:52`) and a sampler (`fluid_display_frag.glsl:48`); `uDither` is a float in `composite_frag.glsl:17` and a sampler in `fluid_display_frag.glsl:50`. A single native owner with one name table has to keep these per program.

### 4. Logic in shaders that duplicates code elsewhere

| GLSL (path:line) | What is written as a literal | The other copy |
|---|---|---|
| `lib_sound.glsl:17,30-142` (reached by every style here) | The `GeodeSoundFrame` scalar order as 113 `#define SND_x uSound[i].c` lines and the array size 29 | `core/sound/SoundFrame.h:26` and the struct it sizes; `core/viz/SoundUniforms.hpp:32` |
| `acid_step_frag.glsl:75`, `cymatics_field_frag.glsl:92`, `lib_scene_uniforms.glsl:101` | `TOUCH_MAX_POINTS 5`, three times in GLSL | `core/viz/TouchField.hpp:10` (`kMaxPoints`), `engine/scenes/src/main/kotlin/dev/geode/render/TouchField.kt:12` (`MAX_POINTS`) |
| `composite_frag.glsl:337-361` | Blend function per `uBlendMode` 0..7 | `core/viz/VisualSafety.hpp:10`, `engine/scenes/src/main/kotlin/dev/geode/render/BlendMode.kt` |
| `composite_frag.glsl:376-414` | Transition per `uStyle` (0 cut, 1 fade, 2 melt, 3 slide, 4 zoom, 5 library, 6 Layers) | `core/viz/VisualSafety.hpp:9`, `engine/scenes/src/main/kotlin/dev/geode/render/TransitionStyle.kt` |
| `composite_frag.glsl:425-431` | Underlay blend 0 screen, 1 multiply, 2 add | `core/viz/CompositePass.cpp:118-120`, `engine/scenes/src/main/kotlin/dev/geode/render/UnderlayBlend.kt:13-17` |
| `composite_frag.glsl:130-136,261-269` | Hue rotate, luma weights 0.299/0.587/0.114, zoom floor 0.05, gamma floor 0.05; the comment says it is mirrored by `CompositeGrade` | `core/viz/CompositeGrade.hpp:16-18` (`kMinZoom`, `kMinGamma`, `kTau`), `core/viz/CompositeGrade.cpp`; the same grade chain is also in `lib_scene_grade.glsl` and `pm_post_frag.glsl` (both outside this range) |
| `composite_frag.glsl:160-216` | The view chain (rotation+sway `0.35*sin(t*0.7)`, kaleido fold, twist, warp, ripple, drift, tile, pixelate) | The same chain with different constants in `lib_scene_uniforms.glsl:126-208` (warp 0.06*sin(8y) here against 0.5*sin(3y) there; ripple 0.12/16 against 0.15/14; pixelate 512..40 cells against 24..288; drift 0.1 against 0.025 with a triangle wave). Two implementations of "the same slider". |
| `dive_frag.glsl:33-88` | Copy weights, band `1 - exp2(-uStep)`, fade floor 0.004, spread cap 12 | `core/viz/EndlessZoom.hpp:125-238` (`endless::tunnel`; the header says "change both together") |
| `edge_glow_frag.glsl:18-25` | Rounded-rectangle distance and smoothstep band | `core/viz/frame/EdgeGlowMath.hpp:145` (`blackAt`) |
| `cymatics_field_frag.glsl:121-125,171-189` | `hexLattice`, `besselApprox` | `core/viz/scenes/CymaticsMath.cpp` (same names) |
| `cymatics_field_frag.glsl:132-163,305-361,453-537` | The eleven Cymatics substyles as `uStyle == n` branches | `CymaticsStyle` at `core/viz/scenes/StyleCatalog.hpp:8`; what substyle number n looks like exists only as these GLSL branches |
| `acid_step_frag.glsl:213-232,262-279,309` and `:119-149` | Acid's ten warp recipes (`uStyle` 0..9) and four source layers (`uSource` 0..3) | Recipe numbers uploaded at `core/viz/scenes/AcidScene.cpp:230-231` from `AcidStyle` (`core/viz/scenes/StyleCatalog.hpp:67-70`) |
| `fluid_display_frag.glsl:152-239` | The nine fluid looks as `#if LOOK == n` (0..8) | `kLookCount = 9` at `core/viz/fluid/FluidLook.hpp:24`; each fluid style's `look` at `core/viz/scenes/StyleCatalog.hpp:119-121` |
| `fluid_splat_batch_frag.glsl:35` | `CUTOFF = 11.5` | `core/viz/fluid/FluidMath.hpp:37` (`kSplatCutoffRadii`) |
| `fluid_splat_batch_frag.glsl:26-27,52` | Array size and loop bound 16 | `core/viz/fluid/FluidMath.hpp:30` (`kSplatBatch`) |
| `fluid_splat_frag.glsl:33-34`, `fluid_splat_batch_frag.glsl:46` | Falloff `exp(-d/r) * (1 - 0.6 fp)` | `FluidMath` `splatFalloff` (named in `fluid_splat_batch_frag.glsl:37`) |
| `fluid_maccormack_frag.glsl:62` | The MacCormack corrector line | `FluidMath::macCormack` (named at `fluid_maccormack_frag.glsl:16`) |
| `fluid_particle_update_frag.glsl:127-133` | Drag `1 - pow(1 - uDrag, dt*60)` and the RK2 midpoint step | `FluidMath` `tracerStepRk2` (named at `:14`) |
| `fluid_gradient_frag.glsl:32` | Terminal speed 12 sim units/s | Not found as a named constant in `core/viz/fluid/FluidMath.hpp` (searched `12.0f`, `kTerminal`); GLSL-only |
| `fluid_display_frag.glsl:56` comment, `fluid_vorticity_frag.glsl:41` | Heat clamp `uMaxHeat` | `core/viz/fluid/FluidMath.hpp:133` (`kMaxTemperature = 2.0`), uploaded, not duplicated |
| `lib_camera.glsl:28` and every flown style | Rig travel wrap 8192 and path period 1024, baked into each style's z period (`BO_ZWRAP 256` `arbor_bolt_frag.glsl:88`, `RT_ZWRAP 1024` `arbor_roots_frag.glsl:91`, `RS_PERIOD 16` `arbor_rosette_frag.glsl:91`, `AU_ZWRAP 64` `aurora_frag.glsl:69`, `BARS_ZWRAP 64` `bars_frag.glsl:102`, `BV_ZWRAP 32` `bead_vortex_frag.glsl:80`, `BB_ZWRAP 256` `blacklight_bloom_frag.glsl:66`, `CO_ZWRAP 256` `chroma_orb_frag.glsl:79`, `PF_ZWRAP 256` `chrys_fall_frag.glsl:87`); `aurora_ocean_frag.glsl:40-45` derives `AO_QK = 2pi/147.456` from `8192 * 0.15 * 0.12` | `core/viz/CameraRig.hpp:44-45` (`kPathPeriod = 1024`, `kTravelWrap = 8192`). Changing either native constant silently breaks the seam of every flown style. |
| `bead_vortex_frag.glsl:37` (comment), spin arrives through the rig | "The style catalogue spins it (rate 1.4)" | `core/viz/Catalogue.cpp:211-218` (`kSpinning`, `bead_vortex` 1.4) |
| `bars_frag.glsl:124-137` | Seven band regions with boundaries at x = 0.125, 0.25, 0.40625, 0.5625, 0.71875, 0.859375 (bands 8, 16, 26, 36, 46, 55 of 64), attributed to `viz/Reactions` | No such boundaries found in `core/viz/Reactions.hpp/.cpp` or `core/sound/SoundFrame.h` by search; unverified, possibly a table that no longer exists natively |
| `chrome_nebula_frag.glsl:26-27,88` | "an onset triggers to 0.4..1 (viz/Reactions)" and the literal `smoothstep(0.05, 0.4, onset)` | `lib_sound.glsl` names `core/sound Form::kEventFloor` for the 0.4 floor; the `viz/Reactions` attribution was not confirmed |
| Per-style march ceilings, e.g. `#define BO_MAX_STEPS 96` (`arbor_bolt_frag.glsl:84`), `GR_MAX_STEPS 120` (`arbor_grove_frag.glsl:74`), 128 in every `bulb_*` (`bulb_cavern_frag.glsl:52`), `CB_MAX_STEPS 128` (`curl_bloom_frag.glsl:58`); `curl_bloom_frag.glsl:142` hard-codes `(uSteps - 64.0) / 64.0`; `black_hole_frag.glsl:214` hard-codes `96.0 / max(uSteps, 24.0)`; `chrys_bloom_frag.glsl:262`, `chrys_corolla_frag.glsl:145`, `chrys_dahlia_frag.glsl:227` test `uSteps` against 48, 72, 64 | The budget's range | `core/viz/scenes/ShaderScene.cpp:20-26` (`kMarchFloorSteps 64`, `kMarchMaxSteps 128`, `kMarchMinScaledSteps 24`) |
| `chrys_glass_frag.glsl:327`, `chrys_kaleido_frag.glsl:251` | `clamp(floor(uSymmetry + 0.5), 3.0, 12.0)`: a narrower range for the Symmetry param | `core/viz/ParamTable.inc:99` (`symmetry`, Int, 0..16) |
| `glass_bubbles_frag.glsl:282`, `glass_gem_frag.glsl:138`, `glass_caustic_frag.glsl:351-352`, `glass_marble_frag.glsl:160-161`, `glass_prism_frag.glsl:520` | Quality tiers ("Saver / Balanced / High") decided in GLSL from `uSteps`: bounce counts, layer counts, sample counts at thresholds 0.3 and 0.6 of `glassQuality()` and `GLASS_TWO_BOUNCE_STEPS 64` (`lib_glass.glsl:106,150-156`) | The budget itself is native (`core/viz/scenes/ShaderScene.cpp:150,395-402`); the tier thresholds exist only in GLSL |
| `glass_orb_frag.glsl:94,138-141`, `grid_frag.glsl:76,171-174`, `bars_frag.glsl:84,162-163`, `hex_hive_frag.glsl:110-113`, `hexgrid_frag.glsl:142` | How the spectrum or EQ is laid out across geometry (12 pillars over the 16-band EQ row, 16 columns at `0.02 + 0.88 b`, 24 lanes, mirrored around a bore) | The texture rows are native (`core/viz/SoundUniforms.hpp:27-30`: 128 x 4, rows spectrum, waveform, EQ, chroma); the row coordinates `0.5/4, 1.5/4, 2.5/4, 3.5/4` are repeated in `lib_sound.glsl:157-165` |
| Envelope lengths quoted in headers: surge "2.5 s" (`bulb_cavern_frag.glsl:38`, `dive_helix_frag.glsl:25`, `chrys_corolla_frag.glsl:35`), hard cut "1.5 s" (`bulb_glow_frag.glsl:34`, `chrys_tunnel_frag.glsl:44`), onset "0.22 s" (`arbor_bolt_frag.glsl:50`, `bulb_prism_frag.glsl:41`) | Comments only | `core/viz/Reactions.cpp:56-57`: HardCut 2.5, Onset 0.22, Surge 2.0. The surge and hard-cut comments disagree with native. |
| Fixed colour tables: `black_hole_frag.glsl:105-109` (five neon stops), each `bulb_*` `tone()` (cosine-ramp coefficients, e.g. `bulb_classic_frag.glsl:49-51`), `fluid_display_frag.glsl:157-158,213-215,226` (paper, lava ramp, stone), `foam_ice_frag.glsl:37`, `dive_orbit_frag.glsl:47` | Palette literals owned by the shader | The user palette comes only through `pal()` (`lib_palette.glsl`); native palette tables are `core/viz/PaletteTable.inc`. These literals are style accents, not copies, but they are colour decisions made outside the palette. |
| `fluid_display_frag.glsl:65` | `wheel()`: "the same form the fragment styles' palette uses" | `lib_palette.glsl` cosine ramp; `core/viz/scenes/SceneCommon.hpp:36-52` has a CPU `hue::hsv` for the fluid emitters |
| `acid_show_frag.glsl:54-57`, `acid_step_frag.glsl:83-86`, `cymatics_field_frag.glsl:104-107` | `hsv2rgb`, three identical copies | `core/viz/scenes/SceneCommon.hpp:36-52` (`hue::hsv`) |
| `2 pi` as a literal under ten names: `BO_TAU`, `RT_TAU`, `SK_TAU`, `BARS_TAU`, `BV_TAU`, `BB_TAU`, `CO_TAU`, `CB_TAU`, `PF_TAU`, `TAU` (e.g. `arbor_bolt_frag.glsl:86`, `acid_step_frag.glsl:81`), plus `6.2831` truncated in `composite_frag.glsl:132`, `fluid_display_frag.glsl:65,72`, `arbor_canopy_frag.glsl:68` | | `core/viz/scenes/SceneCommon.hpp:78` (`kTwoPi`), `core/viz/CompositeGrade.hpp:18` (`kTau`); the libs define `ARB_TAU`, `BULB_TAU`, `CHRYS_TAU`, `FLAME_TAU`, `HOLLOW_TAU` |

### 5. Tempo rule check

Search of all 148 files for `bpm`, `tempo`, `120`, `per beat`, `per bar`, `bar` (case-insensitive), then each hit read.

- **No literal BPM and no tempo number in any file of this range.** No file reads `SND_BPM`, `SND_BEAT_PHASE`, `SND_BEAT`, `SND_BEAT_COUNT`, `SND_DOWNBEAT` or `SND_BAR_PHASE` (the extractor found none of those names in the 148 files).
- The hits for `120` are not tempos: march ceilings `GR_MAX_STEPS 120` (`arbor_grove_frag.glsl:74`) and `BH_MAX_STEPS 120` (`black_hole_frag.glsl:59`), a star-field scale `rd * 120.0` (`chrys_meadow_frag.glsl:212`), Gaussian widths (`glass_lens_frag.glsl:338`, `glass_prism_frag.glsl:514`), and "120 degrees" in comments (`cymatics_field_frag.glsl:116`, `glass_orb_frag.glsl:122`).
- One comment claims a tempo lock that the code does not have: `bulb_contour_frag.glsl:33-36` says the contour lines "flow outward over the body once per beat and stay locked to the tempo"; the code at `:93` is `sndPhase(SND_CURVED_TIME, 2.0)`, i.e. `fract(curvedTime / 2.0)` (`lib_sound.glsl:174`), a sound clock, not the beat phase. No tempo assumption; the comment is stale.
- Bar-rate motion is read only through `uBarOsc`/`uBreath` (`uBreath` at `foam_orb_frag.glsl:34` and `chrys_phyllo_frag.glsl:263`; `uBarOsc` at `chrys_kaleido_frag.glsl:273` and `chrys_lotus_frag.glsl:318`), which the native MotionField produces; the shaders hold no period for it.
- Fixed wall-clock periods that are not tempos but do decide when the picture changes: the `dive_` family's scene cycle "thirteen seconds each (DIVE_SCENE_SECONDS on the Speed clock)" and palette switch "every 16 onsets" (`dive_cycle_frag.glsl:16-28`, `dive_show_frag.glsl:16-20`); the constants live in `lib_dive.glsl` (outside this range). This is a scheduling decision made in GLSL.

### 6. Surprises

1. **One Kotlin class still reads a shader asset.** `FluidShaderTemplate.kt:6-14` opens `shaders/fluid_splat_frag.glsl` from Kotlin to seed the injection editors (`CustomizeAdvanced.kt:594-600`). Native already has the text (`core/viz/fluid/FluidSim.cpp:15`); there is no JNI call that returns it, which is why Kotlin reads the APK itself.
2. **`curl_bloom_frag.glsl` advertises a morph that cannot happen.** Its header (`:30-41`) describes the host body walking a ring of primitives on `uFormPhase`, re-seeded veining on `uSpawnSeed` and a camera bank on `uMoveDir`; native uploads those three as constants (`core/viz/scenes/ShaderScene.cpp:251-257`, "Legacy since wave three, held at their neutral constant until R08 deletes the readers"). The style is one of those readers (`curl_bloom_frag.glsl:140,148,193,195`), so the body stays on ring position 0.
3. **Audio drive is applied by hand, file by file.** `sndSpectrum`, `sndEq`, `sndLevel` and `sndShaped` multiply by `uSoundDrive` (`lib_sound.glsl:157-172`); the raw `SND_*` scalars do not (`core/viz/SoundUniforms.cpp:38`: "No drive, clamp or slew here"). Some styles multiply by hand (`arbor_bonsai_frag.glsl:89,91`, `arbor_canopy_frag.glsl:94-95`, `arbor_frost_frag.glsl:195-197`, `arbor_spire_frag.glsl:111-114`, `arbor_willow_frag.glsl:119-122`, `bead_vortex_frag.glsl:205-208`, `bulb_ink_frag.glsl:79-80`, `chrys_bloom_frag.glsl:185-187`), others in the same family do not (`arbor_bolt_frag.glsl:233-236`, `arbor_grove_frag.glsl:204-206`, `arbor_coral_frag.glsl:101-103`, `bulb_chrome_frag.glsl:103-105`, `chrys_corolla_frag.glsl:139-141`). The Audio drive slider therefore scales the kick in Arbor Bonsai and not in Arbor Bolt. Whether a hit reaction scales with drive is decided per file, by hand.
4. **Three knob conventions.** Offset from 0.5, clamped (`arbor_bolt_frag.glsl:224-229`, `chroma_orb_frag.glsl:102`, `bars_frag.glsl:122`); raw 0..1 used as a multiplier (`blacklight_bloom_frag.glsl:276-281`, `bead_vortex_frag.glsl:219,224,261-262,289,341`, `chrys_fall_frag.glsl:212-220`); mixed in one file (`chrys_dahlia_frag.glsl:178-182`: k0, k1, k4, k5 raw, k3 offset; `chrys_glass_frag.glsl:303-308` the same); and unclamped (`grid_frag.glsl:110,178-179,225,233-234`, `glass_caustic_frag.glsl:196-201`, `glass_prism_frag.glsl:340-345`), where a knob outside 0..1 passes straight into the geometry. The "off is exactly 0.5" contract (`lib_scene_uniforms.glsl:83-85`) holds for all three, but a native knob table cannot describe a knob's effect with one rule.
5. **Hit reactions are written in the shaders.** Each style's header assigns sounds to properties in code ("Kick is the POLYPS" `arbor_coral_frag.glsl:31`, "Snare RE-ANGLES THE FORKS" `arbor_canopy_frag.glsl:30`, "The KICK drives the knife" `bulb_slice_frag.glsl:37`, cap throw on `SND_KICK_TOGGLE` `bars_frag.glsl:243-247`). These read `SND_KICK_ENV`, `SND_SNARE_ENV`, `SND_HAT_ENV`, `SND_ONSET_ENV` directly, not a reaction-matrix output, so what moves on a kick in these 109 styles is fixed GLSL the user cannot see or edit. `acid_step_frag.glsl:156-157,226` records that such reactions were removed there for exactly this reason. This is the largest block of "engine decisions" in the range and it is not addressed by moving Kotlin to C++.
6. **Section-driven choices are hashed in GLSL.** A new section (`SND_CUT_COUNT`) picks a tree form (`arbor_bonsai_frag.glsl:57-63,94`), a crystal habit (`arbor_frost_frag.glsl:104-108,200`), a fractal power 3..12 (`bulb_glow_frag.glsl:66-68,89-90`), kaleidoscope arm counts (`blacklight_bloom_frag.glsl:267-268`, `black_hole_frag.glsl:199`), a material (`foam_show_frag.glsl:37-38`), a dome tessellation (`fractal_temple_frag.glsl:245-248,291-292`), a minimal surface out of four (`gyroid_cavern_frag.glsl:101-107,138`), petal counts 5..12 (`chrys_neon_frag.glsl:107-109,216-217`), a palette step (`chrys_tunnel_frag.glsl:200-202`), a foam seed (`fractal_tunnel_frag.glsl:211-213,229-230`), a coil count (`hex_hive_frag.glsl:125`) and an arm count (`glow_vortex_frag.glsl:252`); the `dive_` family cycles scenes on a 13-second timer (section 5). These are "which look is shown when" rules with no native or host-test copy.
7. **Comments name Kotlin files that no longer exist.** `composite_frag.glsl:14` (`BlueNoise.kt`), `:54-55` and `:299` (`VisualizerRenderer and FxCompositor ... CompositeGrade.gateFor()`; `FxCompositor` is gone, the gate is `core/viz/CompositeGrade.hpp:20`), `acid_step_frag.glsl:73` and `cymatics_field_frag.glsl:90` (`SceneTouch.kt`), `cymatics_field_frag.glsl:39` (`CymaticsClockSafetyTest`), `lib_scene_uniforms.glsl:90,117` (`dev.geode.render.TouchField is the model`, `MarchBudget.forDetail()`), `export_watermark_frag.glsl:4` and `export_seam_frag.glsl:4` ("the Kotlin overlay's maths", "LoopRender's SeamStash"). None of `BlueNoise.kt`, `SceneTouch.kt`, `FxCompositor`, `CymaticsClockSafetyTest`, `MarchBudget` is found in `app/src`, `engine` or `core`.
8. **Envelope lengths in headers disagree with native** (surge 2.5 s against 2.0; hard cut 1.5 s against 2.5; table in section 4).
9. **Shaders also live inside C++.** `core/viz/compute/SimGlsl.cpp`, `core/viz/GlProber.cpp` and `core/viz/frame/CastTee.cpp` contain `#version 300 es` sources as string literals, outside the asset loader and its include allow-list. A single owner for "everything that loads, assembles, compiles" has three assembly mechanisms to fold in: `ShaderSource` includes, `#define` insertion after `#version` (`Transition.cpp:53`, `FluidLook.cpp:210-215`), and marker substitution (`Transition.cpp:12`), plus these inline strings.
10. **`dive_frag.glsl` is not a `dive_` family member.** It is the Endless zoom pass (`core/viz/DivePass.cpp:9`); the eleven `dive_*_frag.glsl` styles and `dive_show`/`dive_cycle` are the family. A loader that maps prefix to family must special-case it (the family table's prefix is `dive_`, `core/viz/Catalogue.cpp:160`, and `dive_frag` would match a naive `startsWith`).
11. **`fluid_particle_frag.glsl:12-14` and `fluid_particle_streak_frag.glsl:7-9` paste `lib_particle_common` and `lib_particle_shade` before their own `precision highp float;`**, so those libs must carry their own precision statements; `fluid_particle_vert.glsl:14-15` does the same. Every other file sets precision before its includes.
12. **Constant prefixes and helper names collide across files.** `BV_` (`bulb_cavern_frag.glsl:52-56`, `bead_vortex_frag.glsl:75-95`), `BH_` (`bulb_shell_frag.glsl:58-67`, `black_hole_frag.glsl:59-62`), `CN_` (`arbor_canopy_frag.glsl:46-50`, `chrome_nebula_frag.glsl:30-35`), `CO_` (`chroma_orb_frag.glsl:72-86`, `chrys_corolla_frag.glsl:53-59`), `GR_` (`arbor_grove_frag.glsl:74-83`, `grid_frag.glsl:66-79`), `BB_` (`blacklight_bloom_frag.glsl:61-77`, `glass_bubbles_frag.glsl:88-106`), `CB_` (`curl_bloom_frag.glsl:58-66`, `glass_cubes_frag.glsl:50-52`); `knob()` is defined in `chroma_orb_frag.glsl:102` and `glass_orb_frag.glsl:105`, `neon()` in `black_hole_frag.glsl:103`, `glow_vortex_frag.glsl:125` and `hex_hive_frag.glsl:78`, and every marched style defines its own `map()` and `normalAt()`. Harmless while each file is its own program; a collision the moment sources are concatenated or shared.
13. **A NaN scrub policy is spread across passes.** `fluid_advect_frag.glsl:35,42-44`, `fluid_maccormack_frag.glsl:46,64`, `fluid_clear_frag.glsl:17`, `fluid_mg_smooth_frag.glsl:33`, `fluid_mg_prolong_frag.glsl:25` scrub; `fluid_particle_update_frag.glsl:121-124` says "this kernel has no NaN scrub" and avoids the divide instead.
14. **`fluid_splat_frag.glsl` is no longer the solver's hot path** (`fluid_splat_batch_frag.glsl:2-4`: it "stays the single-splat template the Customize injection editors start from"), yet `core/viz/fluid/FluidSim.cpp:15` still loads it and `:484-486` still sets its uniforms. Its main remaining consumer is the Kotlin editor in item 1.
15. **The march loop and its helpers are pasted, not shared.** The tetrahedron `normalAt` (identical four lines) appears in 47 files of this range (e.g. `arbor_bonsai_frag.glsl:78-82`, `bars_frag.glsl:200-204`, `glass_cubes_frag.glsl:95-99`), the star field `rd * 150.0` / `hashCell` block in `arbor_grove_frag.glsl:155-159`, `aurora_frag.glsl:120-123`, `fractal_temple_frag.glsl:256-259`, `crystal_canyon_frag.glsl:127-130`, `chrys_meadow_frag.glsl:212-216`, and the eye-z rebase `ro.z = mod(ro.z, <period>)` in every flown style. This is the same duplication `lib_scene_uniforms.glsl:4-15` records having removed once for `view()`.
16. **Spectrum-analyser styles read the signal directly, not through reactions** (`bars_frag.glsl:163`, `grid_frag.glsl:171-174`, `hexgrid_frag.glsl:142`, `hex_hive_frag.glsl:112`, `chrys_meadow_frag.glsl:114-120`, `glass_orb_frag.glsl:138-141`, `glass_lens_frag.glsl:281-282`); `hex_hive_frag.glsl:42-44` states the exemption in a comment ("that is drawing the spectrum itself, so it reads the signal directly"). The rule that separates "drawing the signal" from "a hit reaction" is not written anywhere native.
17. **Performance budgets are stated in comments as device estimates** (`glass_cubes_frag.glsl:46-48` "5-7 ms at 810p on an Adreno 618 class GPU", `glass_ice_frag.glsl:48-51`, `glass_knot_frag.glsl:49-52` "See the PR"); nothing in the range or in the native code read here measures them.

### Not verified

- Whether each catalogue id is reachable from the UI, and whether the family lib helpers (`lib_arbor`, `lib_bulb`, `lib_chrys`, `lib_dive`, `lib_flame`, `lib_foam`, `lib_glass`, `lib_hollow`, `lib_neon`, `lib_touch`, `lib_dmt`, `lib_relief`, `lib_particle_*`, `lib_psrdnoise2`) read further uniforms: those files sort after position 150 and belong to the second shader scan. Only their `uniform` lines and `lib_sound.glsl:143-174`, `lib_scene_uniforms.glsl` (all) and `lib_camera.glsl:16-28` were read here.
- The `bars_frag.glsl:124-137` attribution of its seven band regions to `viz/Reactions`.
- The two `.bin` files were not decoded; only their sizes and loaders were checked (`blue_noise_64.bin` 4,096 bytes = 64 x 64 one-byte texels by size; `cyclic_palettes.bin` 3,840 bytes = 5 rows x 256 x RGB, matching `core/viz/Renderer.cpp:422-424`).
- No shader was compiled; uniform matching is by name string, so a name set on the wrong program would not be caught.


---

## Shader assets B: `app/src/main/assets/shaders/` files 151..302 (`hollow_throat_frag.glsl` .. `world_textures.bin`) and the other assets

Scope read: 152 files = 150 `.glsl` (38,667 lines by newline count) + 2 `.bin` in this half (the other 2 `.bin` sort into the first half; all 4 are reported below). Tree at HEAD `57fdf4a` (the brief says f13229d; I did not change it).

**How it was read (state of verification).** Read line by line: `lib_scene_uniforms`, `lib_sound`, `lib_palette`, `lib_camera`, `lib_scene_grade`, `lib_scene_motion`, `lib_touch`, `lib_feedback`, `quad_vert`, `trail_warp_frag`, `ui_blur_frag`, `ui_blur_mask_frag`, `myco_deposit_vert/frag`, `myco_blur_frag`, `pm_post_frag`, `morph_world_frag`, `hollow_throat_frag`, and parts of `lib_dive`. The other ~130 files (about 35,000 lines) were NOT read line by line: they were scanned by script over the full text of every file (comments stripped) for every `//#include`, every `uniform` declaration, every `u[A-Z]*` identifier, every `uKnob[...]` index, every `SND_*` name, every `#define`, every function definition in the libs, and by grep for tempo words and the literal 120. The inventory columns come from that scan, so they are complete for names; what a style does with a name was not judged per file.

Kinds in this half: 103 style fragments, 32 library includes, 11 passes (sim step/show, liquid, trail, MilkDrop post), 2 vertex, 2 ui, 2 data tables. No export shader sorts into this half.

### 1. Inventory

Include marker: a whole line `//#include <name>` (regex in `core/viz/ShaderSource.cpp:26-44`). Column "includes" drops the `lib_` prefix and keeps file order. "reads" lists uniform identifiers the file's own text uses but does not declare (they come from the includes). `SND:` lists the sound-frame names the file reads (`lib_sound.glsl:30-154`). Knobs: `uKnob[0..5]` = PULSE, TEXTURE, FLOW, TONE, BUILD, SPACE (`lib_scene_uniforms.glsl:68-86`); `ki` means indexed in a loop or through a variable; "via neonKnob" means read through `lib_neon`'s `neonKnob()`.

| File | Lines | Kind | Family | Includes (in order) | Uniforms and sound names | Knobs |
|---|---|---|---|---|---|---|
| `hollow_throat_frag.glsl` | 43 | style fragment | hollow | scene_uniforms scene_motion camera palette scene_grade neon hollow hollow_march | reads: uTime | - |
| `jelly_abyss_frag.glsl` | 362 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uFlowPhase uKeyHue uKeyStrength uSteps uTime; SND: BASS_PRESENCE CUT_COUNT HAT_ENV HELD_HARD_CUT HELD_NOTE_CHANGE HELD_SURGE HIT_TIME KICK_ENV LOW MID_HIGH_ATT MID_PRESENCE ONSET_ENV SNARE_ENV SUB_ATT | - |
| `julia_frag.glsl` | 246 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade | reads: uKnob uMorph uSteps uTime uTurbulence uWarp; SND: BASS HELD_NOTE_CHANGE HIT_TIME INTENSITY KICK_ENV MIDDLE ONSET_ENV TREBLE_ATT | k0,1,2,3,4,5 |
| `kaleido_frag.glsl` | 314 | style fragment | loose | scene_uniforms camera palette scene_grade sdf3 | reads: uCamFocal uKnob uMorph uResolution uSteps uTime uTurbulence uWarp; SND: CUT_COUNT HAT_ENV HELD_HARD_CUT HELD_NOTE_CHANGE HELD_SURGE INTENSITY KICK_ENV LOW ONSET_COUNT | ki |
| `kifs_frag.glsl` | 810 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 touch dmt | reads: uKaleido uResolution uSteps uSymmetry uTime uTouchAxis uTouchSpin; SND: BASS_PRESENCE KICK_ENV LOW MID_PRESENCE TREBLE_PRESENCE | - |
| `lava_frag.glsl` | 285 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uKnob uMorph uSteps uTime uTurbulence uWarp; SND: BASS_ATT HELD_NOTE_CHANGE HELD_SURGE KICK_ENV MID TREBLE_ATT VOL | k0,1,2,3,4,5 |
| `lib_arbor.glsl` | 398 | library include | lib | - | reads: uDriftX uDriftY uKaleido uMirrorX uMorph uPixelate uResolution uRipple uRotation uSteps uSway uSymmetry uTile uTime uTurbulence uWarp uZoom; SND: LOW | - |
| `lib_bulb.glsl` | 317 | library include | lib | - | reads: uCamBasis uCamFocal uColorShift uIntensity uMorph uSteps uTime uTurbulence uTwist uWarp | - |
| `lib_camera.glsl` | 57 | library include | lib | - | declares: uCamPos uCamBasis uCamFocal uCamTravel uCamDolly | - |
| `lib_chrys.glsl` | 258 | library include | lib | - | - | - |
| `lib_dive.glsl` | 666 | library include | lib | - | reads: uCamDolly uCamTravel uFlowPhase uIntensity uMorph uResolution uSteps uTime uTurbulence uWarp; SND: CUT_COUNT HELD_HARD_CUT KICK_ENV KICK_TOGGLE MIDDLE ONSET_COUNT ONSET_ENV | - |
| `lib_dmt.glsl` | 557 | library include | lib | - | reads: uFlowPhase uFormPhase uMoveDir uSteps uTime; SND: INTENSITY | - |
| `lib_feedback.glsl` | 82 | library include | lib | - | declares: uZoom uZoomExp uRot uCx uCy uDx uDy uSx uSy uWarp uWarpScale uWarpSpeed | - |
| `lib_flame.glsl` | 167 | library include | lib | - | reads: uTime; SND: DRIVE LOW MIDDLE | - |
| `lib_flame_march.glsl` | 55 | library include | lib | - | reads: uSteps | - |
| `lib_foam.glsl` | 610 | library include | lib | - | reads: uCamDolly uCamFocal uCamTravel uHarmony uIntensity uKaleido uKeyHue uKeyStrength uMorph uMotionBright uResolution uSteps uSymmetry uTime uTurbulence uWarp; SND: BASS_TIME CURVED_TIME CUT_COUNT ENERGY_TREND FLUX HAT_ENV HELD_HARD_CUT HELD_SURGE HIGH HIT_TIME INTENSITY KICK_ENV LOW MID_TIME ONSET_ENV PITCH_CLASS PITCH_CONFIDENCE SNARE_ENV TREBLE_TIME | - |
| `lib_glass.glsl` | 973 | library include | lib | - | reads: uIntensity uMorph uSteps uTime uTurbulence uWarp; SND: BASS_ATT BASS_PRESENCE CENTROID CURVED_TIME ENERGY_TREND FLATNESS HAT_ENV KICK_ENV ONSET_COUNT ONSET_ENV PITCH_CONFIDENCE PITCH_HEIGHT SNARE_ENV STEREO_PAN STEREO_WIDTH | - |
| `lib_hollow.glsl` | 158 | library include | lib | - | reads: uTime; SND: DRIVE LOW MIDDLE | - |
| `lib_hollow_march.glsl` | 63 | library include | lib | - | reads: uSteps uTime | - |
| `lib_lucid.glsl` | 427 | library include | lib | - | reads: uDriftX uDriftY uKaleido uMirrorX uMorph uPixelate uResolution uRipple uRotation uSteps uSway uSymmetry uTile uTime uTurbulence uTwist uWarp uZoom; SND: BASS | - |
| `lib_nebula.glsl` | 539 | library include | lib | - | reads: uFlowTime uIntensity uKeyHue uKeyStrength uMorph uResolution uSteps uTime uTurbulence uWarp | - |
| `lib_neon.glsl` | 112 | library include | lib | - | reads: uIntensity uKeyHue uKeyStrength uKnob uMorph uPalLutMix uTurbulence uWarp; SND: BASS_ATT BUILD_UP HAT_ENV HELD_SURGE KICK_ENV PRESENCE SUB_ATT | ki |
| `lib_opal.glsl` | 139 | library include | lib | - | reads: uTime; SND: DRIVE LOW MIDDLE | - |
| `lib_opal_march.glsl` | 70 | library include | lib | - | reads: uSteps uTime | - |
| `lib_palette.glsl` | 76 | library include | lib | - | declares: uPalLut uPalLutMix uPalLutRow uPalStops[5] uPalStopCount; reads: uColorShift uHueRange uPal2Base uPal2Range uPalBase uPalRange uPaletteMix | - |
| `lib_particle_common.glsl` | 146 | library include | lib | - | - | - |
| `lib_particle_shade.glsl` | 94 | library include | lib | - | - | - |
| `lib_psrdnoise2.glsl` | 149 | library include | lib | - | - | - |
| `lib_relief.glsl` | 315 | library include | lib | - | SND: CURVED_TIME CUT_COUNT HELD_SURGE KICK_ENV MID_TIME PITCH_HEIGHT PRESENCE STEREO_PAN TREBLE_TIME | - |
| `lib_scene_grade.glsl` | 28 | library include | lib | - | reads: uBloom uBright uContrast uDuotone uGamma uIntensity uInvert uPosterize uSat uSolarize uTemperature | - |
| `lib_scene_motion.glsl` | 274 | library include | lib | - | declares: uEnergyRel uBassRel uMidRel uTrebRel uMotionBright uHarmony uKeyHue uKeyStrength uBarOsc uOrbit uDrift uBreath uFlowPhase uFlowTime uSoundTimeSped uMoveDir uSpawnSeed uSpawnAge uFormPhase; SND: INTENSITY TREBLE_PRESENCE | - |
| `lib_scene_uniforms.glsl` | 209 | library include | lib | sound | declares: uTime uResolution uZoom uRotation uColorShift uHueRange uSat uBright uInvert uIntensity uMirrorX uTurbulence uPalBase uPalRange uContrast uGamma uPal2Base uPal2Range uPaletteMix uDuotone uBloom uWarp uRipple uSymmetry uKaleido uMorph uPixelate uPosterize uSway uDriftX uDriftY uTile uTwist uTemperature uSolarize uKnob[6] uTouchAnchor uTouchPoints[TOUCH_MAX_POINTS] uTouchCount uTouchGesture uTouchAxis uTouchSpin uSteps; SND: LOW | k6 |
| `lib_sdf3.glsl` | 401 | library include | lib | - | - | - |
| `lib_sound.glsl` | 174 | library include | lib | - | declares: uSound[29] uSoundTex uSoundDrive uSoundEvents uSoundCounts | - |
| `lib_terra.glsl` | 327 | library include | lib | - | reads: uIntensity uMorph uSteps uTime uTurbulence uTwist uWarp | - |
| `lib_touch.glsl` | 166 | library include | lib | - | reads: uResolution uTouchAnchor uTouchCount uTouchGesture uTouchPoints | - |
| `lib_truchet.glsl` | 457 | library include | lib | - | reads: uIntensity uMorph uTime uTurbulence uWarp | - |
| `lib_world.glsl` | 987 | library include | lib | - | declares: uWorld[24] uWorldShapes uWorldTex; reads: uCamBasis uCamFocal uCamPos | - |
| `life_show_frag.glsl` | 95 | pass | life (sim scene) | sound relief | declares: uState uRes uSimRes uFrac uLookYoung uLookOld uDepth uShowV uBaseHue uHueSpan; SND: ENERGY_TREND HAT_ENV ONSET_ENV TREBLE_PRESENCE | - |
| `life_step_frag.glsl` | 256 | pass | life (sim scene) | - | declares: uPrev uRes uRule uDt uCore uGrowth uMu uSigma uRadius uRings uB uF uK uDiff uAniso uSeed uSeedJitter uSeedSalt uBud uBudPos uInk uSprinkle uShift uAgeRate uTime uTouchPoints[TOUCH_MAX_POINTS] uTouchCount | - |
| `liquid_frag.glsl` | 175 | pass | frame pass | feedback | declares: uPrev uStyle uBorn uVel uVelScale uTexel uStyleTexel uDt uAspect uVp uInject uEdge uSharpen uBornR uBornGain uBornMode uAge uSurface uFeedback uShimmer uLight uTime | - |
| `liss_frag.glsl` | 299 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade | reads: uCamFocal uKnob uMorph uResolution uSteps uTime uTurbulence uWarp; SND: BASS CUT_COUNT ENERGY_TREND HAT_ENV HELD_NOTE_CHANGE HELD_SURGE KICK_ENV LEVEL MIDDLE MID_TIME | ki |
| `lucid_bulge_frag.glsl` | 324 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade lucid | reads: uBreath uFlowTime uIntensity uKeyHue uKeyStrength uKnob uMorph uSteps uTime; SND: HELD_NOTE_CHANGE INTENSITY LOW | k0,1,2,3,4,5 |
| `lucid_disc_frag.glsl` | 392 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade lucid | reads: uCamBasis uCamDolly uCamFocal uCamTravel uFlowTime uIntensity uKnob uMorph uResolution uSoundDrive uSteps uTime; SND: CUT_COUNT HELD_HARD_CUT HELD_SURGE INTENSITY KICK_ENV SNARE_ENV TREBLE_PRESENCE | k0,1,2,3,4,5 |
| `lucid_hive_frag.glsl` | 297 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade sdf3 lucid | reads: uCamBasis uHarmony uIntensity uKnob uMorph uSoundDrive uSteps uTime uTurbulence uTwist; SND: ENERGY_TREND LEVEL SNARE_ENV | k0,1,2,3,4,5 |
| `lucid_ink_frag.glsl` | 292 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade lucid | reads: uCamFocal uFlowTime uIntensity uKnob uMorph uResolution uSteps uTime; SND: BASS_PRESENCE FLUX MID_PRESENCE | k0,1,2,3,4,5 |
| `lucid_lattice_frag.glsl` | 316 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade sdf3 lucid | reads: uFlowTime uHarmony uIntensity uKaleido uKnob uMorph uSteps uSymmetry uTime; SND: BASS_PRESENCE KICK_ENV MID_PRESENCE | k0,1,2,3,4,5 |
| `lucid_mandala_frag.glsl` | 327 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade sdf3 lucid | reads: uCamFocal uDrift uFlowTime uIntensity uKaleido uKnob uMorph uResolution uSteps uSymmetry uTime; SND: HAT_ENV KICK_ENV LEVEL MIDDLE | ki |
| `lucid_mirror_frag.glsl` | 315 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade sdf3 lucid | reads: uCamFocal uFlowTime uIntensity uKnob uMorph uResolution uSteps uTime; SND: ENERGY_TREND HIGH ONSET_COUNT ONSET_ENV | k0,1,2,3,4,5 |
| `lucid_petal_frag.glsl` | 321 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade lucid | reads: uFlowTime uIntensity uKaleido uKnob uMorph uSteps uSymmetry uTime; SND: LEVEL MIDDLE | k0,1,2,3,4,5 |
| `lucid_spiral_frag.glsl` | 249 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade lucid | reads: uIntensity uKaleido uKnob uSteps uSymmetry uTime; SND: BASS_PRESENCE KICK_ENV MID_PRESENCE TREBLE_PRESENCE | k0,1,2,3,4,5 |
| `lucid_tunnel_frag.glsl` | 291 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade lucid | reads: uCamBasis uFlowTime uIntensity uKnob uMorph uSteps uTime uWarp uZoom; SND: ENERGY_TREND HELD_SURGE KICK_ENV LEVEL | k0,1,2,3,4,5 |
| `lucid_vortex_frag.glsl` | 348 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade touch sdf3 lucid | reads: uCamBasis uCamFocal uEnergyRel uIntensity uKnob uMorph uOrbit uResolution uSoundDrive uSteps uTime uTwist; SND: ENERGY_TREND ONSET_ENV | k0,1,2,3,4,5 |
| `lucid_weave_frag.glsl` | 294 | style fragment | lucid | scene_uniforms scene_motion camera palette scene_grade touch lucid | reads: uFlowTime uIntensity uKnob uSteps uTime; SND: INTENSITY LOW | k0,1,2,3,4,5 |
| `mandala_dome_frag.glsl` | 364 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 touch | reads: uCamFocal uFlowPhase uKnob uResolution uSteps uTime; SND: BASS_ATT ENERGY_TREND HELD_SURGE HIGH KICK_ENV TREBLE_PRESENCE | k0,1,3,4,5 |
| `mandel_frag.glsl` | 306 | style fragment | loose | scene_uniforms camera palette scene_grade | reads: uKnob uMorph uSteps uTime uTurbulence uWarp; SND: ENERGY_TREND HELD_NOTE_CHANGE HIT_TIME KICK_ENV ONSET_ENV SUB_ATT | ki |
| `mandelbox_cathedral_frag.glsl` | 606 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade neon | reads: uSteps uTime; SND: CHORD_HUE CUT_COUNT HELD_HARD_CUT SNARE_ENV TENSION | via neonKnob |
| `menger_core_frag.glsl` | 272 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uCamFocal uResolution uSteps uTime; SND: BASS_ATT CUT_COUNT ENERGY_TREND HAT_ENV HELD_HARD_CUT HELD_NOTE_CHANGE HELD_SURGE KICK_ENV ONSET_COUNT ONSET_ENV SNARE_ENV | - |
| `merkaba_grid_frag.glsl` | 422 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 touch | reads: uCamFocal uFlowPhase uFormPhase uIntensity uKnob uMorph uResolution uSpawnSeed uSteps uTime uTurbulence uWarp; SND: BASS_PRESENCE HIGH LEVEL | k0,1,2,3,4,5 |
| `metaballs_frag.glsl` | 259 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uKnob uMorph uSteps uTime uTurbulence uWarp; SND: HELD_NOTE_CHANGE HELD_SURGE INTENSITY KICK_ENV LEVEL SNARE_ENV | k0,1,2,3,4,5 |
| `morph_world_frag.glsl` | 65 | style fragment | loose | scene_uniforms camera palette scene_grade world | reads: uSteps | - |
| `morphogen_frag.glsl` | 1036 | style fragment | loose | scene_uniforms scene_motion palette scene_grade sdf3 touch dmt | reads: uMorph uResolution uSteps uTime uTouchCount uTouchPoints uTouchSpin; SND: BASS_PRESENCE HIGH LEVEL MIDDLE | - |
| `myco_agent_frag.glsl` | 161 | pass | myco (sim scene) | - | declares: uAgents uTrail uTrailRes uSensorDist uSensorAngle uTurnAngle uMoveStep uMatrix uJitter uSnap uTime uAniso uInit uSpeciesMix uTouchPoints[TOUCH_MAX_POINTS] uTouchCount uTouchBirth | - |
| `myco_blur_frag.glsl` | 32 | pass | myco (sim scene) | - | declares: uTrail uTrailRes uDecay | - |
| `myco_deposit_frag.glsl` | 18 | pass | myco (sim scene) | - | declares: uDeposit | - |
| `myco_deposit_vert.glsl` | 22 | vertex | myco (sim scene) | - | declares: uAgents uAgentRes | - |
| `myco_show_frag.glsl` | 86 | pass | myco (sim scene) | - | declares: uTrail uRes uTrailRes uLook uBaseHue uHueSpan uExposure uEnergy | - |
| `nebula_cluster_frag.glsl` | 293 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uCamFocal uKnob uResolution uSoundDrive uSteps; SND: HAT_ENV KICK_ENV LEVEL ONSET_COUNT ONSET_ENV | k0,1,2,3,4,5 |
| `nebula_dust_frag.glsl` | 152 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uKnob uSteps; SND: HAT_ENV INTENSITY LOW | k0,1,2,3,4,5 |
| `nebula_flight_frag.glsl` | 112 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uSteps; SND: BASS_PRESENCE HAT_ENV LEVEL MIDDLE | - |
| `nebula_frag.glsl` | 866 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 touch dmt | reads: uResolution uSteps uTime uTouchAxis uTouchSpin; SND: BASS_PRESENCE KICK_ENV LOW MID_PRESENCE TREBLE_PRESENCE | - |
| `nebula_horizon_frag.glsl` | 277 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uCamFocal uKnob uResolution uSoundDrive uSteps; SND: BASS_PRESENCE ENERGY_TREND HAT_ENV | k0,1,2,3,4,5 |
| `nebula_nova_frag.glsl` | 233 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uKnob uSoundDrive uSteps; SND: BASS_PRESENCE CUT_COUNT HAT_ENV HELD_HARD_CUT HELD_SURGE MID_PRESENCE | k0,1,2,3,4,5 |
| `nebula_pillars_frag.glsl` | 144 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uSteps; SND: HAT_ENV INTENSITY LOW | - |
| `nebula_pulsar_frag.glsl` | 223 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uKnob uSteps; SND: HAT_ENV INTENSITY KICK_ENV LOW | k0,1,2,3,4,5 |
| `nebula_ring_frag.glsl` | 237 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uCamFocal uCamTravel uKnob uResolution uSteps; SND: BASS_PRESENCE HAT_ENV HELD_SURGE LOW MID_PRESENCE | ki |
| `nebula_spiral_frag.glsl` | 247 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uKnob uSteps; SND: HAT_ENV KICK_ENV LEVEL MIDDLE | k0,1,2,3,4,5 |
| `nebula_storm_frag.glsl` | 148 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uSteps; SND: BASS_PRESENCE HAT_ENV HIT_TIME LOW MID_PRESENCE SNARE_ENV | - |
| `nebula_veil_frag.glsl` | 229 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uKnob uSteps; SND: ENERGY_TREND HAT_ENV HIGH KICK_ENV | k0,1,2,3,4,5 |
| `nebula_web_frag.glsl` | 131 | style fragment | nebula | scene_uniforms scene_motion camera palette scene_grade nebula | reads: uSteps; SND: BASS_ATT BASS_PRESENCE ENERGY_TREND HAT_ENV ONSET_COUNT ONSET_ENV | - |
| `nectar_flow_frag.glsl` | 178 | style fragment | loose | scene_uniforms scene_motion palette scene_grade sdf3 touch dmt | reads: uFlowPhase uFormPhase uSpawnSeed uSteps uTime; SND: BASS_PRESENCE LOW MID_PRESENCE TREBLE_PRESENCE | - |
| `neon_chrome_sea_frag.glsl` | 304 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade neon sdf3 | reads: uSteps uTime; SND: BASS_TIME CHORD_HUE | via neonKnob |
| `neon_city_frag.glsl` | 240 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uCamPos uSteps uTime; SND: BASS_PRESENCE FLUX HELD_SURGE INTENSITY LOW MID_HIGH_ATT MID_PRESENCE SNARE_ENV SUB_ATT TREBLE_ATT TREBLE_PRESENCE | - |
| `neon_tiles_frag.glsl` | 354 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 touch | reads: uCamBasis uCamFocal uFlowPhase uKaleido uKnob uMorph uResolution uSteps uSymmetry uTime uTurbulence uWarp; SND: BASS_PRESENCE CUT_COUNT HELD_HARD_CUT HIGH KICK_ENV LEVEL | k0,1,2,3,4,5 |
| `noneuclid_frag.glsl` | 790 | style fragment | loose | scene_uniforms scene_motion palette scene_grade sdf3 touch dmt | reads: uResolution uSteps uTime uTouchAnchor uTouchAxis uTouchSpin; SND: BASS_PRESENCE HIGH LEVEL MIDDLE | - |
| `opal_bubbles_frag.glsl` | 49 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | reads: uTime | - |
| `opal_cells_frag.glsl` | 45 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | - | - |
| `opal_domes_frag.glsl` | 46 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | - | - |
| `opal_drop_frag.glsl` | 50 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | reads: uTime | - |
| `opal_lava_frag.glsl` | 47 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | reads: uTime | - |
| `opal_melt_frag.glsl` | 50 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | reads: uTime | - |
| `opal_nautilus_frag.glsl` | 56 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | reads: uTime | - |
| `opal_pearls_frag.glsl` | 48 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | - | - |
| `opal_ribbon_frag.glsl` | 50 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | - | - |
| `opal_rings_frag.glsl` | 58 | style fragment | opal | scene_uniforms scene_motion camera palette scene_grade neon opal opal_march | reads: uTime | - |
| `orb_lattice_frag.glsl` | 255 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uFlowPhase uFormPhase uKaleido uKnob uMorph uMoveDir uSpawnSeed uSteps uSymmetry uTime uTurbulence uWarp; SND: BASS_PRESENCE HAT_ENV HIGH INTENSITY KICK_ENV LEVEL | k0,1,2,3,4,5 |
| `outrun_drive_frag.glsl` | 243 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uFlowPhase uTime; SND: BASS_PRESENCE CUT_COUNT HAT_ENV HELD_HARD_CUT HELD_SURGE HIT_TIME KICK_ENV LOW MID_PRESENCE SNARE_ENV SUB_ATT | - |
| `particle_tunnel_frag.glsl` | 151 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uSteps uTime; SND: HAT_ENV HELD_HARD_CUT LEVEL ONSET_COUNT ONSET_ENV | - |
| `plasma_frag.glsl` | 259 | style fragment | loose | scene_uniforms camera palette scene_grade sdf3 | reads: uCamFocal uKnob uMorph uResolution uSteps uTime uTurbulence uWarp; SND: BASS HAT_ENV HELD_NOTE_CHANGE HELD_SURGE KICK_ENV SNARE_ENV VOL_ATT | k0,1,2,3,4,5 |
| `pm_post_frag.glsl` | 110 | pass | frame pass | - | declares: uTex uZoom uRotation uMirrorX uHue uSat uBright uContrast uGamma uInvert uIntensity uPalBase uPalSpan uPalTint | - |
| `pool_frag.glsl` | 179 | style fragment | loose | scene_uniforms palette scene_grade | declares: uPoolWaves[8] uPoolRings[3] uPoolWakes[6] uPoolLook uPoolView; reads: uKnob; SND: BASS_ATT PRESENCE TREBLE_ATT VOL_ATT | k3 |
| `prism_rain_frag.glsl` | 295 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uFlowPhase uSteps uTime; SND: BASS_PRESENCE CUT_COUNT HAT_ENV HELD_HARD_CUT HELD_NOTE_CHANGE HELD_SURGE HIT_TIME INTENSITY KICK_ENV MID_HIGH_ATT MID_PRESENCE ONSET_ENV SNARE_ENV SUB_ATT TREBLE_ATT TREBLE_PRESENCE | - |
| `quad_vert.glsl` | 9 | vertex | frame pass | - | - | - |
| `ribbon_storm_frag.glsl` | 265 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uFlowPhase uTime; SND: BASS_ATT CUT_COUNT ENERGY_TREND HAT_ENV HELD_HARD_CUT HELD_NOTE_CHANGE HELD_SURGE HIGH KICK_ENV LEVEL MID_ATT ONSET_COUNT ONSET_ENV SNARE_ENV | - |
| `ring_frag.glsl` | 330 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uCamTravel uKnob uMorph uOrbit uSteps uTime uTurbulence uWarp; SND: ENERGY_TREND HELD_NOTE_CHANGE HELD_SURGE KICK_ENV LEVEL MIDDLE SUB_ATT | ki |
| `ringed_planet_frag.glsl` | 330 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uFlowPhase uKeyHue uKeyStrength uTime; SND: BASS_PRESENCE CUT_COUNT HAT_ENV HELD_HARD_CUT HELD_NOTE_CHANGE HELD_SURGE HIGH KICK_ENV LEVEL ONSET_COUNT ONSET_ENV SNARE_ENV | - |
| `ripples_frag.glsl` | 250 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uKnob uMorph uSteps uTime uTurbulence uWarp; SND: BASS_ATT ENERGY_TREND HELD_NOTE_CHANGE KICK_ENV MID_ATT VOL_ATT | k0,1,2,3,4,5 |
| `rod_tunnel_frag.glsl` | 235 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 touch dmt | reads: uFormPhase uSteps uTime; SND: BASS_PRESENCE LOW MID_PRESENCE ONSET_ENV TREBLE_PRESENCE | - |
| `scope_frag.glsl` | 286 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uCamFocal uKnob uMorph uResolution uSteps uTime uTurbulence uWarp; SND: ENERGY_TREND HAT_ENV HELD_NOTE_CHANGE KICK_ENV LEVEL SNARE_ENV TREBLE_ATT VOL_ATT | ki |
| `silk_show.glsl` | 112 | pass | silk (sim scene) | sound relief | declares: uRes uBaseHue uHueSpan uExposure uFold uFoldPhase uEnergy uMelodyHue uMelodyAmt uLookBase uLookAlt uDepth; SND: HAT_ENV ONSET_ENV SNARE_ENV | - |
| `silk_step.glsl` | 303 | pass | silk (sim scene) | - | declares: uField uB uAdvect uDecay uFieldScale uSwirl uSlabX uSlabY uSeedEpoch uDrift uStrokes uElong uDrive uBass uMid uTreble uStrike uBeatRing uRingCenter uRingTint uEmitter uEmit uEmitSwirl uWind uWrithe uTouchPoints[TOUCH_MAX_POINTS] uTouchCount | - |
| `solar_frag.glsl` | 249 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uKeyHue uKeyStrength uKnob uMorph uSteps uTime uTurbulence uWarp; SND: ENERGY_TREND HELD_NOTE_CHANGE HELD_SURGE HIGH KICK_ENV LOW | k0,1,2,3,4,5 |
| `spectrum_terrain_frag.glsl` | 229 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uKeyHue uKeyStrength uSteps uTime; SND: BASS_ATT BASS_PRESENCE CUT_COUNT FLUX HAT_ENV HELD_HARD_CUT HELD_NOTE_CHANGE HELD_SURGE HIGH KICK_ENV LEVEL MID_ATT SNARE_ENV | - |
| `spiral_eye_frag.glsl` | 285 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 touch | reads: uFlowPhase uFormPhase uKnob uMorph uMoveDir uSteps uTime uTurbulence uWarp; SND: BASS_PRESENCE LOW MID_PRESENCE TREBLE_PRESENCE | k0,1,2,3,4,5 |
| `spiral_frag.glsl` | 242 | style fragment | loose | scene_uniforms camera palette scene_grade sdf3 | reads: uKnob uMorph uSteps uTime uTurbulence uWarp; SND: BASS_ATT HELD_NOTE_CHANGE HELD_SURGE INTENSITY KICK_ENV LEVEL SNARE_ENV TREBLE_ATT | k0,1,2,3,4,5 |
| `starfield_frag.glsl` | 272 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uCamFocal uKnob uMorph uResolution uSteps uTime uTurbulence uWarp; SND: HAT_ENV HELD_SURGE HIT_TIME KICK_ENV LOW ONSET_ENV SNARE_ENV | ki |
| `supershape_frag.glsl` | 547 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uCamBasis uFlowPhase uSoundTimeSped uSteps; SND: BASS_ATT CENTROID CUT_COUNT DENSITY_FAST ENERGY_TREND FLATNESS FLUX HAT_ENV HELD_HARD_CUT HELD_SURGE KICK_ENV NOTE_TIME ONSET_ENV PITCH_CLASS PITCH_CONFIDENCE SNARE_ENV STEREO_PAN STEREO_WIDTH TREBLE_ATT | - |
| `terra_canyon_frag.glsl` | 152 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSteps; SND: BASS_ATT BASS_PRESENCE ENERGY_TREND KICK_ENV | - |
| `terra_dunes_frag.glsl` | 133 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSteps; SND: BASS_PRESENCE HAT_ENV LOW MID_PRESENCE | - |
| `terra_glacier_frag.glsl` | 137 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSteps; SND: INTENSITY LOW SNARE_ENV | - |
| `terra_isles_frag.glsl` | 142 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSteps; SND: BASS_ATT BASS_PRESENCE HIGH LEVEL | - |
| `terra_mesa_frag.glsl` | 126 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSteps; SND: BASS_PRESENCE LOW MID_PRESENCE | - |
| `terra_moon_frag.glsl` | 173 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSoundDrive uSteps; SND: BASS_PRESENCE LEVEL ONSET_COUNT ONSET_ENV | - |
| `terra_night_frag.glsl` | 144 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSteps; SND: BASS_PRESENCE MID_HIGH_ATT MID_PRESENCE TREBLE_PRESENCE | - |
| `terra_ridge_frag.glsl` | 118 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uKeyHue uKeyStrength uSteps; SND: BASS_ATT BASS_PRESENCE LEVEL | - |
| `terra_terraces_frag.glsl` | 146 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSoundDrive uSteps; SND: INTENSITY KICK_ENV MID_PRESENCE | - |
| `terra_topo_frag.glsl` | 225 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uKeyHue uKeyStrength uKnob uSoundDrive uSteps; SND: BASS_PRESENCE ENERGY_TREND KICK_TOGGLE | k0,1,2,3,4,5 |
| `terra_volcano_frag.glsl` | 150 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uKeyHue uKeyStrength uSoundDrive uSteps; SND: BASS_PRESENCE ENERGY_TREND KICK_ENV | - |
| `terra_wire_frag.glsl` | 133 | style fragment | terra | scene_uniforms scene_motion camera palette scene_grade terra | reads: uSteps; SND: INTENSITY | - |
| `trail_warp_frag.glsl` | 30 | pass | frame pass | - | declares: uPrev uDecay uScale uWarp uTime | - |
| `truchet_arcs_frag.glsl` | 263 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamBasis uKeyHue uKeyStrength uKnob uSteps uTurbulence uWarp; SND: BASS_PRESENCE HIT_TIME LOW MID_PRESENCE ONSET_ENV | k0,1,2,3,4,5 |
| `truchet_flow_frag.glsl` | 270 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uKeyHue uKeyStrength uKnob uSteps uTurbulence uWarp; SND: BASS_PRESENCE CURVED_TIME HIT_TIME LOW MID_PRESENCE ONSET_ENV | k0,1,2,3,4,5 |
| `truchet_hex_frag.glsl` | 313 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamBasis uCamFocal uCamPos uFlowTime uKeyHue uKeyStrength uKnob uResolution uSteps uTurbulence uWarp; SND: BASS_PRESENCE ENERGY_TREND MIDDLE ONSET_COUNT ONSET_ENV | k0,1,2,3,4,5 |
| `truchet_knot_frag.glsl` | 343 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamBasis uKeyHue uKeyStrength uKnob uSoundDrive uSteps uTurbulence uWarp; SND: BASS_PRESENCE ENERGY_TREND ONSET_COUNT ONSET_ENV SNARE_ENV | k0,1,2,3,4,5 |
| `truchet_maze_frag.glsl` | 322 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uKeyHue uKeyStrength uKnob uSteps uTime uTurbulence uWarp; SND: BASS_PRESENCE HIT_TIME LEVEL ONSET_COUNT ONSET_ENV | k0,1,2,3,4,5 |
| `truchet_neon_frag.glsl` | 345 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uKeyHue uKeyStrength uKnob uSteps uTurbulence uWarp; SND: BASS_PRESENCE LEVEL MIDDLE ONSET_COUNT ONSET_ENV | k0,1,2,3,4,5 |
| `truchet_quad_frag.glsl` | 407 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamBasis uCamFocal uKeyHue uKeyStrength uKnob uResolution uSoundDrive uSteps uTurbulence uWarp; SND: BASS_PRESENCE ENERGY_TREND KICK_ENV ONSET_COUNT ONSET_ENV | k0,1,2,3,4,5 |
| `truchet_relief_frag.glsl` | 173 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamBasis uCamFocal uCamPos uKeyHue uKeyStrength uSoundDrive uSteps uTurbulence uWarp; SND: BASS_PRESENCE LEVEL ONSET_COUNT ONSET_ENV | - |
| `truchet_tri_frag.glsl` | 360 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamFocal uKeyHue uKeyStrength uKnob uSteps uTurbulence uWarp; SND: BASS_PRESENCE CUT_COUNT HELD_HARD_CUT HIT_TIME LOW MID_PRESENCE ONSET_ENV | k0,1,2,3,4,5 |
| `truchet_tubes_frag.glsl` | 295 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamBasis uKeyHue uKeyStrength uKnob uSteps uTurbulence uWarp uZoom; SND: HIT_TIME INTENSITY LOW ONSET_ENV TREBLE_PRESENCE | k0,1,2,3,4,5 |
| `truchet_tunnel_frag.glsl` | 288 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamBasis uCamFocal uFlowTime uKeyHue uKeyStrength uKnob uResolution uSteps uZoom; SND: HELD_SURGE HIT_TIME INTENSITY LOW ONSET_ENV | k0,1,2,3,4,5 |
| `truchet_weave_frag.glsl` | 332 | style fragment | truchet | scene_uniforms scene_motion camera palette scene_grade truchet | reads: uCamBasis uCamFocal uCamPos uKeyHue uKeyStrength uKnob uResolution uSteps uTurbulence uWarp; SND: HIGH HIT_TIME INTENSITY KICK_ENV LOW ONSET_ENV SNARE_ENV | k0,1,2,3,4,5 |
| `tunnel_frag.glsl` | 253 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uCamBasis uKnob uMorph uSteps uTime uTurbulence uWarp uZoom; SND: ENERGY_TREND HELD_SURGE HIGH KICK_ENV | k0,1,2,3,4,5 |
| `ui_blur_frag.glsl` | 26 | ui | ui | - | declares: uSrc uStep | - |
| `ui_blur_mask_frag.glsl` | 30 | ui | ui | - | declares: uBlur uRect uTexScale uCorner uStrength | - |
| `vanishing_frag.glsl` | 731 | style fragment | loose | scene_uniforms scene_motion palette scene_grade sdf3 touch dmt | reads: uFlowPhase uResolution uSteps uTime uTouchAxis uTouchSpin; SND: BASS_PRESENCE LOW MID_PRESENCE TREBLE_PRESENCE | - |
| `voronoi_frag.glsl` | 304 | style fragment | loose | scene_uniforms camera palette scene_grade sdf3 | reads: uKnob uMorph uSoundDrive uSoundTex uSteps uTime uTurbulence uWarp; SND: ENERGY_TREND HAT_ENV HELD_NOTE_CHANGE KICK_ENV | k0,1,2,3,4,5 |
| `warp_frag.glsl` | 237 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uFlowTime uKnob uMorph uSteps uTime uTurbulence uWarp; SND: ENERGY_TREND HELD_NOTE_CHANGE HELD_SURGE KICK_ENV LEVEL | ki |
| `waves_frag.glsl` | 284 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uKnob uMorph uSteps uTime uTurbulence uWarp; SND: BASS_ATT ENERGY_TREND HELD_NOTE_CHANGE KICK_ENV TREBLE_ATT VOL | k0,1,2,3,4,5 |
| `winter_frag.glsl` | 438 | style fragment | loose | scene_uniforms scene_motion camera palette scene_grade sdf3 | reads: uCamFocal uKnob uMorph uResolution uSteps uSway uTime uTurbulence uWarp; SND: BASS_ATT ENERGY_TREND KICK_ENV LEVEL MIDDLE TREBLE | k0,1,2,3,4,5 |
| `world_shapes.bin` | 786432 bytes | data table | - | - | - | - |
| `world_textures.bin` | 131072 bytes | data table | - | - | - | - |

#### What each `lib_*` provides, and who includes it

Users counted over all 298 `.glsl` files (both halves), by direct `//#include`.

| Library | Provides | Uniforms it declares | Included by |
|---|---|---|---|
| `lib_scene_uniforms` (209) | `view()` (pixelate, mirror, kaleidoscope, morph, rotation+sway, drift, zoom, turbulence, twist, tile, warp, ripple; `VIEW_OWNS_SHAPE` switches morph/turbulence/warp off, lines 146-202); `TOUCH_MAX_POINTS 5`; pulls in `lib_sound` (line 30) | 35 look floats `uTime`..`uSolarize` (32-66), `uKnob[6]` (86), `uTouchAnchor`, `uTouchPoints[5]`, `uTouchCount`, `uTouchGesture`, `uTouchAxis`, `uTouchSpin` (104-114), `uSteps` (124) | 212 files: every ShaderScene style of every family and loose style, both halves |
| `lib_sound` (174) | 119 `SND_*` macros over `uSound[29]`, 6 held/counter macros, `sndSpectrum`, `sndWave`, `sndWave01`, `sndEq`, `sndChroma`, `sndLevel`, `sndShaped`, `sndPhase` | `uSound[29]`, `uSoundTex`, `uSoundDrive`, `uSoundEvents`, `uSoundCounts` (17-28) | `lib_scene_uniforms`, and directly by `life_show_frag`, `silk_show`, `acid_show_frag` |
| `lib_palette` (76) | `palStops`, `palProcedural`, `pal` | `uPalLut`, `uPalLutMix`, `uPalLutRow`, `uPalStops[5]`, `uPalStopCount` (30-41) | 212 files (same set as `lib_scene_uniforms`) |
| `lib_scene_grade` (28) | `grade()` (bloom, posterize, duotone, saturation, contrast, gamma, temperature, solarize, brightness x intensity, invert) | none | 212 files |
| `lib_scene_motion` (274) | `spawnGrow`, `spawnFresh`, `flowOffset`, `flowBasis`, `motionHash/Noise/Fbm`, `curlVelocity`, `fluidWarp`, `fluidMotes`, `abcFlow`, `fluidWarp3`, `fluidWarp3Lipschitz` | 19 motion uniforms (43-118) | 205 files (all ShaderScene styles except `kaleido`, `mandel`, `plasma`, `spiral`, `voronoi`, `morph_world`, `pool` in this half) |
| `lib_camera` (57) | `camRayDir`, `camFly`, `camAround`, `CAM_SPIN_UNIT`, `CAM_AROUND_GAIN` | `uCamPos`, `uCamBasis`, `uCamFocal`, `uCamTravel`, `uCamDolly` (17-25) | 206 files (not `morphogen`, `noneuclid`, `vanishing`, `nectar_flow`, `pool`, `curl_bloom`) |
| `lib_sdf3` (401) | `sdSphere/Box/Torus/Octahedron`, `smin/smax/sminMat`, `opRound/Onion/Twist`, `rot2/rotX/Y/Z/rotAxis`, `hash11/13`, `hashCell`, `uhashUnit`, `vnoise3`, `fbm3`; reads no uniform | none | 92 files: most loose marched styles, arbor, bulb, chrys, foam, some lucid |
| `lib_touch` (166) | `touchIdle`, `touchAnchor`, `touchStrength`, `touchFalloff`, `touchWake`, `touchWarp3`, `touchWarpLipschitz` | none (reads lib_scene_uniforms' touch uniforms) | 18 files: `kifs`, `morphogen`, `nebula`, `noneuclid`, `vanishing`, `nectar_flow`, `rod_tunnel`, `mandala_dome`, `merkaba_grid`, `neon_tiles`, `spiral_eye`, `lucid_vortex`, `lucid_weave`, and 5 first-half styles |
| `lib_dmt` (557) | `dmtShade`, `dmtHalo`, `dmtChrysanthemum`, `dmtMorphBody`, `dmtSatellites*`, `dmtMarchSatellites`, `dmtFlight*`, `dmtTunnel*`, `dmtLife*`; reads `uFlowPhase`, `uFormPhase`, `uMoveDir`, `uSteps`, `uTime` | none | 8 loose styles: `curl_bloom`, `kifs`, `morphogen`, `nebula`, `nectar_flow`, `noneuclid`, `rod_tunnel`, `vanishing` |
| `lib_neon` (112) | `neonKnob`, `neonForm/Chaos/Flow`, `neonKick/Surge/Quiet/Weight/Build`, `neonPal`, `neonKeyTurn`, `neonGlow`, `neonSparkle`, `neonAces`, `neonFinish`; reads `uKnob`, `uIntensity`, `uMorph`, `uTurbulence`, `uWarp`, `uKeyHue`, `uKeyStrength`, `uPalLutMix`, 7 `SND_*` | none | 35 files: the neon set (`glow_vortex`, `neon_chrome_sea`, `black_hole`, `hex_hive`, `mandelbox_cathedral`) and all flame, opal, hollow styles |
| `lib_feedback` (82) | `FrameMotion`, `frameMotion()`, `uvFor()`, `fold()` | `uZoom`, `uZoomExp`, `uRot`, `uCx`, `uCy`, `uDx`, `uDy`, `uSx`, `uSy`, `uWarp`, `uWarpScale`, `uWarpSpeed` (21-32) | `liquid_frag` only |
| `lib_relief` (315) | `reliefLook`, `reliefMarch`, `reliefShade`, `reliefLight`, `reliefTilt`, `rlHue/Sat/Sky/Glint/Hash`; 8 materials `RELIEF_SATIN..RELIEF_INK` (43-50); reads 9 `SND_*` | none | `life_show_frag`, `silk_show`, `acid_show_frag` |
| `lib_particle_common` (146) | `ptHash12`, `ptHsv2rgb`, `ptSd*` shapes, `ptShapeField`, `ptBillboard`, `ptRadiusFade`, `ptSpikes`, `ptAces` | none | 4 fluid particle shaders (first half) |
| `lib_particle_shade` (94) | `ptShade` (fragment only, uses `fwidth`) | none | 2 fluid particle fragment shaders (first half) |
| `lib_psrdnoise2` (149) | `psrdnoise` (MIT, Gustavson and McEwan, header lines 1-6) | none | `curl_field_frag` and one fluid shader (first half) |
| `lib_world` (987) | Morph World: `world`, `worldRender`, `worldCamera`, `worldPrepare`, operators `wKifs/wMenger/wBulb/wJulia/wMandelbox/wApollo/wGyroid/wLattice/...`, `WG_*` indices into `uWorld` | `uWorld[24]` (70), `uWorldShapes` sampler3D (185), `uWorldTex` sampler2DArray (186) | `morph_world_frag` only |
| `lib_arbor` (398) | tree fields `arborTree`, `arborTree2`, `arborDendrite2`, `arbLimb2/3`, `arbView`, `arbMorph/Chaos/Levels/StepAO/Light/Ink` | none | 12 `arbor_*` (first half) |
| `lib_bulb` (317) | `bulbDE`, `bulbJuliaDE`, `bulbPow`, `bulbFrame`, `bulbAim`, `bulbIters`, `bulbChaos*`, `bulbEps`, `bulbStepAO`, `bulbTime`, `bulbEnergy`, `bulbRamp` | none | 12 `bulb_*` (first half) |
| `lib_chrys` (258) | petal geometry `chrysPetal`, `chrysProfile`, `chrysFloret`, `chrysLeaf`, `chrysCore`, `chrysGlow`, hashes; reads no uniform | none | 12 `chrys_*` (first half) |
| `lib_dive` (666) | whole fly-through: `diveSetup`, `diveCamera`, `diveMap`, `diveMarch`, `diveShade`, `diveColor`, five scenes, five camera paths, six own cosine palettes | none | 12 `dive_*` (first half) |
| `lib_glass` (973) | optics `glassRefract`, `glassFresnel`, `glassFilm`, `glassBeer`, `glassCaustic*`, seven environments `glassEnv*`, `glassListen`, `glassTone` | none | 12 `glass_*` (first half) |
| `lib_foam` (610) | Apollonian packing `foam`, `foamMap`, `foamMarch`, `foamCamera`, `foamOrbitCamera`, materials `foamNeon/Soap/Pearl`, `foamFinish` | none | 12 `foam_*` (first half) |
| `lib_flame` (167) + `lib_flame_march` (55) | `flameSetup`, `flameCol`, `flameLine`, `flameFinish`, ...; the march half calls the style's `flameField` and is included after it | none | 10 `flame_*` (first half) |
| `lib_opal` (139) + `lib_opal_march` (70) | `opalSetup`, `opalCol`, `opalFinish`, ...; `opalField`, `opalSkin`, `opalTrace` call the style's `opalMap` | none | 10 `opal_*` |
| `lib_hollow` (158) + `lib_hollow_march` (63) | `hollowSetup`, `hollowEye`, `hollowCut`, `hollowFinish`, ...; `hollowField`, `hollowTrace` call the style's `hollowMap` | none | 10 `hollow_*` (1 in this half: `hollow_throat`) |
| `lib_lucid` (427) | noise kit `lucidValue/Simplex/Cells/Ridged/Sines/Fbm/Nest`, folds, `lucidView`, `lucidBudget`, `lucidFilm` | none | 12 `lucid_*` |
| `lib_nebula` (539) | `nebFbm2/3`, `nebWarp*`, `nebStars`, `nebSky`, volume step `nebVolStep/Dt/Emit`, `nebKeyShift`, `nebEnergy`, `nebTime` | none | 12 `nebula_<x>` family styles (not the loose `nebula_frag`) |
| `lib_terra` (327) | `terraNoise/Fbm/Ridged`, `terraFly`, `terraStep`, `terraRefine`, `terraFog`, `terraSky`, `terraStars`, `terraChaos*` | none | 12 `terra_*` |
| `lib_truchet` (457) | tilings `truchetSquare`, `truchetHex*`, `truchetTri*`, `truchetField`, `truchetLine`, `truchetRoll`, `truchetEnergy`, `truchetTime` | none | 12 `truchet_*` |

The family libs declare no uniform of their own: they read the names `lib_scene_uniforms`, `lib_scene_motion`, `lib_camera`, `lib_palette` declare, so they must be pasted after those.

### 2. Who loads each file

**Everything in this half is loaded, assembled, compiled and driven by `core/`. No Kotlin file reads, assembles or compiles any file of this half.**

Assembly path, end to end:

| Step | Where | Side |
|---|---|---|
| The app's `AssetManager` and cache directory are handed over once | `engine/scenes/.../render/bridge/NativeViz.kt:33,74` → `GeodeNative.vizCreate` (`engine/audio-core/.../bridge/GeodeNative.kt:206`) | Kotlin (hand-over only) |
| `AAssetManager_fromJava`, then `geode_viz_create` | `app/src/main/cpp/geode_viz_jni.cpp:128-134` → `core/api/geode_viz_api.cpp:210` | JNI |
| The renderer owns one `ShaderSource`, one `ProgramBinaryCache`, one `ProgramLoader`, one `ProgramStager` | `core/viz/Renderer.hpp:264-271`, `core/viz/Renderer.cpp:26-43` | core |
| Asset read: `AAssetManager_open("shaders/<name>")`, whole file into a string | `core/viz/ShaderSource.cpp:48-65` | core |
| Include resolution: each `//#include x` line is replaced by `shaders/x.glsl`, recursively, with a cycle guard; the name must be in the allow-list | `core/viz/ShaderSource.cpp:72-118`; list `core/viz/ShaderIncludes.inc:10-48` | core |
| Style source: `<id>_frag.glsl` for an id the catalogue calls a shader style | `core/viz/SceneRegistry.cpp:29-36`; ids `core/viz/Catalogue.cpp:17-42` (loose) and `:103-` (families) | core |
| Prefix or header injection (only three cases): Morph World compiles `lib_world` with only the parts in use (`world::withParts`, `core/viz/world/MorphDriver.cpp:227`, called `core/viz/scenes/ShaderScene.cpp:369-389`); Silk wraps `silk_step.glsl` and `silk_show.glsl` (which have no `#version`) in a generated preamble (`core/viz/compute/SimGlsl.cpp:72-96`, called `core/viz/scenes/SilkScene.cpp:70-81`); a GL transition is spliced into `composite_frag.glsl` at a marker with `#define MV_TRANSITION 1` (`core/viz/Transition.cpp:51-57`) | core |
| Compile and link: `glShaderSource`, `glCompileShader`, `glLinkProgram` | `core/viz/Program.cpp:51-101` (`program::build`); spread over frames for a live renderer by `core/viz/ProgramStager.cpp:73-199` | core |
| Cache: SHA-256 of vertex + fragment source is the key; `glGetProgramBinary` to store, `glProgramBinary` to load; a sentinel file disables the cache after a crash | `core/viz/ProgramBinaryCache.cpp:95-102,110,139-147,189-235` | core |
| Uniform locations cached per program | `core/viz/Program.cpp:10-37` (`UniformCache`) | core |
| User-edited source (shader override) replaces a style's fragment; it is compiled as given, without include resolution | Kotlin hands a string: `VisualizerRenderer.kt:144-149` → `GeodeNative.shaderOverrideSet` (`GeodeNative.kt:488`) → `geode_viz_jni.cpp:449-462` → `core/viz/ShaderOverrides.cpp:31-43` → `core/viz/Renderer.cpp:330-346,790-792` → `ShaderScene::compilePendingIfAny` (`ShaderScene.cpp:404-424`) | Kotlin string, core compile |

Loader per file of this half (native, path:line):

| Files | Named by |
|---|---|
| 103 `*_frag.glsl` style fragments | built as `id + "_frag.glsl"` at `core/viz/SceneRegistry.cpp:31`; every stem is an id in `core/viz/Catalogue.cpp` (checked by script: each of the 103 stems appears quoted there) |
| 32 `lib_*.glsl` | `core/viz/ShaderIncludes.inc:10-48` (all 32 listed); loaded at `core/viz/ShaderSource.cpp:91` |
| `quad_vert.glsl` | `core/viz/Renderer.cpp:404`, `core/viz/frame/ExportPasses.cpp:117`, `core/viz/scenes/AcidScene.cpp:57`, `SilkScene.cpp:81`, `LifeScene.cpp:86`, `MycoScene.cpp:20`, `CymaticsScene.cpp:19` |
| `life_step_frag.glsl`, `life_show_frag.glsl` | `core/viz/scenes/LifeScene.cpp:87,92` |
| `myco_agent_frag`, `myco_deposit_vert`, `myco_deposit_frag`, `myco_blur_frag`, `myco_show_frag` | `core/viz/scenes/MycoScene.cpp:22-28` |
| `silk_step.glsl`, `silk_show.glsl` | `core/viz/scenes/SilkScene.cpp:70,81` |
| `liquid_frag.glsl` | `core/viz/liquid/LiquidPass.cpp:35` |
| `pm_post_frag.glsl` | `core/viz/scenes/MilkdropScene.cpp:117` |
| `trail_warp_frag.glsl` | `core/viz/TrailPass.cpp:14` |
| `ui_blur_frag.glsl`, `ui_blur_mask_frag.glsl` | `core/viz/UiBlurPass.cpp:19,21` |
| `world_shapes.bin` (786,432 bytes = 24 x 32^3 R8), `world_textures.bin` (131,072 bytes) | `core/viz/SceneRegistry.cpp:40-41`; sizes checked against `core/viz/world/WorldAssets.hpp:27-31` in `ShaderScene::setWorldAssets` (`ShaderScene.cpp:58-63`); uploaded at `ShaderScene.cpp:289-343`. Baked by `tools/morph_world_assets/bake.py` |
| `cyclic_palettes.bin` (3,840 bytes = 256 x 5 x RGB8; first half) | `core/viz/Renderer.cpp:423-434`, bound as `uPalLut` on unit 2 (`ShaderScene.cpp:141-146`) |
| `blue_noise_64.bin` (4,096 bytes = 64 x 64 R8; first half) | `core/viz/BlueNoise.cpp:6-16` |

Unreferenced files in this half: **none**. Method: for each file, grep of the quoted file name in `core/`, `app/src/main/cpp/`, `app/src/main/java/`, `engine/`; for style fragments, grep of the quoted stem in `Catalogue.cpp` and `StyleCatalog.cpp` (the name is built at run time as `id + "_frag.glsl"`); for libs, the allow-list plus a count of `//#include` users (every lib has at least one). Not checked: that each catalogue id is reachable from a picker.

**Kotlin sites that read a shader asset, build shader source or know a shader file name (the port targets):**

| Site | What it does |
|---|---|
| `engine/scenes/.../render/fluid/FluidShaderTemplate.kt:6-14` | Reads `shaders/fluid_splat_frag.glsl` through `context.assets` (first-half file). Caller: `app/.../ui/CustomizeAdvanced.kt:594-600`, which seeds two text editors with it |
| `app/.../ui/CustomizeAdvanced.kt:574-625` | The fluid force and dye shader editors: the user's GLSL text goes to `VisualizerRenderer.submitFluidInjectionShaders` (`VisualizerRenderer.kt:158-163`) → `NativeViz.setFluidInjectionShaders` (`NativeViz.kt:152`) |
| `engine/scenes/.../render/scene/SceneCapabilities.kt:27-30` | Builds the map id → `"shaders/${id}_frag.glsl"` from the native catalogue. Production users take only the keys: `app/.../ui/AutoVisualsController.kt:257`, `app/.../pro/FreeTier.kt:45`. The path value is read only by a test (`engine/scenes/src/test/.../StyleFamiliesTest.kt:48`) |
| `engine/scenes/.../render/VisualizerRenderer.kt:144-149`, `GeodeNative.kt:488-494` | Hands a user's fragment source for a style to native and reads it back (saved in presets and templates as `customShader`: `app/.../data/PresetStore.kt:173,305`, `app/.../data/TemplateFormat.kt:316,332,559`) |
| `engine/scenes/.../render/TouchField.kt:11-12` | `MAX_POINTS = 5` "mirrors `uTouchPoints` in lib_scene_uniforms.glsl"; used at `app/.../ui/VisualGestures.kt:200,461` and `app/.../wallpaper/VisualizerWallpaperService.kt:111,409` |
| Comments only: `StyleFamilies.kt:8-10`, `ParamScope.kt:15`, `UnderlayBlend.kt:9` | Name shader files in KDoc |

**Other assets:**

- `app/src/main/assets/gl_transitions.json` (151,428 bytes): a JSON array of 122 objects, each with exactly `name`, `author`, `license`, `paramsTypes` (name → GLSL type), `defaultParams` (name → value), `glsl` (the transition function source). Read natively: `core/viz/Transition.cpp:11,19-27` (parse in `core/viz/TransitionLibrary.hpp:75`), spliced into `composite_frag.glsl` (`Transition.cpp:51-57`), params uploaded by type (`Transition.cpp:59-72`). Kotlin gets the parsed table through `GeodeNative.transitionsTable(context.assets)` (`core/api/geode_viz_api.cpp:453-460`) and re-splits it in `engine/scenes/.../render/TransitionCatalog.kt:42-76` (`parseTable`, a separator-delimited decode: a small Kotlin parser of a native format). `app/.../editor/ClipTransition.kt:5` names the file in a comment.
- `app/src/main/assets/milk/`: 6 presets (`aurora_veil`, `fracture`, `geode_bloom`, `prism_well`, `slow_orbit`, `tidal_drift`, 28 KB). Kotlin only: `engine/scenes/.../render/scene/MilkStarterPack.kt:7-48` lists and copies them into `filesDir/milk` once (marker `.starter-pack`, `VERSION = 1`); callers `engine/scenes/.../render/MilkPresetMemory.kt:111`, `app/.../ui/MilkImportController.kt:66`. Native reads the copies by path, not the asset.
- `app/src/main/assets/thumbs/`: only `README.md`; no `.webp` is checked in. Reader: `app/.../widget/WidgetVisual.kt:84-90` opens `thumbs/<styleId>.webp` and falls back to a generated glow when missing, so today every style takes the fallback.

### 3. The uniform contract

Shared contract of every ShaderScene style. "Set" is the `glUniform*` site.

| Group | Uniforms | Declared | Set by |
|---|---|---|---|
| Time, size | `uTime`, `uResolution` | `lib_scene_uniforms.glsl:32-33` | `core/viz/scenes/ShaderScene.cpp:165` (integrated `speed * dt`, wraps at 7100 s, `ShaderScene.hpp:48`, `ShaderScene.cpp:86`), `:138` |
| View and look params | `uZoom`, `uRotation`, `uColorShift`, `uHueRange`, `uSat`, `uBright`, `uInvert`, `uIntensity`, `uMirrorX`, `uTurbulence`, `uContrast`, `uGamma`, `uDuotone`, `uBloom`, `uWarp`, `uRipple`, `uSymmetry`, `uKaleido`, `uMorph`, `uPixelate`, `uPosterize`, `uSway`, `uDriftX`, `uDriftY`, `uTile`, `uTwist`, `uTemperature`, `uSolarize` | `lib_scene_uniforms.glsl:34-66` | `ShaderScene.cpp:166-176,195-212` |
| Palette | `uPalBase`, `uPalRange`, `uPal2Base`, `uPal2Range`, `uPaletteMix` | `lib_scene_uniforms.glsl:44-50` | `ShaderScene.cpp:177-181` |
| Palette stops and LUT | `uPalStops[5]`, `uPalStopCount`, `uPalLut`, `uPalLutMix`, `uPalLutRow` | `lib_palette.glsl:30-41` | `ShaderScene.cpp:182-194`, `:141-148` (texture unit 2) |
| Knobs | `uKnob[6]` | `lib_scene_uniforms.glsl:86` | `ShaderScene.cpp:219-223` (from `viz/NoteResponse`, else 0.5 each) |
| March budget | `uSteps` | `lib_scene_uniforms.glsl:124` | `ShaderScene.cpp:149-150,395-402` (64..128 from Detail, floor 24 after the performance scale) |
| Touch | `uTouchAnchor`, `uTouchPoints[5]`, `uTouchCount`, `uTouchGesture`, `uTouchAxis`, `uTouchSpin` | `lib_scene_uniforms.glsl:104-114` | `ShaderScene.cpp:260-275`; sim scenes: `core/viz/scenes/SceneCommon.hpp:64-69` |
| Motion | `uEnergyRel`, `uBassRel`, `uMidRel`, `uTrebRel`, `uMotionBright`, `uHarmony`, `uKeyHue`, `uKeyStrength`, `uBarOsc`, `uOrbit`, `uDrift`, `uBreath`, `uFlowPhase`, `uFlowTime`, `uSoundTimeSped` | `lib_scene_motion.glsl:43-96` | `ShaderScene.cpp:229-249` (from `viz/MotionField`) |
| Motion, legacy | `uMoveDir`, `uSpawnSeed`, `uSpawnAge`, `uFormPhase` | `lib_scene_motion.glsl:109-118` | `ShaderScene.cpp:254-257`, always the constants (1,0), 0, 1000, 0 |
| Camera | `uCamPos`, `uCamBasis`, `uCamFocal`, `uCamTravel`, `uCamDolly` | `lib_camera.glsl:17-25` | `ShaderScene.cpp:277-287` (from `viz/CameraRig`); a program that has `uCamBasis` is treated as drawing the camera (`ShaderScene.cpp:388,420`) |
| Sound | `uSound[29]`, `uSoundTex` (R32F 128 x 4), `uSoundDrive`, `uSoundEvents`, `uSoundCounts` | `lib_sound.glsl:17-28` | `core/viz/SoundUniforms.cpp:40-58` (unit 3 for ShaderScene, `ShaderScene.cpp:157`) |
| Morph World | `uWorld[24]`, `uWorldShapes`, `uWorldTex` | `lib_world.glsl:70,185-186` | `ShaderScene.cpp:328-342` (units 4, 5) |
| Pool | `uPoolWaves[8]`, `uPoolRings[3]`, `uPoolWakes[6]`, `uPoolLook`, `uPoolView` | `pool_frag.glsl:28-32` | `ShaderScene.cpp:357-367` |

Feedback textures are not part of the style contract: styles never sample the previous frame. The feedback passes own them: `trail_warp_frag` (`uPrev`, `uDecay`, `uScale`, `uWarp`, `uTime`; set in `core/viz/TrailPass.cpp`), `liquid_frag` (`uPrev`, `uStyle`, `uBorn`, `uVel` plus `lib_feedback`'s 12; set in `core/viz/liquid/LiquidPass.cpp`).

Cross-checks done by script (string literals `"u..."` in the owning `.cpp` against declarations in the shader with its includes resolved):

- ShaderScene + SoundUniforms against `lib_scene_uniforms`, `lib_palette`, `lib_scene_motion`, `lib_camera`, `lib_world`, `lib_sound`, `pool_frag`: exact match both ways (88 names).
- Life, Myco, Liquid, MilkDrop post, Trail, UI blur: exact match both ways.
- Silk: the code names `uSimState` and `uSimSize` (`core/viz/compute/SimGlsl.hpp:20-21`), which no asset declares. They are declared in the preamble `SimGlsl.cpp` generates around `silk_step.glsl`, so this is not a mismatch (I did not read `SimGlsl.cpp` beyond the function signatures).

Flags:

- **Set every frame, read by no shader in either half:** `uBassRel`, `uMidRel`, `uTrebRel` (`lib_scene_motion.glsl:45-49`, set `ShaderScene.cpp:236-238`). `uEnergyRel` has one reader (`lucid_vortex_frag.glsl`).
- **Declared and set, but frozen at a constant:** `uMoveDir`, `uSpawnSeed`, `uSpawnAge`, `uFormPhase`. Still read by `lib_scene_motion` helpers (`flowOffset`, `fluidMotes`, `fluidWarp3`), `lib_dmt`, `curl_bloom`, `merkaba_grid`, `nectar_flow`, `orb_lattice`, `rod_tunnel`, `spiral_eye`.
- **Set by code, declared by no shader asset:** `uBeat` (`core/viz/fluid/FluidSim.cpp:501`; fluid shaders are the other half's scope) and `uMax` (`core/viz/frame/CastTee.cpp:322`; CastTee compiles a source of its own, not an asset, `CastTee.cpp:278-305`).
- **69 of the 119 `SND_*` names are read by no shader in either half**, including every tempo and beat field (`SND_BPM`, `SND_BEAT*`, `SND_BAR_*`, `SND_PHRASE*`), the one-frame triggers (`SND_KICK`, `SND_SNARE`, `SND_HAT`, `SND_ONSET`, `SND_HARD_CUT`, `SND_DROP`, `SND_SECTION_CHANGE`), the key and scale fields and the loudness fields. They cost nothing (one `uSound[29]` upload) but the macro list is wider than its use.
- Uniform names are not unique across programs: `uZoom`, `uWarp` mean the view zoom and warp in `lib_scene_uniforms` and MilkDrop motion terms in `lib_feedback`; `uDrift` is a motion uniform in `lib_scene_motion` and a Silk step uniform in `silk_step.glsl`. A single native owner needs per-program tables, not one global name table.

### 4. Logic in shaders that duplicates code elsewhere

| GLSL | Same thing in code |
|---|---|
| `lib_sound.glsl:30-142`, the 113 scalar offsets into `uSound[29]` | The field order of `GeodeSoundFrame` (`core/api/geode_api.h`, copied raw at `core/viz/SoundUniforms.cpp:41-43`). The header says the macros are generated and `core/tests/sound_test.cpp` checks them (`lib_sound.glsl:6-7`) |
| `lib_sound.glsl:157-165`, texture rows at `0.5/4 .. 3.5/4`, 12 chroma bins | `core/viz/SoundUniforms.hpp:29-30` (`kTexWidth 128`, `kTexRows 4`), row order `SoundUniforms.cpp:44-47` |
| `lib_scene_uniforms.glsl:101` `TOUCH_MAX_POINTS 5`; repeated in `life_step_frag.glsl:77`, `myco_agent_frag.glsl:52`, `silk_step.glsl:79` | `core/viz/TouchField.hpp:10` (`kMaxPoints = 5`) and Kotlin `engine/scenes/.../render/TouchField.kt:12` (`MAX_POINTS = 5`). Five copies of one number |
| `lib_touch.glsl:19` release decay 0.55 s (comment, the wake maths depends on it) | `core/viz/TouchField.hpp:16` (`kReleaseTauSeconds = 0.55f`) |
| `lib_palette.glsl:40` `uPalStops[5]` | `core/viz/Colour.hpp:17` (`kMaxStops = 5`); note `core/viz/Palette.hpp:21` has another `kMaxStops = 16` |
| `lib_palette.glsl:43-48` stop ramp, `:52` cosine ramp `0.5 + 0.5 cos(2 pi (h + {0, .33, .67}))` | `core/viz/Colour.hpp` `ramp()` (named in the GLSL comment, `lib_palette.glsl:39`) and the cosine at `core/viz/Colour.hpp:135` |
| `lib_feedback.glsl:39-82` `uvFor()` and `fold()` | `core/viz/liquid/FeedbackMotion.hpp:167-168` and around ("term for term", `FeedbackMotion.hpp:21`) |
| `lib_camera.glsl:16,22` travel wraps at 8192; `CAM_SPIN_UNIT = 2 pi / 1024` (`:28`) | `core/viz/CameraRig.cpp:250` (`kTravelWrap`), copied again at `core/viz/world/MorphDriver.cpp:14` and `core/viz/world/WorldGenome.cpp:408` |
| `lib_hollow.glsl:65`, `lib_flame.glsl:69`, `lib_opal.glsl:63`: `*_RATE(k) = 2 pi k / 7100.0` (whole cycles per `uTime` wrap) | `core/viz/scenes/ShaderScene.hpp:48` (`kTimeWrapSeconds = 7100`), also `Renderer.hpp:60`, `FluidScene.hpp:42`, `CurlFlowScene.hpp:33`, `FluidChoreography.hpp:50` |
| `lib_foam.glsl:91-92` `FOAM_RATE(k) = 2 pi k / 3600.0` (sound clock wrap) | the sound clocks' wrap in `core/sound` (an hour, per `core/viz/Reactions.cpp:272`'s comment; the constant itself was not located) |
| `lib_sound.glsl:27` counters wrap at 4096 | `core/viz/Reactions.hpp:115` (`kCountWrap = 4096`) |
| `lib_scene_motion.glsl:67` `uDrift` wraps at 200 pi | `MotionField::kDriftWrap` (named in the comment; value not re-read) |
| `lib_sdf3.glsl:15`, `lib_dive.glsl:107`, `lib_glass.glsl:100`, `vanishing_frag.glsl:317` and the per-style `*_MAX_STEPS` (72..128) | `core/viz/scenes/ShaderScene.cpp:20-26` (`kMarchMaxSteps 128`, floor 64, scaled floor 24) and the Detail range `core/viz/Catalogue.hpp:82` |
| `lib_relief.glsl:43-50` `RELIEF_SATIN 0 .. RELIEF_INK 7` | `core/viz/scenes/AcidScene.cpp:15` (enum 0..6), `core/viz/scenes/LifeScene.cpp:17` (enum 0..7), and the Silk scene's `uLookBase/uLookAlt` ints: three native copies of the GLSL table |
| `lib_world.glsl:70` `uWorld[24]`, `:208-209` `WG_ATLAS_COLS 6`, `WG_ATLAS_ROWS 4`, and the `WG_*` slot indices | `core/viz/world/WorldGenome.hpp:156-158` (static_assert 24), `core/viz/world/WorldAssets.hpp:35-36` |
| `pool_frag.glsl:28-30` array sizes 8, 3, 6 | `core/viz/pool/PoolMath.hpp:18-21` |
| `pm_post_frag.glsl:103-108` (saturation, contrast, gamma, brightness x intensity, invert) | the same lines of `lib_scene_grade.glsl:18-27`; a second copy of the grade in GLSL. Luma weights `0.299, 0.587, 0.114` also at `app/.../ui/ColorDerive.kt:14` |
| Mirror fold `1 - abs(1 - mod(p, 2))` | three GLSL copies: `lib_feedback.glsl:81`, `trail_warp_frag.glsl:25`, `pm_post_frag.glsl:89` |
| `lib_scene_uniforms.glsl:126-209` `view()`: pixelate range 24..288 cells, drift rate 0.025, sway 0.35, zoom floor 0.05 | param ranges live in `core/viz/ParamTable.inc`; whether the composite pass repeats `view()`'s transforms was not checked (composite is the other half) |
| `lib_dive.glsl:193-200`: six fixed cosine palettes `DIVE_PA..PD`, mixed half and half with `pal()` (`:233-236`); `DIVE_SCENE_SECONDS 13` (`:114`), `DIVE_PAL_ONSETS 16` (`:116`) | no copy in code; this is scheduling and colour choice that exists only in GLSL |

### 5. Tempo rule check

No literal BPM and no tempo assumption in any file of this half.

- Grep for `bpm`, `tempo`, `beats per`, `per minute` (case-insensitive) over the 150 files: one definition, `lib_sound.glsl:104` (`SND_BPM` as a slot of `uSound`), and comments at `lib_dive.glsl:29`, `lib_neon.glsl:34`, `lib_scene_motion.glsl:30,59`, `mandelbox_cathedral_frag.glsl:56`, `truchet_flow_frag.glsl:42`. None holds a number of beats per minute.
- No shader in either half reads `SND_BPM`, `SND_BEAT_PHASE`, `SND_BEAT`, `SND_BEAT_COUNT`, `SND_BAR_PHASE` or any other tempo field.
- Literal `120` appears 12 times and never as a tempo: step ceilings (`julia_frag.glsl:79`, `mandelbox_cathedral_frag.glsl:89`, `terra_glacier_frag.glsl:43`), a specular exponent (`lib_relief.glsl:241`), sky distances (`ring_frag.glsl:178`, `ringed_planet_frag.glsl:183`, `solar_frag.glsl:118`), a hash scale (`terra_dunes_frag.glsl:125`), 120-degree arcs in comments (`lib_truchet.glsl:316`, `lucid_mirror_frag.glsl:127`, `truchet_hex_frag.glsl:18`) and `FOAM_RATE(120.0)` at `lib_foam.glsl:460`, which is 120 turns per 3600 s clock wrap (`lib_foam.glsl:91-92`).
- `truchet_flow_frag.glsl:42-43` says "the TEMPO sets the speed ... one spacing per beat" and names `sndPhase(SND_CURVED_TIME, 2.0)`: it reads an analyser clock, not a tempo number. Whether that clock really advances one unit per beat is a `core/sound` question I did not check.
- `uBarOsc` (bar phase gated by confidence) is read only by `chrys_kaleido_frag` and `chrys_lotus_frag` (first half).

### 6. Surprises

1. **Reactions are written in GLSL.** 84 of the files in this half read drum or onset envelopes directly (`SND_KICK_ENV` in 50 files, `SND_HAT_ENV` 35, `SND_ONSET_ENV` 33, `SND_SNARE_ENV` 24, `SND_HELD_SURGE` 29). What moves on a kick is decided per style in shader text, outside the reaction matrix. `lib_scene_motion.glsl:23-33` says the motion layer is "not keyed off a drum"; the styles around it are.
2. **Family colour and scheduling inside a lib.** `lib_dive` switches between six hard-coded palettes on cut and onset counts and changes scene every 13 s (`lib_dive.glsl:114-116,193-236`); `lib_glass` carries seven environments, `lib_relief` eight materials. None of this is a param the user sees.
3. **`thumbs/` is empty** apart from its README, which describes about 237 files of 10 KB. `WidgetVisual.thumbnail` (`app/.../widget/WidgetVisual.kt:84-90`) therefore always falls back.
4. **Stale comments naming Kotlin that is gone or elsewhere:** `lib_scene_uniforms.glsl:90` ("dev.geode.render.TouchField is the model"; the model is `core/viz/TouchField`), `:117` and `lib_sdf3.glsl:15,20` (`MarchBudget.forDetail()`, no such symbol in `core/`, `engine/` or `app/`), `lib_palette.glsl:10,66` (`SceneParams.PALETTES`), `pm_post_frag.glsl:99` (`FluidHue.kt`), `lib_particle_common.glsl:4` (`ParticleSceneBase`), and `hyperspace_frag` cited in `lib_touch.glsl:142`, `lib_sdf3.glsl:56`, `kifs_frag.glsl:35`, `morphogen_frag.glsl:49,143,417`, `nebula_frag.glsl:41` (no such asset). `core/viz/ProgramBinaryCache.hpp:19` refers to a `ProgramBinaryCache.kt` that does not exist.
5. **`lib_particle_shade.glsl:4` says the include resolver is one level deep on purpose**; `core/viz/ShaderSource.cpp:96-101` recurses, and `lib_scene_uniforms.glsl:30` relies on that to pull in `lib_sound`.
6. **Three families need two includes with the style's function between them** (`lib_flame` / `lib_flame_march`, `lib_opal` / `lib_opal_march`, `lib_hollow` / `lib_hollow_march`), because a forward declaration froze and crashed on some drivers (`lib_hollow_march.glsl:4-6`). Include order is part of the contract and nothing but the two tool checks enforces it.
7. **`silk_step.glsl` and `silk_show.glsl` are not complete shaders** (no `#version`, no `_frag` suffix): native wraps them, and can build the step as a compute shader (`core/viz/compute/SimGlsl.hpp:24-26`).
8. **A user shader override is compiled without include resolution** (`ShaderScene.cpp:404-412`), so a saved `customShader` must be a fully resolved source. Where the editor's starting text for a style comes from was not found in this scan (`GeodeNative.shaderOverride` returns null for a stock style, `GeodeNative.kt:493-494`).
9. **`StyleFamiliesTest` (Kotlin, JVM) and two tools (`tools/check_shaders.py`, `tools/glslcheck/check.mjs`) parse `core/viz/ShaderIncludes.inc` as text** (`ShaderIncludes.inc:3-6`): a Kotlin test knows the native include list.
10. `ShaderScene.cpp:168` carries a comment about "the bare phase ... no bundled style reads it" above `uColorShift`, which `lib_palette.glsl:45,52,53,73` reads in every style; the comment is misplaced.

### Not verified

- About 130 files were scanned by script, not read line by line (see the top). Per-style judgement of logic (section 4 and 6) is therefore from the libs, the passes and samples, not from every style.
- Sections 2 to 8 of the brief (threads, per-frame Kotlin paths, JNI data) do not apply to assets and were not written.
- `SimGlsl.cpp`, `ProgramStager.cpp` and `ProgramBinaryCache.cpp` were read only at the cited lines.
- Graphify was not used; every reference check is a grep on the working tree.


---

## 13: app tests (`app/src/test/`, `app/src/androidTest/`)

Read in full at HEAD `57fdf4a`: 102 Kotlin files under `app/src/test/` (487 `@Test`), 4 resource files under `app/src/test/resources/`, 33 Kotlin files under `app/src/androidTest/` (68 `@Test`); 13,490 lines in all. Nothing was run. Paths below are repo-relative; test paths are shortened to `test/…` = `app/src/test/java/dev/geode/…` and `androidTest/…` = `app/src/androidTest/java/dev/geode/…`; `main/…` = `app/src/main/java/dev/geode/…`; `scenes/…` = `engine/scenes/src/main/kotlin/dev/geode/…`.

Kinds: **UI** (UI/shell behaviour), **ENG** (engine logic living in Kotlin), **FMT** (saved-format compatibility), **JNI** (JNI contract or parity with native), **ARCH** (architecture/gate), **GL** (device/GL render), **helper** (no tests, support code).

## 1. Per-test-file table

### 1a. `app/src/androidTest/` (device)

| File (lines, tests) | Subject (path) | What it pins | Kind | Native host test for the same thing | Fate on port |
|---|---|---|---|---|---|
| `androidTest/BuildVariantTest.kt` (31, 2) | manifest / Gradle variant | debug package is `com.beatrealms3d.app.debug` (:21); a provider `${packageName}.presets` resolves (:26-29) | ARCH | none (not native) | stays |
| `androidTest/export/WatermarkOverlayTest.kt` (202, 4) | `main/export/WatermarkOverlay.kt:22`, `main/model/WatermarkChoice.kt:9` | On a real pbuffer ES3 context over black: tiled default lights every quadrant by > 0.005 and the whole frame by < 0.5 at 1280x720 and 720x1280 (:85-108, :198-199); `CORNER`/`BOTTOM_RIGHT` lights only the bottom-right quadrant (:111-117); `END_CARD` draws nothing at t=0 and lights the centre at `END_MS-1` of 60 000 ms (:120-125). Lit = blue channel > 24 (:172,:197) | GL + ENG (Kotlin GL drawing in the export path) | none: `core/tests/export_passes_test.cpp` mentions the watermark only as an export pass; no placement/timing test | port the three placement rules (tile coverage bounds, corner containment, end-card time window) to a native test (fakegl or a new `watermark_test.cpp`) when `WatermarkOverlay` moves to `core/`; delete this with the subject, or keep as a device smoke test driving the native overlay |
| `androidTest/journey/AppLaunchSmokeTest.kt` (44, 1) | `MainActivity`, onboarding | fresh install: "Before the visuals start" → "I understand" → "Skip tour" → "Make your music come to life" → "Close" → "Pick something to play" (:28-42) | UI | none | stays |
| `androidTest/journey/ExportJourneyTest.kt` (44, 1) | Studio › Render page | the bar's "Render" button (Role.Button) is displayed (:40-42) | UI | none | stays |
| `androidTest/journey/Journeys.kt` (201, 0) | helper: `Onboarded` rule | seeds prefs `boot_anim=false`, `AppStateCodec.KEY_SAFETY_ACKNOWLEDGED/KEY_SETUP_DONE/KEY_TUTORIAL_SEEN=true` in `GeodePrefsFiles.GENERAL` (:58-65), pins `PerformanceSettings.SAVER` (:68), start screen `PLAYER` (:71) | helper | – | stays |
| `androidTest/journey/PaywallJourneyTest.kt` (82, 2) | paywall, `FreeTier` | Silk › "Halvorsen Web" is locked and opens "Every visual style, unlocked"; Close dismisses; fake purchase unlocks and applies the style (:56-81) | UI (Pro) | none | stays |
| `androidTest/journey/PlaybackServiceTest.kt` (62, 2) | `main/playback/PlaybackService` | library root id `"root"`, children contain `tracks, albums, artists, playlists` (:37-43); a 3 s WAV plays to `STATE_ENDED` (:47-60) | UI/shell (Media3) | none | stays |
| `androidTest/journey/PlayerJourneyTest.kt` (123, 3) | Player tab | Play restarts an ended track (:45-64); Repeat names 3 distinct modes (:72-99); heart toggles and saves under the track URI in `FavouritesStore` (:107-122) | UI | none | stays |
| `androidTest/journey/ShellTabsTest.kt` (92, 2) | shell tabs, Settings list | every tab opens; Settings "Playback" → "Sleep timer", "Next group" → "Sound & equalizer"; tab bar still there after CREATED→RESUMED and 8 s (:35-87) | UI | none | stays |
| `androidTest/journey/TestTrack.kt` (129, 0) | helper | 440 Hz mono 16-bit 44.1 kHz WAV, `MediaBrowser` helpers | helper | – | stays |
| `androidTest/journey/UiTourGestures.kt` (269, 0) | helper for the tour; reads `BehaviourPrefs.DEFAULT_GESTURES` (`main/data/BehaviourPrefs.kt:112`) | gesture list (`CanvasGesture` × default action), judge table `GestureAction` → observable effect (:183-212) | helper (documents the gesture→action expectations) | none | stays |
| `androidTest/journey/UiTourKit.kt` (705, 0) | helper | reads prefs `geode-viz`/`live_state` and the `"sceneId"` JSON field by regex (:407,:512-513) to tell which style is on screen | helper (depends on the saved live-state format) | – | stays; breaks if the `live_state` JSON loses its `sceneId` field |
| `androidTest/journey/UiTourLayout.kt` (96, 0) | helper | accessibility-tree dump | helper | – | stays |
| `androidTest/journey/UiTourSetup.kt` (190, 0) | helper | tour config, seeds `CachedEntitlement(owned=true)` via `PrefsEntitlementStore` (:141-144) | helper | – | stays |
| `androidTest/journey/UiTourTest.kt` (808, 9) | whole app | best-effort walk, no assertions beyond "activity came up"; hard-codes Settings' 15 tab titles (:751-768), Customize tabs and groups (:779-791), Library tabs (:744) | UI (report tool) | none | stays; `CUSTOMIZE_TABS` (:779-791) is a hand copy of `core/viz/CustomizeMap` titles |
| `androidTest/journey/UiTourTouch.kt` (200, 0) | helper | injected touch paths | helper | – | stays |
| `androidTest/journey/VisualsJourneyTest.kt` (96, 3) | Customize, Presets | Shape › Basics "Kaleidoscope" toggles and reveals "Folds" (:57-77); Save look "Journey look" appears under Mine (:85-94) | UI (and the rule "Folds is shown only when Kaleidoscope is on") | `core/tests/customize_test.cpp` (map), not the reveal rule | stays |
| `androidTest/render/ReactSourceCatalogDeviceTest.kt` (23, 1) | `scenes/render/Reactions.kt:17` (`ReactSource`) vs `GeodeNative.reactSourceCatalog()` | Kotlin enum must equal the native catalogue line for line: `NAME|Label|GROUP|0/1` in wire order (:18-21) | JNI parity | `core/tests/reaction_text_test.cpp` / `reaction_resolve_test.cpp` (native side only) | delete with the Kotlin `ReactSource` mirror once Customize reads the native catalogue alone |
| `androidTest/store/DemoGroove.kt` (58, 0) | helper | 32 s stereo WAV of `SyntheticGroove` | helper | – | stays |
| `androidTest/store/StoreFramesTest.kt` (199, 1) | `scenes/render/bridge/NativeViz.kt:29` | tool, gated `storeframes=1`: renders 6 free styles at 2048x1000 through `NativeViz` (create, setOffscreen, surfaceCreated/Changed, setScene, setTransition("cut",0), setSoundTuning, setParams, writeAudio, render, pollError, destroy) (:127-156) | GL (tool) + JNI call-order contract | none | stays; the Kotlin frame loop here (:143-148) is a third copy of the offscreen driver and should call the native export driver once one exists |
| `androidTest/store/StoreReelTest.kt` (77, 4) | app, Pro styles | tool, gated `storereel=1`: screen-records `black_hole`, `nebula_pulsar`, `terra_volcano`, `mandelbox_cathedral` | UI (tool) | none | stays |
| `androidTest/store/StoreSetup.kt` (279, 0) | helper | writes `live_state` = `PresetStore.toJson(Preset("__live__", styleId, attack, decay, null, params))` into `GeodePrefsFiles.VIZ` (:126-130,:148-149); refuses a Pro style in a free capture via `FreeTier.freeStyleIds` (:110) | helper (uses the saved live-look format) | – | stays |
| `androidTest/store/StoreShotsTest.kt` (134, 8) | app | tool, gated `storeshots=1`: 8 screenshots; style ids `nebula_dust`, `terra_ridge`, `bulb_classic` (:128-130) | UI (tool) | none | stays |
| `androidTest/thumbs/StyleThumbnailRenderTest.kt` (287, 1) | `NativeViz`, `scenes/render/scene/Catalogue.kt:20`, `ParamScope.MARCH_DETAIL` (`scenes/render/scene/ParamScope.kt:18`) | tool, gated `thumbs=1`: every non-MilkDrop style renders 4 s warm-up + 1 frame at 512², saved 256² WebP q75; fails only if > 10 % of styles fail (:127-193,:276-285) | GL (tool) | `core/tests/visual_gate.cpp` renders on fakegl, not real GL | stays; uses the Kotlin `Catalogue.styles` and `ParamScope` (:156-169), must switch to the native catalogue read when those are deleted |
| `androidTest/thumbs/SyntheticGroove.kt` (116, 0) | helper | 93 BPM four-bar loop, 48 kHz stereo float, fixed LCG seed (:93,:110-112) | helper | – | stays (93 BPM, allowed) |
| `androidTest/ui/CrystalSliderTest.kt` (96, 5) | `main/ui/CrystalControls.kt:132` | state description is the label; 0.5..2 in 0.05 steps = 29 steps; SetProgress updates value (:50-94) | UI (a11y) | none | stays |
| `androidTest/ui/CustomizeBrowserTest.kt` (141, 4) | `main/ui/CustomizeBrowser.kt:64` over `CustomizeMapTable.sectionsOf` (`main/ui/CustomizeCategory.kt:115`, needs libgeode) | LOOK tab has groups "Basics" and "Colour detail", collapsed; `PALETTE` in LOOK/basics, `HUE_SHIFT` in LOOK/colour_detail, `MORPH` in SHAPE/basics; open-group paths are `"LOOK/basics"`, `"LOOK/colour_detail"`, `"SHAPE/basics"` (:104,:120); ENGINE has "Fluid: flow" and no Basics (:124-130) | UI + JNI (reads the native map) | `core/tests/customize_test.cpp` + `core/tests/customize.txt` | stays; the open-group path strings are saved (CustomizeGroupsStore) and must keep their `CATEGORY/section` shape |
| `androidTest/ui/DockedMiniPlayerTest.kt` (56, 1) | `main/ui/ShellChrome.kt:308` | height = `DOCKED_PLAYER_HEIGHT`, three ≥48 dp buttons | UI | none | stays |
| `androidTest/ui/LibraryEmptyTest.kt` (57, 1) | `main/ui/LibraryEmpty.kt:49` | buttons call their lambdas | UI | none | stays |
| `androidTest/ui/PresetRowTest.kt` (70, 2) | `main/ui/PresetsPage.kt:591` | tap applies, long press/custom action "Preset actions" opens menu; state "Applied, In the visual playlist" (:58-68) | UI | none | stays |
| `androidTest/ui/ReactivityTabTest.kt` (200, 7) | `main/ui/CustomizeEvents.kt:78` (`EventsTab`), `main/ui/CustomizeAdvanced.kt:116` (`ReactionsSection`), `scenes/render/scene/BuildStyleReactionsUseCase.kt:26`, `scenes/render/EventRecipe.kt:16`, `scenes/render/Reactions.kt:206` (`HandReactions`) | routes shown = style DNA for (`SceneIds.KIFS`, seed 0) until edited; "Shuffle reactions" sets `reactSeed`+1 and `reactions=null`; "Add" appends one route to the DNA list; editing mode to `ReactMode.SPIN` changes only that route; "Remove" drops it; a recipe chip adds `routesFor(sceneId)` and `toggle` takes them off (:102-194) | UI + ENG (`EventRecipe.routesFor/isOn/fits/toggle` and the DNA use case decide reactions in Kotlin) | `core/tests/reaction_resolve_test.cpp` (DNA resolve), nothing for recipes | stays as a UI test; port recipe cases (`routesFor`, `isOn`, `fits`, `toggle`) to a native test when `EventRecipe` moves; the DNA calls must read native |
| `androidTest/ui/SceneTabLayersTest.kt` (63, 1) | `main/ui/CustomizeAdvanced.kt:417` (`LayersPage`), `main/ui/EnginePlumbing.kt:37` (`LayersBus`) | "Layers enabled" toggles `LayersBus.state.enabled`; search "layer" still finds it | UI | `core/tests/layer_state_test.cpp` (state, native) | stays |
| `androidTest/ui/VisualsTabsAndPresetsToolbarTest.kt` (109, 3) | `main/ui/VisualsSheet.kt:317`, `main/ui/PresetsPage.kt:405` | tab labels fit at `PANEL_MIN - 48 dp`; "Save look" one line at 336 dp, no overflow at 2x font | UI | none | stays |

### 1b. `app/src/test/` (JVM / Robolectric)

| File (lines, tests) | Subject (path) | What it pins | Kind | Native host test for the same thing | Fate on port |
|---|---|---|---|---|---|
| `test/ExportTimeLimitTest.kt` (32, 3) | `main/ExportTimeLimit.kt:19` | `reached(running, cancel, nowMs)` always cancels; leaves a mark only if running; `justReached` true for `WINDOW_MS - 1`, false at `WINDOW_MS` | UI/shell (service lifecycle) | none | stays |
| `test/ForegroundStartTest.kt` (29, 2) | `startedInForeground` (`main/ForegroundStart.kt:10`) | a throwing `start` calls `stop` once and returns false | UI/shell | none | stays |
| `test/audio/dsp/NativeDspProcessorTest.kt` (49, 4) | `main/audio/dsp/NativeDspProcessor.kt:50` | `runs(encoding, channels)`: true only for PCM 16-bit or float with 1 or 2 channels; 3..8 channels and 8/24/32-bit step aside; 6-channel `configure` returns `NOT_SET`, `isActive` false (:20-47) | ENG gate rule inside a Media3 shell (which formats the native chain accepts) | `core/tests/dsp_handoff_test.cpp`, `bit_perfect_test.cpp` test the chain, not this format gate | stays while the Media3 `AudioProcessor` wrapper stays; if "which formats run" moves into `core/audio/dsp`, port the truth table there |
| `test/billing/BillingBuildTest.kt` (15, 1) | `main/billing/BillingBuild.kt:10` | fake billing needs flag AND debuggable | UI/shell (billing) | – | stays |
| `test/billing/CachedEntitlementTest.kt` (137, 12) | `main/billing/EntitlementStore.kt:24` (`CachedEntitlement`, `SignedPurchase`, `PlayAnswer`) | signed purchase vouches with no expiry; re-verified on read; `FullList(null)` starts a wait, revoke after `REVOKE_GRACE_MS`; `NoAnswer`/`Update(null)` never revoke; `isCompletedPro` needs `productId`/`productIds` = `beatrealms3d_pro` and `purchaseState` 0 | UI/shell (billing) + FMT (purchase JSON) | – | stays |
| `test/billing/FakeBillingRepositoryTest.kt` (34, 1) | `main/billing/FakeBillingRepository.kt:33` | cancelled purchase returns to `Idle` | shell | – | stays |
| `test/billing/PurchaseVerifierTest.kt` (80, 6) | `main/billing/PurchaseVerifier.kt:25` | SHA1withRSA verify; blank/bad key trusts nothing; `packageName` must match install; `allowUnsigned` only without a key | shell | – | stays |
| `test/billing/StoredTrialRepositoryTest.kt` (122, 3) | `main/billing/StoredTrialRepository.kt:80` | trial = `FreeTier.TRIAL_DURATION_MS` (one hour) of wall clock, re-read each minute; clock set back ends it for good | shell (Pro) | – | stays |
| `test/cast/RequestLineTest.kt` (155, 11) | `main/cast/RequestLine.kt:37` (`RequestLineReader`), `:130` (`PerAddressLimit`) | HTTP request line + headers to the blank line; `MAX_LINE_BYTES` exact kept, +10 → `TooLong`; headers over `MAX_HEADER_BYTES` → `HeadersTooLarge`; overall deadline → `Dropped`; `readBody` to Content-Length or null; `PerAddressLimit(2)` per address | ENG (cast HTTP server parsing on a socket thread, off main) | partly: `core/tests/link_share_test.cpp:322-333` pins the native Content-Length read (cap 16384, two lengths, chunked refused) and `:46-129` the routes and response heads; no native test reads a request line from a socket with a deadline | port all 11 cases to a native test (`core/cast` request-line reader) when the cast server moves; delete with `RequestLine.kt` |
| `test/data/BackupRulesCoverageTest.kt` (84, 5) | `app/src/main/res/xml/backup_rules.xml`, `data_extraction_rules.xml`, `EditorProjectStore.TAKES_DIR/PROJECTS_DIR` | cloud rules exclude `TAKES_DIR`, `milk/textures`, `milk/generated`, `crash-latest.txt`, never `projects`, no `<include>`; device-transfer excludes only `crash-latest.txt` | ARCH + FMT (what is backed up) | – | stays |
| `test/data/BuiltInPresetsTest.kt` (77, 3) | `main/data/BuiltInPresets.kt:15` (`parse`) | reader of native text `presets|1|n|m`, `look|Name|attack|decay|k=v,…`, `preset|Name|sceneId|Look`; values land on `SceneParams` fields over defaults; presets share a look instance; unknown param throws `IllegalStateException` | JNI contract (text format from `geode_catalogue_presets_text`) | `core/tests/presets_test.cpp` (native emits the table and it reads back) | stays as long as Kotlin parses the text into `SceneParams`; delete when presets are applied natively by name |
| `test/data/ByteBudgetCacheTest.kt` (127, 7) | `main/data/ByteBudgetCache.kt:23` | byte-budget LRU, remembered misses (`maxMisses`), single decode under concurrency | shell (artwork cache) | – | stays |
| `test/data/DeviceTrackTagTest.kt` (23, 3) | `main/data/DeviceTrackQuery.kt:23` (`tagOr`) | `"<unknown>"`, null, blank → fallback | shell | – | stays |
| `test/data/EditorProjectStoreFoldersTest.kt` (51, 3) | `main/data/EditorProjectStore.kt:34` | any `ProjectId` string (`""`, `.`, `..`, `../x`, `a/b`, …) maps to one folder directly under `projects`; delete never escapes | shell + FMT (folder naming of saved projects) | – | stays |
| `test/data/ExportFpsTest.kt` (15, 1) | `main/data/VideoTemplate.kt:373` (`TemplateExport.resolvedFps`) | authored fps snaps to nearest of 24/25/30/50/60: 0→24, 27→25, 40→30, 45→50, 55→50, 120→60 | ENG (export maths in Kotlin) + FMT (template `fps`) | `core/tests/output_test.cpp` pins the encoder fps retry (per the test's own comment :6), `encoder_ladder_test.cpp`; grep found no snap-to-offered-rate case | port the 11-row table to `core/tests/output_test.cpp`; delete with `resolvedFps` |
| `test/data/ExportPresetFillTest.kt` (58, 3) | `main/data/ExportPrefs.kt:91` (`ExportDefaults.filledFrom`), `main/export/ExportPresets.kt:18` | a platform preset sets only quality, fps, ratio, loopSafe; codec and loudness target kept; every `ExportPresets.ALL` fills fps in `EXPORT_FPS_OPTIONS` | ENG table (export presets) + shell rule | none found | port the "every preset is an offered combination" case if `ExportPresets` moves to `core/output`; the fill rule can stay Kotlin |
| `test/data/FavouritesRepositoryTest.kt` (73, 3) | `main/data/FavouritesRepository.kt:23`, `FavouritesStore.kt:7` | toggles saved under track URI; external writes observed; newest first | shell + FMT | – | stays |
| `test/data/GuiSettingsCodecsTest.kt` (189, 7) | `main/data/AppState.kt:33` (`AppStateCodec`), `GeneralSettingsCodec`, `VisualsSettingsCodec`, `SoundInputSettingsCodec`, `LibrarySettingsCodec` | legacy keys still read and are folded away on next save: `KEY_INTENT="LISTENING"` → setupDone; `KEY_TUTORIAL_SEEN=true` → every `TourChapter` done, key removed; `KEY_REDUCED_MOTION` dropped; `KEY_AUTO_ENTER_PIP_OFF_DEFAULT=false` ignored; `KEY_WHITE_FONT=true` → `FontColorChoice.WHITE_ARGB` | FMT (prefs migration) | – | stays |
| `test/data/LibraryPrefsStoreTest.kt` (38, 3) | `LibraryPrefsStore` | key `"min_track_seconds"`, default 30, negative → 0 | FMT + shell | – | stays |
| `test/data/MacroEditorTableTest.kt` (136, 12) | `MacroEditorTable.parse`, `MacroLiveTable.parse`, `MacroControlsTable.parse`, `MacroStore.knobsOf` | Kotlin reader of native text against the shared fixture `core/tests/macro_editor.txt` (system property `MacroEditorTable.TEXT_FILE_PROPERTY`): maxKnobs 8, maxLinks 12, maxNameBytes 24; knob order `Shine, Motion, Colour, Depth, Mine`; curves `LINEAR, EASE_IN, EASE_OUT, S_CURVE`; live rows `mod|id|value|sources|lo|hi`; controls `macrocontrols|1` / `control|Label|TARGET|CATEGORY|ids`; bank `macrobank|1|6` / `knob|5|0.25|Mine|` / `link|ZOOM|0.25|-0.25`; other version → empty state | JNI contract (shared fixture) + FMT (bank text) | `core/tests/macro_bank_test.cpp` keeps `macro_editor.txt` equal to what C++ emits | stays (reader of native text); the fixture is the two-sided contract |
| `test/data/MacroStoreTest.kt` (145, 7) | `MacroStore`, `MacroBankCodec` | bank saved under prefs key `"macro_bank"` as native text; edit pushes then saves; drag saves once at commit; draft not saved until `saveDraft`; `RETIRED_KEYS` removed on load | FMT + shell | `core/tests/macro_bank_test.cpp` (the bank text itself) | stays |
| `test/data/MemoryPrefs.kt` (97, 0) | helper | in-memory `SharedPreferences` | helper | – | stays |
| `test/data/MinTrackLengthTest.kt` (41, 5) | `MinTrackLength` | default 30 s; `keeps(ms, s)`; unknown length (≤0) kept | shell (library filter) | `core/tests/library_search_test.cpp` (search, not this) | stays |
| `test/data/MusicPlaylistStoreTest.kt` (59, 3) | `MusicPlaylistStore` | legacy file `old-name.json` with `{"name":"Road","tracks":["a"]}` is renamed `Road.json` on first read/write, not in the constructor | FMT (playlist files) | – | stays |
| `test/data/NoteResponseStoreTest.kt` (167, 4) | `NoteResponseStore`, `NoteResponseCodec` | default on; saved `KEY_ENABLED` wins; pushed to the engine once per change | FMT + shell | `core/tests/note_response_test.cpp` (the response itself) | stays |
| `test/data/NotificationButtonTest.kt` (37, 4) | `NotificationButton` | order SHUFFLE, REPEAT, FAVOURITE; room for `ROOM` (two) | shell | – | stays |
| `test/data/PendingPublishTest.kt` (43, 3) | `publishPending` | retry once, delete row on failure | shell (MediaStore) | – | stays |
| `test/data/PlayerPrefsCrossfadeTest.kt` (36, 4) | `PlayerPrefs` | crossfade 0 default, clamp to `MAX_CROSSFADE_MS` = 12 000, whole seconds, NaN → 0 | ENG clamp of an audio parameter in Kotlin + FMT | `core/tests/gain_ramp_test.cpp` (ramp), not the clamp | port the clamp/rounding cases to native if crossfade's range moves to `core/audio`; else stays |
| `test/data/PresetGoldenTest.kt` (159, 6) | `main/data/PresetStore.kt:25` (`fromJson`, `toJson`, `paramsToJson`), `scenes/render/bridge/SceneParamsCodec` (`pack`), `scenes/render/scene/ParamSchema` (needs native schema text), `SceneParams` | Golden files `presets/current.json`, `legacy.json`, `hostile.json`. current round-trips to the same keys, values and JSON types; written keys = live (non-retired) `ParamSchema` names + `customPaletteId`, `customPalette2Id`; `paletteStops` `[0xFF8800,0x2233FF,0x10E0A0,0xFFFFFF,0x000000]` survive; envelope-only preset = defaults; legacy fills defaults, 20 retired names (:91-110) are not read or written and their frame slot holds the default; `sceneId "water"` opens as `"pool"` and `waterDepth` is dropped; `beatFlash` not written; hostile clamps: attack to `ATTACK_RANGE` end, decay to `DECAY_RANGE` start, speed 4, zoom 0.3, symmetry 16, palette 0, particleShape 2, reactSeed 16 777 215, paletteLut 4, brightness 1.875, bloom 0.625, intensity 2, cymaticsFundamental 40, flowCurl 50; non-finite/wrong-type → default; circular `colorShift` 1.5 not clamped | FMT (preset and shared-link file format) + ENG (clamp/default/retire rules run through Kotlin over the native schema) | `core/tests/params_schema_test.cpp` + `params_schema.txt` (schema rows, ranges), `params_test.cpp`, `presets_test.cpp`; none reads these three JSON goldens | the three JSON files become the native compatibility fixtures: port every case to a native preset-JSON reader test (new, e.g. `core/tests/preset_json_test.cpp`) when `PresetStore`'s codec moves; keep a thin Kotlin round-trip test only if Kotlin still stores the string |
| `test/data/ReadCapTest.kt` (41, 4) | `ReadCap` | byte caps for `LYRICS`, `MILK`, `PRESET`, `CUBE` reads; one byte over throws `IOException` | shell (input safety) | – | stays |
| `test/data/RecordPrefsStoreTest.kt` (66, 3) | `RecordPrefsStore`, `RecordPrefs` | keys `record_quality`, `record_fps`, `record_max_sec`; unset follows `ExportDefaults` (default FHD1080/30, no limit); `"UHD8K"`/120 fall back; max length snapped by the injected native snap; negative → 0 | FMT + shell | `core/tests/record_plan_test.cpp` (lengths and snap, named at :12-13) | stays |
| `test/data/RecordSaveStoreTest.kt` (44, 4) | `RecordSaveStore` | file `geode-record`, key `save_target`, value `"library"` | FMT | – | stays |
| `test/data/SharedLinkNumbersTest.kt` (70, 4) | `PresetStore.finiteOr/finiteIn` (`main/data/PresetStore.kt`), `TemplateFormat.decode/encode` | NaN, ±Inf, `1e39`, `1e999` → fallback/default; attack/decay clamp to `ATTACK_RANGE`/`DECAY_RANGE`; template with overflowing numbers parses to `TemplateLook.DEFAULT_ATTACK`, `TemplateLayout.DEFAULT_ARTWORK_SCALE`, `TextSlot.DEFAULT_SIZE_SP` and re-encodes | FMT (hostile shared links) + ENG (clamps) | none (`core/tests/link_share_test.cpp` is the cast link, not preset links) | port the number cases with `PresetGoldenTest`; template cases stay with `TemplateFormat` (Kotlin, org.json) unless it moves |
| `test/data/StudioPrefsStoreTest.kt` (50, 4) | `StudioPrefsStore` | keys `studio_last_tab` (`"MY_VIDEOS"`), `studio_keep_as_project` (tri-state) | FMT | – | stays |
| `test/data/SystemMotionTest.kt` (21, 2) | `SystemMotion.reducedAt` | reduced motion iff animator scale == 0 | shell | – | stays |
| `test/data/TemplateStyleGateTest.kt` (36, 3) | `TemplateLook.toPreset`, `main/pro/FeatureGate` | template applies its `sceneId`; `SceneIds.DEFAULT` (silk) is Pro, `FreeTier.FALLBACK_STYLE_ID` free | shell (Pro gate) | – | stays |
| `test/data/TouchAndDreamPrefsTest.kt` (79, 6) | `VisualsSettingsCodec`, `BehaviourCodec` | defaults: touchTransform on, smear off at strength 1.0; keys `KEY_TOUCH_TRANSFORM`, `KEY_TOUCH_SMEAR`, `KEY_TOUCH_SMEAR_STRENGTH`, `behaviour_gesture_PINCH_TWIST` = `"TOUCH_PLAY"`, `behaviour_dream_hear_other_apps`; `PINCH_TWIST` default `OFF` | FMT | `core/tests/touch_transform_test.cpp`, `touch_smear_test.cpp` (behaviour, named at :15-16) | stays |
| `test/data/WatermarkPrefsCodecTest.kt` (30, 2) | `WatermarkPrefsCodec`, `WatermarkChoice` | default `TILED`, `proAdds=false`; `required` is never stored | FMT | – | stays |
| `test/data/backup/BackupArchiveTest.kt` (268, 19) | `main/data/backup/` (`BackupArchive`, `BackupManifest`, `BackupFormat`, `BackupMigrations`, `PrefsJson`) | manifest encode/decode; newer version → `NEWER_VERSION`; wrong `FORMAT_ID` → `NOT_A_BACKUP`; zip-slip/absolute → `UNSAFE_PATH`; allowlist → sections (`presets/` and `prefs/geode-mod.json` CREATIVE, `history.json` and `prefs/geode-favourites.json` MUSIC, `milk/textures/` MILK, `prefs/geode-prefs.json` and `prefs/geode-wallpaper.json` SETTINGS; `takes/`, `analysis/`, `crash-latest.txt`, `milk/generated/`, `milk-builtin/`, `prefs/geode-widget.json`, `session.json` refused); SHA-256 mismatch/missing/extra → `DAMAGED`; size/count → `TOO_LARGE`; migrations in order, gap = damage; prefs JSON keeps String/Int/Long/Float/Boolean/Set types; key rules (`gui_tutorial_seen`, `roots` device-local; `macro_bank` CREATIVE; `macro_knobs`, `macro_locks`, `macro_presets` nowhere; `mix_refused` nowhere); file name `beatrealms-backup-20261030.zip` | FMT (backup format) | none (Kotlin data layer) | stays; the backup must keep carrying whatever file/keys the native port reads |
| `test/data/prefs/BehaviourRepositoryTest.kt` (82, 4) | `BehaviourCodec`, `PrefsSettingsRepository` | keys/values `behaviour_start_screen`=`"LAST_USED"`, `behaviour_notification_buttons`={`"FAVOURITE"`}, `behaviour_gesture_SWIPE_UP`=`"PLAY_PAUSE"`, `behaviour_dream_fps`=24, `behaviour_last_screen`; unknown enum names read as defaults (`"SHUFFLE_FOREVER"` → `QueueEndAction.STOP`) | FMT | – | stays (`behaviour_dream_fps` is an engine value stored by the shell) |
| `test/data/prefs/PrefsSchemaTest.kt` (363, 8) | every prefs owner: 30 `Owner` rows (:90-200) incl. `main/data/LfoStore.kt`, `main/ui/ModulationController.kt`, `main/audio/AudioFxController.kt`, `scenes/render/MilkPresetMemory.kt`, `main/wallpaper/WallpaperPrefs.kt`, `main/widget/WidgetState.kt`, `engine/audio-core` `SoundTuning` | each store writes exactly its declared `PrefKey`s with their types; no key declared twice; backup class = what `BackupFormat.sectionOfKey` does; `LfoStore.KEYS`, `MacroStore.KEYS/RETIRED_KEYS`, `RecordPrefsStore.KEYS`, `PerformanceStore.KEYS` equal their `PREF_KEYS`; every Kotlin file under `app/src/main` and `engine/*/src/main` that imports `SharedPreferences` or calls `getSharedPreferences(` is an owner or one of 10 pass-throughs (:217-229, scanned from source at :327-337); `SoundTuning.fromFloats(FloatArray(SoundTuning.FLOATS))` is stored by `SoundInputSettingsCodec` (:96-97); `LfoStore` is built with `encodeReactions = { "reactions/2" }` (:125,:171) | ARCH + FMT (the full prefs schema) | none | stays; must be edited in the same PR whenever a port deletes or moves a prefs-touching Kotlin file (the source scan fails otherwise) |
| `test/data/prefs/PrefsSettingsRepositoryTest.kt` (123, 6) | `PrefsSettingsRepository`, `PlayerPrefsCodec` | load-before-update, atomic ordered writes, normalize before publish; `PlayerPrefsCodec` drops retired keys `native_engine`, `crossfade_ms`, `gapless`; speed clamps to `PlayerPrefs.MAX_SPEED` | FMT + shell | – | stays |
| `test/data/prefs/ProcessStoresRepositoryTest.kt` (99, 4) | `StyleShelfStore`, `CustomizeGroupsStore`, `AutoDirectorCodec` | keys `style_favourites` (set), `style_recents` = `"plasma,tunnel"`, `customize_open_groups` = {`"SHAPE/basics"`}, `auto_director_enabled`, `auto_director_reach`; default reach `AutoDirectorStore.DEFAULT_REACH`; kept set `AutoDirectorStore.KEY_KEPT` in the modulation file holds param names and `#CATEGORY` tokens (`"speed"`, `"#CAMERA"`) | FMT | `core/tests/director_reach_test.cpp` (what reach does) | stays; the kept-set tokens are read by native Director and must keep their spelling |
| `test/data/prefs/RecordingPrefs.kt` (64, 0) | helper | recording in-memory prefs | helper | – | stays |
| `test/export/ChapterMarkersTest.kt` (32, 2) | `main/export/ChapterMarkers` (`markerTitle`) | blank marker name → caller's 1-based title; `LEAD_IN` → caller's lead-in title | shell (strings) | `core/tests/output_test.cpp` (spans, named at :10-11) | stays |
| `test/export/ExportRunTest.kt` (63, 2) | `main/export/ExportRun` | `requestCancel` cancels the job and raises `cancelRequested` (the flag the Kotlin frame loops poll); `begin` resets it | ENG driver control (export job lifetime in Kotlin) | `core/tests/recorder_test.cpp`, `output_test.cpp` do not cover cancel | stays while the job is a Kotlin coroutine; when the export loop is native, port "cancel flag stops the loop, a new run starts clear" to a native export-driver test |
| `test/export/ExportServiceCancelTest.kt` (29, 2) | `routeCommand`, `ExportService.ACTION_CANCEL` | only the cancel action calls cancel | shell | – | stays |
| `test/export/PcmResamplerTest.kt` (19, 1) | `main/export/PcmResampler` | equal rates = passthrough without native; `process(…, 2)` returns the first 2 frames | ENG shim (Kotlin passthrough before the native resampler) | `core/tests/resampler_test.cpp`, `export_pcm_test.cpp` | delete with `PcmResampler.kt` once the native resampler owns the equal-rate case; add an equal-rate case to `resampler_test.cpp` if missing |
| `test/export/RenderBatchTest.kt` (92, 7) | `main/export/` `RenderScope.tracksFor`, `RenderBatch`, `BatchOutcome` | scope → track list (dedupe, order); failed track skipped; `cancelRest`; progress = (done + share)/n, clamped | ENG (export scheduling: which tracks, progress maths) | none found | port to a native batch-plan test in `core/output` when the batch moves; else stays as shell queueing |
| `test/model/ControlRangeTest.kt` (52, 3) | `main/model/ControlRange` (`of`, `CAPS`, `FLOORS`) over `ParamSchema` (reads `core/tests/params_schema.txt`) | ranges: speed 0.05..4, flowCurl 0..50, cymaticsModes 1..8, endlessZoomSpeed -1.2..1.2; UI caps: brightness and intensity 0.2..1.25, bloom 0..0.25, camFly -2..2; every control range inside its schema range; each cap/floor strictly inside | ENG (slider range and cap table in Kotlin) + JNI (shared schema fixture) | `core/tests/params_schema_test.cpp` (schema ranges); the caps (1.25, 0.25, ±2) are Kotlin-only | port `CAPS`/`FLOORS` into the native schema as a UI-range column and pin them in `params_schema_test.cpp`; delete with `ControlRange` |
| `test/model/KnobSizeTest.kt` (13, 1) | `KnobSize.fromName` | unknown → `SMALL` | FMT (enum name) | – | stays |
| `test/model/LibraryChoicesTest.kt` (24, 2) | `LibraryStartTab`, `TrackTap` | unknown → `TRACKS`, `PLAY_LIST` | FMT | – | stays |
| `test/model/OpenedAudioTest.kt` (132, 12) | `main/model/OpenedAudio` | intent routing, provider refusal, display-name sanitising (bidi/control dropped, capped at `MAX_NAME_CODE_POINTS`) | shell (intent safety) | – | stays |
| `test/model/PresetLinkTest.kt` (70, 9) | `main/model/PresetLink` | `geode://preset/<base64 gzip>`; case-insensitive; fragment/query stripped; `MAX_LINK_LENGTH` cap; bad payload → null; `findIn` text | FMT (shared link envelope) | none: `core/tests/link_share_test.cpp` is the Show-on-a-computer link (`core/viz/frame/LinkShare`), not `geode://preset`; grep for `geode://`, base64 or gzip in `core/tests` finds nothing | stays unless link decode moves; the envelope must keep decoding |
| `test/model/PresetShelfTest.kt` (87, 8) | `PresetFiltering`, `PresetFilter`, `PresetEntry` | list order (mine first, then built-ins for this style), filters, rename errors, `cleanName` replaces `·` with `-`, `uniqueName` | UI | – | stays |
| `test/model/ScreenRotationTest.kt` (17, 2) | `ScreenRotation.fromName` | unknown → `FOLLOW` | FMT | – | stays |
| `test/model/StyleLookTest.kt` (19, 1) | `main/model/StyleLook` vs `Catalogue.styles[].look` (reads `core/tests/catalogue.txt`) | the set of `StyleLook` enum names equals the set of look names in the native catalogue | JNI parity (enum mirror) | `core/tests/catalogue_test.cpp` keeps `catalogue.txt` | delete with the `StyleLook` mirror when the Styles page groups by the native look column directly; else stays |
| `test/model/StyleShelfTest.kt` (54, 5) | `main/model/StyleShelf` | recents newest first, no repeats, `RECENT_LIMIT`; favourites toggle; `randomFamilyOnly`; `yours(order, onScreen)` = on screen, favourites, recents, each once, unknown ids dropped | UI + FMT (shelf) | `core/tests/style_pick_test.cpp` (the random pick itself) | stays |
| `test/model/TabLabelsTest.kt` (32, 3) | `TabLabels` | `AUTO/ALWAYS/SELECTED/NEVER.shows`; unknown name → `AUTO` | UI + FMT | – | stays |
| `test/model/TourTest.kt` (236, 23) | `main/model/Tour`, `TourProgress`, `TourChapter`, `TourSpot` | tour step rules; `placeCard` geometry; `TourProgress.decode(done, resume, tips, legacySeen, off)`: unknown names ignored; legacy chapter names map (`WELCOME`→`START`; `PLAYER`, `LIBRARY`, `LIVE_VIEW`, `PRO`, `SETTINGS` dropped); legacy resume `"CUSTOMIZE:3"` → none; `legacySeen=true` → every chapter done | UI + FMT (tour progress strings) | – | stays |
| `test/playback/PlayRecorderTest.kt` (123, 3) | `main/playback/PlayRecorder`, `HistoryStore`, `SessionStore` | a restored paused queue is not a play; queue re-read only on timeline change; service alone keeps session (tracks, index 5, 42 000 ms) and history newest first | shell (Media3 listener, storage) | – | stays |
| `test/playback/PlaybackQueueTest.kt` (111, 11) | `main/playback/PlaybackQueue` (`window`, `around`, `contextFor`, `MAX_QUEUE`) | window of `MAX_QUEUE` centred on the tapped track, clamped at both ends; browse list > device list > library; unknown track plays alone | shell (queue building for Media3) | – | stays |
| `test/playback/ResumeSourceTest.kt` (34, 4) | `ResumeSource.of` | null/empty → `Nothing`; index and position clamped | shell | – | stays |
| `test/playback/VoiceSearchTest.kt` (39, 3) | `VoiceSearch.of` | item with `searchQuery` and no id is a search; empty/null query = `""`; a browse id is not | shell | – | stays |
| `test/pro/AutoDirectorPolicyTest.kt` (58, 6) | `main/pro/AutoDirectorPolicy.amount(enabled, gate, positionMs, playerPlaying)`, `isTaste` | off → 0; Pro/trial → 1 always; free → 1 for position < `FREE_AUTO_DIRECTOR_TASTE_MS` (= 60 000) while the player is the source, else 0; `FREE_AUTO_DIRECTOR_TASTE_MS >= FREE_MAX_EXPORT_MS` and must equal native `director::kFreeTasteMs` | ENG (decides the Auto Director amount per frame/poll in Kotlin) + JNI parity (constant) + Pro | `core/tests/resolver_test.cpp` (`director::forExport`, named at :53-54), `director_test.cpp` | port the live truth table to `resolver_test.cpp`/`director_test.cpp` so live uses the same native rule as export; delete with the Kotlin policy, keeping only the entitlement input |
| `test/pro/FeatureGateTest.kt` (138, 11) | `main/pro/FeatureGate`, `FreeTier`, `TrialState`, `scenes/render/scene/StyleFamilies` | free = `FreeTier.freeFeatures` + `freeStyleIds`; each `StyleFamilies.all` family frees its `opener` only; free export blocked above 1080 short side (`EXPORT_4K`) or 30 fps (`EXPORT_60FPS`), resolution first; trial clock rules with `TRIAL_CLOCK_SLACK_MS`; neon: one opener free, 4 Pro; watermark preference never lifts a required mark | shell (Pro) + ENG table read (`StyleFamilies`) | `core/tests/catalogue_test.cpp` (families) | stays; `StyleFamilies` must come from the native catalogue |
| `test/pro/FreeTierCustomizeTest.kt` (22, 2) | `FreeTier.freeCustomizeControls`, `customizeSectionFree` | free controls = Basics | shell (Pro) | – | stays |
| `test/ui/CustomizeMapTableTest.kt` (104, 6) | `main/ui/CustomizeCategory.kt:115` (`CustomizeMapTable`, parses `core/tests/customize.txt`) | every control placed once; categories in `CustomizeCategory` order; every category but EXPERT and ENGINE has one Basics first with 1..4 controls; groups ≤ 8; `LOOK/basics` = Palette, Brightness, Saturation, Bloom; `MOTION/basics` = Speed, Zoom, Rotation; `CAMERA/basics` = CamPath, CamFly; `SHAPE/stirrers` = `Stirrers`, `Stirrer speed`; `ENGINE/fluid_layers` title "Fluid: layers"; `LOOK/colour_detail` title "Colour detail"; free set = union of Basics incl. `@macros`; `@reactions`, `@shader_code`, HueShift, FluidCurl are Pro | JNI contract (shared fixture) + parity (`CustomizeCategory` enum vs native categories) | `core/tests/customize_test.cpp` (byte-equal `customize.txt`, same ≤ 8 bound) | stays as the reader's test; the layout assertions duplicate `customize_test.cpp` and can go if native owns them |
| `test/ui/FakeLibrarySearcher.kt` (44, 0) | helper for `LibrarySearcher` | Kotlin stand-in for the native index | helper | `core/tests/library_search_test.cpp` | stays |
| `test/ui/GeodeAppStateTest.kt` (31, 2) | `main/ui/GeodeAppState` | navigation closes search | UI | – | stays |
| `test/ui/LibraryBrowseTest.kt` (96, 6) | `main/ui/LibraryBrowse` (`index`, `browse`, `favouriteTracks`) over `LibrarySearcher` | blank query = whole library in sort order, grouped by album/artist/folder; a search keeps the index's order and names whole artists/albums; index built lazily once; favourites in store order minus missing | UI/data (uses the native search rows) | `core/tests/library_search_test.cpp` (match and rank) | stays |
| `test/ui/LibraryDuplicatesTest.kt` (28, 1) | `main/ui/LibraryDuplicates.find` | same title+artist split by length (180 000/180 500 vs 240 000/240 900 ms) gives 2 groups with distinct keys | UI/data | none | stays |
| `test/ui/LibraryRescansTest.kt` (39, 2) | `main/data` `libraryRescans(quietMs)` | debounce: a burst = 1 rescan | shell | – | stays |
| `test/ui/LibraryViewModelTest.kt` (188, 5) | `main/ui/LibraryViewModel` | 15 000 tracks: grouping runs on the compute dispatcher; keystroke burst filters once (`QUERY_DEBOUNCE_MS`); cleared box answers at once; duplicates over the whole library; `showEmptyLibrary` only after first load | UI (ViewModel) | – | stays |
| `test/ui/MusicLibraryPlaylistEditsTest.kt` (66, 3) | `main/data/MusicLibraryRepository` | playlist edits off the caller's thread, in order, tracks deduped | shell/data | – | stays |
| `test/ui/PendingDeletesTest.kt` (106, 4) | `main/ui/PendingDeletes` | delete commits after a 4 000 ms window even if the UI scope dies; undo keeps | UI | – | stays |
| `test/ui/PermissionStatusTest.kt` (40, 4) | `statusAfterRefusal` | DENIED vs PERMANENTLY_DENIED rule | shell | – | stays |
| `test/ui/PipAspectTest.kt` (38, 4) | `pipAspectFraction`, `PIP_MIN_ASPECT`, `PIP_MAX_ASPECT` | aspect clamped to the platform range, orientation kept | shell (platform rule) | – | stays |
| `test/ui/PrivacyPolicyTest.kt` (57, 3) | `policyLines`; `app/src/main/assets/privacy_policy.md` vs `docs/privacy-policy.md` | the two files are byte-equal; markdown → plain lines; first heading "BeatRealms 3D — Privacy Policy" | ARCH + UI | – | stays |
| `test/ui/SavedPalettesTest.kt` (68, 3) | `main/ui/SavedPalettes`, `main/data/PaletteStore` (`create(name, a, b)`, `save`, `list`, `delete`) | store opened only on the palette dispatcher; save then delete land in order; `load` reads disk | shell + FMT (saved palettes round-trip through `PaletteStore`; no literal file content asserted) | `core/tests/palette_test.cpp`, `art_palette_test.cpp` (palette maths) | stays; note no test pins the saved palette file's on-disk JSON (gap for the port) |
| `test/ui/ScenePreviewTest.kt` (20, 2) | `sceneAfterPreview` | leaving a preview keeps an allowed style, drops a locked one | UI (Pro) | – | stays |
| `test/ui/SettingsMapTest.kt` (142, 7) | `SettingsGroupKey`, `SettingsSectionKey`, `SettingsRowKey` | 15 tabs in order; each row once; former group names (`BEHAVIOUR`, `VISUALS`, `SOUND_INPUT`, `HELP`, `BACKUP_ABOUT`) and 19 former section names still map to a tab | UI + FMT (saved page names) | – | stays |
| `test/ui/ShellChromeStateTest.kt` (79, 7) | `ShellChromeState` | chrome sleep/wake/held/covered timer rules | UI | – | stays |
| `test/ui/StageIconRoomTest.kt` (90, 9) | `stageIconRoom(...)` | which stage icons fit beside the sheet/panel, in px on a 412x904 phone | UI layout | – | stays |
| `test/ui/StageKeysTest.kt` (69, 6) | `stageKeyGesture(keyCode, optionsShown)` | D-pad/centre/channel keys → `CanvasGesture` and direction; every default action reachable from a key | UI (TV input) | `core/tests/stage_step_test.cpp` (the step itself) | stays |
| `test/ui/StageStyleTest.kt` (16, 1) | `stageStyleOrder()` | the Kotlin style order has no duplicates | ENG leftover (Kotlin builds the stage style order that native walks) | `core/tests/stage_step_test.cpp` (walk, wrap, locked, MilkDrop; named at :7-8) | delete with `stageStyleOrder()` when the order comes from the native catalogue; add "order has no duplicates" to `stage_step_test.cpp` or `catalogue_test.cpp` |
| `test/ui/StartDestinationTest.kt` (46, 5) | `BehaviourPrefs.startDestination()`, `GeodeDestination.startScreen` | cold start opens the chosen screen, `LAST_USED` the last one; fresh install opens Library; Settings is never recorded | UI + FMT | – | stays |
| `test/ui/StudioSectionTest.kt` (34, 4) | `StudioSection.fromName` | `"RENDER"`, `"MY_VIDEOS"`; saved `"EDIT"` → `RENDER`; unknown → null | FMT (saved tab name) | – | stays |
| `test/ui/TapRestoresOptionsTest.kt` (24, 3) | `tapRestoresOptions(chromeAwake, sheetShown)` | true only when awake and hidden | UI | – | stays |
| `test/ui/TelevisionManifestTest.kt` (84, 4) | `app/src/main/AndroidManifest.xml` | no `uses-feature` required; touchscreen, faketouch, leanback `false`; permission-implied features optional; one `LEANBACK_LAUNCHER` on `.ui.MainActivity`, banner `@drawable/tv_banner` | ARCH | – | stays |
| `test/ui/TimeLeftTest.kt` (22, 3) | `formatTimeLeft` | `"−2:30"`, `"−1:00:00"`, never below `"−0:00"`, unknown length `"0:00"` | UI | – | stays |
| `test/ui/VisualGesturesTest.kt` (80, 7) | `main/ui/VisualGestures.kt` (`gestureSafeArea`, `WindowPlacement`, `isSecondTap` :386, `swirlAttempted` :397) | a down in a system gesture strip is the system's; second tap within the slop (hypot ≤ 50 px); a swirl attempt is |turn| > π | UI (gesture recognition on the main thread; decides which gesture, not what it does) | `core/tests/touch_transform_test.cpp`, `touch_smear_test.cpp` (what touch does to the picture) | stays |
| `test/ui/VisualsSheetWidthTest.kt` (23, 3) | `sheetWidthDp`, `SHEET_MAX_WIDTH` | cap 640 dp | UI | – | stays |
| `test/ui/cast/CastButtonUiTest.kt` (54, 5) | `castButtonUi`, `castMessage`, `main/cast` `CastState`, `CastFailure` | state → string resource | UI | – | stays |
| `test/ui/cast/ComputerLinkUiTest.kt` (67, 6) | `linkRowValue`, `linkButtonLabel`, `linkSheetStatus`, `linkEndedMessage`, `LinkState`, `CastFailure.linkReason` (`main/cast/CastController.kt:22`), `GeodeNative.LINK_REASON_*` (`engine/audio-core/.../bridge/GeodeNative.kt:2586-2590`) | each `LINK_REASON_*` (STREAM, NO_LAN, BACKGROUND, NETWORK, TRIAL) has its own message, 0 reads as STREAM; `CastFailure` → link reason map; URL shape `http://3232235799:41237/<32 hex>/` | UI + JNI (constants mirrored from `core/api/geode_api.h:1414-1418`) | `core/tests/link_share_test.cpp` (state machine, link maker) | stays |
| `test/ui/pro/PaywallViewModelTest.kt` (287, 11) | `main/ui/pro/PaywallViewModel`, `GetFeatureGateUseCase` | purchase/pending/fail/cancel/restore states; trial unlocks for `TRIAL_DURATION_MS` then shows the trial-ended paywall once | UI (Pro) | – | stays |
| `test/ui/theme/GlassContrastTest.kt` (105, 7) | `GeodeGlass`, `GeodeColors`, `GeodeContrast`, `VisualAccent.from/onAccent/readable` | worst backdrop grey 0.316 (floor 0.72 over white + 5 % fill); text roles ≥ `BODY_MIN`; accents lifted to read on glass and ≥ `MIN_CONTRAST` on canvas | UI (theme colour maths for the chrome, not the visual) | `core/tests/art_palette_test.cpp` (artwork palette hues; the accent source) | stays |
| `test/ui/theme/SecondaryTextTest.kt` (34, 3) | `secondaryTextFor`, `isReadableOnCanvas` | alpha 0.78 unless that drops below `BODY_MIN` | UI | – | stays |
| `test/ui/tour/TourViewModelTest.kt` (342, 16) | `main/ui/tour/TourViewModel` | tour state holder: steps, skip, replay, tips, reset; timings `SETTLE_MS`, `OFFER_DELAY_MS`, `REPLAY_SETTLE_MS` | UI (ViewModel) | – | stays |
| `test/widget/NowPlayingWidgetTest.kt` (32, 4) | `layoutFor`, `fitWithin` | widget breakpoints; artwork fit 700x350 → 344x172 | UI (widget) | – | stays |
| `test/widget/WidgetVisualTest.kt` (76, 5) | `main/widget/WidgetVisual.render`, `WidgetState`, `ArtKey` | picture redrawn only when style, hue, span or tick changes; `Glow(style, 0.3, 0.1, tick)` vs `Thumbnail(style)` | UI (widget) + ENG-adjacent (a Kotlin-drawn "glow" picture from the art hue; Canvas, main-safe) | none | stays (RemoteViews bitmap; no NDK form needed) |
| `app/src/test/resources/presets/current.json` (177) | fixture for `PresetGoldenTest` | see section 2 | FMT fixture | – | copy to `core/tests/` as the native preset fixture |
| `app/src/test/resources/presets/legacy.json` (30) | fixture | see section 2 | FMT fixture | – | copy to `core/tests/` |
| `app/src/test/resources/presets/hostile.json` (24) | fixture | see section 2 | FMT fixture | – | copy to `core/tests/` |
| `app/src/test/resources/robolectric.properties` (3) | Robolectric config | `sdk=36`, `application=android.app.Application` (store tests must not start `GeodeApp`: Hilt, crash handler, native libraries) | config | – | stays |

Counts by primary kind (135 Kotlin files): UI/shell 74, saved-format 23, engine logic 12, JNI contract/parity 5, architecture/gate 5, device GL render 3, helpers 13.

## 2. Saved-format fixtures (the "old saved data" the tests hold)

Resource files:

- `app/src/test/resources/presets/current.json:1-177`: a full preset, name `"Golden"`, `sceneId "silk"`, `attack 0.25`, `decay 0.5`, then every key the pre-schema writer put, each off its default, e.g. `"speed": 2.03125`, `"endlessZoom": true`, `"symmetry": 7`, `"customPaletteId": "palette-a"`, `"customPalette2Id": "palette-b"`, `"fluidCurl": 25`, `"flowCurl": 12.5`, `"cymaticsFundamental": 240`, `"paletteAmount0".."paletteAmount7"`, `"reactSeed": 12345`, `"paletteStop0": 16746496`, `"paletteStop1": 2241535`, `"paletteStop2": 1106080`, `"paletteStop3": 16777215`, `"paletteStop4": 0`, `"customShader": "void main() {}"`, `"milkPreset": "golden.milk"`. Flat object: envelope fields and params share one level.
- `app/src/test/resources/presets/legacy.json:1-30`: `{"name":"Old Kyanite","sceneId":"raymarch","attack":0.05,"decay":0.3,"speed":1.5,"zoom":1.25,"rotation":0.5,"audioDrive":1.5,"turbulence":0.25,"trails":true,"trailLength":0.75,"mirror":false,"palette":3,"colorShift":0.125,"saturation":1.25,"brightness":1.125,"symmetry":8,"kaleidoscope":true,"glitch":0.75,"pulse":0.6,"shake":0.4,"beatResponse":1.9,"flash":0.6,"strobe":0.5,"formDrive":0.2,"motionOrbit":0.9,"beatFlash":0.5,"customPaletteId":"old-palette"}` (retired keys `glitch, pulse, shake, beatResponse, flash, strobe, formDrive, motionOrbit`, unknown key `beatFlash`).
- `app/src/test/resources/presets/hostile.json:1-24`: `{"name":"Hostile","sceneId":"silk","attack":40,"decay":-1,"speed":99,"zoom":-5,"rotation":1e39,"warp":-1e999,"endlessZoomSpeed":"abc","trails":"true","symmetry":40,"palette":-3,"particleShape":2.75,"reactSeed":99999999,"paletteLut":9,"brightness":1.875,"bloom":0.625,"intensity":3,"cymaticsFundamental":10,"flowCurl":75,"colorShift":1.5,"camFov":NaN}`.
- The full retired-name list a reader must skip is asserted at `test/data/PresetGoldenTest.kt:91-110` (20 names: `pulse, shake, beatResponse, flash, glitch, strobe, waterWaveSpeed, waterDamping, waterRippleStrength, waterDepth, waterSpecular, waterFlow, waterLiquid, waterLiquidFlow, waterLiquidFade, rippleOverlayEnabled, rippleOverlayStrength, rippleOverlaySpecular, formDrive, motionOrbit`).
- Shared native fixtures read by JVM tests through system properties set in `build-logic/src/main/kotlin/geode.kotlin-common.gradle.kts:61-68`: `core/tests/catalogue.txt` (`geode.catalogue`), `core/tests/customize.txt` (`geode.customize`), `core/tests/params_schema.txt` (`geode.paramsSchema`), `core/tests/mod_targets.txt` (`geode.modTargets`), `core/tests/macro_editor.txt` (`geode.macroEditor`).

Inline presets, links, templates:

- `test/data/PresetGoldenTest.kt:72`: `{"name":"e","sceneId":"silk","attack":0.1,"decay":0.2}` → default params.
- `test/data/PresetGoldenTest.kt:123`: `{"name":"w","sceneId":"water","attack":0.1,"decay":0.2,"waterDepth":0.7}` → `sceneId` reads as `"pool"`, `waterDepth` not written back.
- `test/data/SharedLinkNumbersTest.kt:34`: `{"name":"x","sceneId":"silk","attack":1e39,"decay":-1e999,"zoom":1e39,"speed":1e999,"warp":-1e999}`.
- `test/data/SharedLinkNumbersTest.kt:49-52`: template `{"format":"<TemplateFormat.FORMAT>","name":"t","look":{"attack":1e39,"decay":1e999,"params":{"zoom":1e39,"bloom":-1e999}},"layout":{"artworkScale":1e39,"safeArea":1e999},"text":{"slots":[{"sizeSp":1e39,"offsetX":-1e999}]}}`.
- `test/model/PresetLinkTest.kt:15`: link payload `{"name":"Kyanite","style":"raymarch","params":{"gain":0.75}}` inside `geode://preset/<payload>` (:29, :42, :49). Note: this payload uses `style` and nested `params`, a different shape from the flat `PresetStore` JSON; `PresetLink` only wraps a string, so the test does not pin which shape real links carry.
- `androidTest/store/StoreSetup.kt:127-129,148-149`: prefs file `geode-viz`, key `live_state`, value `PresetStore.toJson(Preset("__live__", styleId, attack, decay, null, params))`; `androidTest/journey/UiTourKit.kt:513` reads `"sceneId"` out of it by regex.

Native text formats saved or exchanged:

- Macro bank (prefs `geode-prefs`/`macro_bank`), `test/data/MacroStoreTest.kt:141-143`: `"macrobank|1|5\nknob|1|0|Glow|\nlink|BLOOM|1|0\n"`, `"macrobank|1|6\nknob|1|0|Glow|\nlink|BLOOM|1|0\nknob|5|0.5|Mine|\n"`, `"macrobank|1|6\nknob|1|0|Glow|\nlink|BLOOM|1|0\nknob|5|0|Mine|\nlink|ZOOM|0.3|0\n"`; `test/data/MacroEditorTableTest.kt:116`: `"macrobank|1|6\nknob|5|0.25|Mine|\nlink|ZOOM|0.25|-0.25\n"`; `test/data/prefs/PrefsSchemaTest.kt:123`: `"macrobank|1|1\n"`. Retired keys `macro_knobs`, `macro_locks`, `macro_presets` (`main/data/MacroStore.kt:251`).
- Macro live rows `test/data/MacroEditorTableTest.kt:52`: `"mod|3|0.625|3|-0.5|1.25\nmod|7|0.1|1|-0.3|0.3\nmod|x|1|1|0|0\nmod|9|1|1\n"`; controls table `:93`: `"macrocontrols|1\ncontrol|Bloom|BLOOM|LOOK|1,5\ncontrol|Zoom|ZOOM|MOTION|\ncontrol|bad\n"`; editor text `:122,:126`: `"macroeditor|1\nknob|x|1|1|1|0|Glow|\nknob|2|1|1|1|0|Heat|\nlink|ZOOM|a|b|1|LINEAR|0|0\nwhat|ever\n"`.
- Built-in presets text `test/data/BuiltInPresetsTest.kt:17-24`: `presets|1|2|3`, `look|Chill|0.4|0.06|speed=0.5,audioDrive=0.85,trails=1,trailLength=0.8,saturation=0.8,brightness=0.9,colorCycle=1,cycleSpeed=0.025,palette=3,sway=0.35,vignette=0.4,paramFadeSec=1.5`, `preset|julia · Chill|julia|Chill`, `look|cymatics · Chladni plate|0.35|0.45|cymaticsGeometry=1,cymaticsFundamental=82,cymaticsModes=4,cymaticsScale=1.15,cymaticsSwirl=0,palette=4,hueRange=0.6`.
- Reactions: only the version string `"reactions/2"` appears (`test/data/prefs/PrefsSchemaTest.kt:125,171`; native `core/viz/ReactionText.cpp:16`). No saved LFO, ADSR or reaction text is used as a fixture anywhere under `app/src/test`.
- Reaction source catalogue lines `NAME|Label|GROUP|0/1` (`androidTest/render/ReactSourceCatalogDeviceTest.kt:20`).

Prefs keys and stored values asserted literally:

- `geode-prefs`: `gui_safety_acknowledged`, `gui_setup_done`, `gui_intent`=`"LISTENING"`, `gui_tutorial_seen`, `gui_reduced_motion`, `gui_auto_enter_pip`, `gui_white_font`, `gui_mic_reactive`, `preset_mirror_uri`, `gui_touch_transform`, `gui_touch_smear`, `gui_touch_smear_strength`=1.6 (`test/data/GuiSettingsCodecsTest.kt:33,39,51,59,70,91-92`; `test/data/TouchAndDreamPrefsTest.kt:34,48-49`); `boot_anim` (`androidTest/journey/Journeys.kt:60`).
- `behaviour_start_screen`=`"LAST_USED"`, `behaviour_last_screen`=`"VISUALS"`, `behaviour_notification_buttons`={`"FAVOURITE"`}, `behaviour_gesture_SWIPE_UP`=`"PLAY_PAUSE"`, `behaviour_gesture_SWIRL`, `behaviour_gesture_PINCH_TWIST`=`"TOUCH_PLAY"`, `behaviour_dream_fps`=24, `behaviour_dream_hear_other_apps`, `behaviour_queue_end`=`"SHUFFLE_FOREVER"` (unknown → STOP) (`test/data/prefs/BehaviourRepositoryTest.kt:48-51,59-61,67`; `test/data/TouchAndDreamPrefsTest.kt:65,73`).
- `macro_bank` (`test/data/MacroStoreTest.kt:55`); `note_response_enabled` (`NoteResponseCodec.KEY_ENABLED`, `test/data/NoteResponseStoreTest.kt:43`); `auto_director_enabled`, `auto_director_reach`=2 (`test/data/prefs/ProcessStoresRepositoryTest.kt:89`); `geode-mod`/`locked_params` = {`"speed"`, `"#CAMERA"`} (`:96`, `main/data/AutoDirectorStore` `KEY_KEPT`).
- `geode-viz`: `style_favourites`={`"plasma"`}, `style_recents`=`"plasma,tunnel"` (`ProcessStoresRepositoryTest.kt:46-47`); `customize_open_groups`={`"SHAPE/basics"`}, tab `"LOOK"` (`:70-71`; `PrefsSchemaTest.kt:121`).
- `record_quality`=`"UHD8K"` (unknown), `record_fps`=120, `record_max_sec`=40 (`test/data/RecordPrefsStoreTest.kt:39`); file `geode-record`, `save_target`=`"library"` (`test/data/RecordSaveStoreTest.kt:16,26`).
- `studio_last_tab`=`"MY_VIDEOS"`, `studio_keep_as_project` (`test/data/StudioPrefsStoreTest.kt:29,39`); saved tab `"EDIT"` (`test/ui/StudioSectionTest.kt:21`).
- `geode-library`/`min_track_seconds` (`test/data/LibraryPrefsStoreTest.kt:26-27`); `geode-player` retired `native_engine`, `crossfade_ms`, `gapless` (`test/data/prefs/PrefsSettingsRepositoryTest.kt:109,116`).
- Tour progress strings: `"PLAYER,VISUALS,ANYWHERE,"`, `"PLAYER:2"`, `"GONE,,SWIPE"` (`test/model/TourTest.kt:143`); `"WELCOME,PLAYER,LIBRARY,CUSTOMIZE,LIVE_VIEW,PRO"`, `"CUSTOMIZE:3"` (:151); `"WELCOME,SETTINGS,STUDIO"`, `"SWIPE_STYLES,HOLD_TO_RECORD"` (:162).
- Settings page names from before the regroup (`test/ui/SettingsMapTest.kt:65,77-96`).
- Misclassified backup keys list: `test/data/prefs/PrefsSchemaTest.kt:235-248`.

Files and backup:

- Playlist file `music-playlists/old-name.json` = `{"name":"Road","tracks":["a"]}` (`test/data/MusicPlaylistStoreTest.kt:25`).
- Project folder `projects/<id>/project.json` = `{}` (`test/data/EditorProjectStoreFoldersTest.kt:21`).
- Backup: format id `"muzviz-backup"` (`main/data/backup/BackupFormat.kt:112`), version 2 (:118), `manifest.json` (:120); paths `presets/a.json` = `{"name":"Kyanite"}`, `prefs/geode-prefs.json` = `PrefsJson.encode(mapOf("gui_opacity" to 0.4f))`, `prefs/geode-mod.json`, `prefs/geode-favourites.json`, `prefs/geode-wallpaper.json`, `history.json`, `milk/textures/cloud.png` (`test/data/backup/BackupArchiveTest.kt:27,37-44,112-122`); file name `beatrealms-backup-20261030.zip` (:223).
- Purchase JSON: `{"orderId":"GPA.1","packageName":"com.beatrealms3d.app","productId":"<beatrealms3d_pro>","purchaseTime":1,"purchaseState":0,"purchaseToken":"t","quantity":1,"acknowledged":true}` (`test/billing/CachedEntitlementTest.kt:106-107`).

Not covered by any fixture in this scope (so the port has no written record here): saved LFO and ADSR JSON (`main/data/LfoStore.kt:139`), saved reaction text beyond its version, saved palettes' JSON (`main/data/PaletteStore.kt:148-157`), `live_state` content, `SoundTuning` floats, editor project JSON (`main/data/EditorProjectJson.kt:50-52`), session and history files. `engine/*/src/test` holds 13 more tests (`SoundTuningTest`, `ParamSchemaTest`, `ModTableTest`, `LookApplierTest`, `WireLayoutTest`, …) outside this scope.

## 3. Parity tests: hand-kept mirror pairs

| # | Kotlin mirror (path) | Native source (path) | Held by |
|---|---|---|---|
| 1 | `ReactSource` enum: name, label, group, isEvent, order (`scenes/render/Reactions.kt:17`; group enum `:143`) | reaction source catalogue in `core/viz/Reactions.cpp` / `Reactions.hpp`, read through `GeodeNative.reactSourceCatalog()` | `androidTest/render/ReactSourceCatalogDeviceTest.kt:18-21` (device only) |
| 2 | `StyleLook` enum names (`main/model/StyleLook.kt:18`) | look column of `core/viz/Catalogue.cpp`, via `core/tests/catalogue.txt` | `test/model/StyleLookTest.kt:16-17` |
| 3 | `CustomizeCategory` enum order and titles (`main/ui/CustomizeCategory.kt:25`) | categories of `core/viz/CustomizeMap.cpp`, via `core/tests/customize.txt` | `test/ui/CustomizeMapTableTest.kt:51`; `androidTest/ui/CustomizeBrowserTest.kt`; hand copy of titles in `androidTest/journey/UiTourTest.kt:779-791` |
| 4 | `FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS = 60_000` (`main/pro/FreeTier.kt:88`) | `director::kFreeTasteMs = 60'000` (`core/viz/Director.hpp:42`) | `test/pro/AutoDirectorPolicyTest.kt:55-56` and `core/tests/resolver_test.cpp:453` (two literals, each checked on its own side) |
| 5 | `GeodeNative.LINK_REASON_*` 1..5 (`engine/audio-core/src/main/kotlin/dev/geode/engine/bridge/GeodeNative.kt:2586-2590`) and `CastFailure.linkReason` (`main/cast/CastController.kt:22`) | `GEODE_LINK_REASON_*` (`core/api/geode_api.h:1414-1418`) | used, not compared, by `test/ui/cast/ComputerLinkUiTest.kt:52-65`; no test compares the numbers to the header |
| 6 | `ControlRange.CAPS` / `FLOORS` (`main/model/ControlRange.kt:22,35`): brightness and intensity 1.25, bloom 0.25, camFly ±2 | schema ranges in `core/viz/ParamTable.inc` via `core/tests/params_schema.txt`; the flash limits in `core/viz/VisualSafety` | `test/model/ControlRangeTest.kt:24-27,41-50` (inside-the-range only; the cap values are Kotlin's) |
| 7 | `MacroEditorTable` / `MacroLink.LINEAR` / curve names, caps 8/12/24 (`main/data/MacroEditor.kt:150`) | `core/viz/MacroBank.cpp` `editorText`, via `core/tests/macro_editor.txt` | `test/data/MacroEditorTableTest.kt:20-22,87` (reader, not a second table; the numbers are assertions about the fixture) |
| 8 | `BuiltInPresets.parse` param names = `SceneParams` field names (`main/data/BuiltInPresets.kt:29`) | `core/viz/CataloguePresets.cpp` text | `test/data/BuiltInPresetsTest.kt` (hand-written sample rows, not the native output) |
| 9 | `PresetStore` written key set (`main/data/PresetStore.kt:200`) | live rows of the native schema (`ParamSchema`, `core/tests/params_schema.txt`) | `test/data/PresetGoldenTest.kt:63-67` |
| 10 | `StyleFamilies.all` openers (`scenes/render/scene/StyleFamilies.kt:31`) and `FreeTier.freeStyleIds` | families in `core/viz/Catalogue.cpp` | `test/pro/FeatureGateTest.kt:53-61` (consistency of the two Kotlin lists; `StyleFamiliesTest` in `engine/scenes` is out of scope) |
| 11 | `stageStyleOrder()` (`main/ui/VisualsStage.kt:504`) = `Catalogue.picker` minus MilkDrop | the order `core/viz/StageStep.hpp` walks | `test/ui/StageStyleTest.kt:12-14` (no duplicates only) |
| 12 | Settings/Customize titles and gesture expectations in `androidTest/journey/UiTourTest.kt:751-791` and `UiTourGestures.kt:183-212` | app strings; `BehaviourPrefs.DEFAULT_GESTURES` | not asserted (report tool) |

## 4. Tests that name engine behaviour living in Kotlin

| Test | Kotlin production function (path:line) | What it decides |
|---|---|---|
| `test/data/ExportFpsTest.kt:10-12` | `TemplateExport.resolvedFps()` `app/src/main/java/dev/geode/data/VideoTemplate.kt:383` | snaps an authored fps to the nearest of `EXPORT_FPS_OPTIONS` |
| `test/export/RenderBatchTest.kt` | `RenderScope.tracksFor` `app/src/main/java/dev/geode/export/RenderBatch.kt:39`; `RenderBatch` `:76`; `progress` `:101` | which tracks a batch renders, batch progress |
| `test/export/ExportRunTest.kt` | `ExportRun.requestCancel` `app/src/main/java/dev/geode/export/ExportRun.kt:11,62` | the cancel flag the Kotlin frame loops poll |
| `test/export/PcmResamplerTest.kt` | `PcmResampler.isPassthrough` / `process` `app/src/main/java/dev/geode/export/PcmResampler.kt:15,25` | equal-rate passthrough, frame count cut |
| `test/data/ExportPresetFillTest.kt` | `ExportDefaults.filledFrom` `app/src/main/java/dev/geode/data/ExportPrefs.kt:91`; `ExportPresets.ALL` `app/src/main/java/dev/geode/export/ExportPresets.kt:18` | platform export table (quality, fps, ratio, loop-safe) |
| `test/model/ControlRangeTest.kt` | `ControlRange.of` `app/src/main/java/dev/geode/model/ControlRange.kt:41,44`; `CAPS` `:22`; `FLOORS` `:35` | slider range per param and the UI caps |
| `test/pro/AutoDirectorPolicyTest.kt` | `AutoDirectorPolicy.amount` `app/src/main/java/dev/geode/pro/AutoDirectorPolicy.kt:19`; `isTaste` `:33` | Auto Director amount (0 or 1) from entitlement, position and source |
| `test/ui/StageStyleTest.kt` | `stageStyleOrder()` `app/src/main/java/dev/geode/ui/VisualsStage.kt:504` | the order of styles the stage steps through |
| `test/data/PlayerPrefsCrossfadeTest.kt` | `PlayerPrefs.coerced()` `app/src/main/java/dev/geode/data/PlayerPrefs.kt:44`; `MAX_CROSSFADE_MS` `:58`; `crossfadeMsFromSeconds` `:67` | crossfade length range and rounding |
| `test/audio/dsp/NativeDspProcessorTest.kt` | `NativeDspProcessor.runs` `app/src/main/java/dev/geode/audio/dsp/NativeDspProcessor.kt:169` | which PCM formats enter the native DSP chain |
| `test/cast/RequestLineTest.kt` | `RequestLineReader.read` `app/src/main/java/dev/geode/cast/RequestLine.kt:47`; `readBody` `:75`; `PerAddressLimit` `:130` | cast HTTP server request parsing, caps, deadlines, per-address limit |
| `test/data/PresetGoldenTest.kt`, `SharedLinkNumbersTest.kt` | `PresetStore.toJson` `app/src/main/java/dev/geode/data/PresetStore.kt:166`; `paramsToJson` `:200`; `ATTACK_RANGE` `:210` (0.005..1); `DECAY_RANGE` `:211` (0.01..1.5); `finiteOr` `:224`; `finiteIn` `:233`; `fromJson` `:298`; `SceneParamsCodec.pack` `engine/scenes/src/main/kotlin/dev/geode/render/bridge/SceneParamsCodec.kt:21,38` | preset read/write, clamp, default, retired-key drop, `water`→`pool` rename, float frame packing |
| `test/data/BuiltInPresetsTest.kt` | `BuiltInPresets.parse` `app/src/main/java/dev/geode/data/BuiltInPresets.kt:29` | built-in look text → `SceneParams` |
| `androidTest/ui/ReactivityTabTest.kt` | `EventRecipe.routesFor` `engine/scenes/src/main/kotlin/dev/geode/render/EventRecipe.kt:53`; `isOn` `:57`; `fits` `:63`; `toggle` `:69`; `BuildStyleReactionsUseCase.invoke` `engine/scenes/src/main/kotlin/dev/geode/render/scene/BuildStyleReactionsUseCase.kt:30` | ready-made reaction recipes and the style's reaction DNA |
| `androidTest/export/WatermarkOverlayTest.kt` | `WatermarkOverlay.prepare` `app/src/main/java/dev/geode/export/WatermarkOverlay.kt:38`; `draw` `:59` | watermark placement and end-card timing, drawn with GLES from Kotlin |
| `androidTest/store/StoreFramesTest.kt:127-156`, `androidTest/thumbs/StyleThumbnailRenderTest.kt:197-230` | the tests' own Kotlin frame loops over `NativeViz` (`engine/scenes/src/main/kotlin/dev/geode/render/bridge/NativeViz.kt:29`) | offscreen render driver (warm-up, audio per frame, render) written a second and third time in test code |
| `test/ui/cast/ComputerLinkUiTest.kt:61-65` | `CastFailure.linkReason` `app/src/main/java/dev/geode/cast/CastController.kt:22` | cast failure → native link end reason |
| `test/ui/theme/GlassContrastTest.kt:70-102` | `VisualAccent.from` (`app/src/main/java/dev/geode/ui/theme/VisualAccent.kt`, `Hsl.of` at :141) | accent colour derived from the visual for the chrome (UI theme, stays Kotlin unless the accent is to be native) |

## 5. Tempo rule check

No test in scope uses 120 as a tempo. The literal 120 appears five times, none a BPM: fps cases `test/data/ExportFpsTest.kt:10` and `test/data/RecordPrefsStoreTest.kt:39`; seconds `test/data/MinTrackLengthTest.kt:33`; pixels `test/widget/NowPlayingWidgetTest.kt:30` and `androidTest/journey/UiTourTest.kt:804`. The only tempo in scope is 93 BPM (`androidTest/thumbs/SyntheticGroove.kt:93`).

## 6. Gaps: classes with no test at all

Method: a main file counts as untested when its base name appears nowhere in any test source under `app/src/test`, `app/src/androidTest` or `engine/*/src/test` (name match only; a file named in a comment counts as "named"). Line counts in brackets.

- `app/src/main/java/dev/geode/export/`: `AudioTranscoder.kt` (510), `EncoderSurface.kt` (90), `ExportRange.kt` (31), `LoopExtend.kt` (642), `LoopRender.kt` (753), `LoudnessMeter.kt` (346), `MuxerBudget.kt` (37), `StudioClips.kt` (19), `VideoExporter.kt` (632).
- `cast/`: `CastController.kt` (211; only its `CastFailure`/`CastState` types are used by tests), `CastEncoder.kt` (253), `CastMirror.kt` (100), `CastOptionsProvider.kt` (45), `ComputerLink.kt` (123), `LiveHttpServer.kt` (426), `LiveSegments.kt` (190), `LiveStreamConverter.kt` (57), `WebRtcLink.kt` (145).
- `record/`: `RecordEncoder.kt` (268).
- `wallpaper/`: `DreamCover.kt` (72), `DreamOverlay.kt` (162), `EdgeGlow.kt` (22), `OutputMixCapture.kt` (126), `SoundSource.kt` (148), `SystemAnimations.kt` (53), `WallpaperRenderer.kt` (36), `WallpaperRunState.kt` (83).
- `audio/`: `AiffExtractor.kt` (172), `AudioBus.kt` (10), `AudioCapturePump.kt` (121), `AudioFxPresets.kt` (27), `MicCapture.kt` (98), `NowPlayingBridge.kt` (75), `OboeAudioOutput.kt` (175), `PlaybackCapture.kt` (82), `PlaybackCaptureService.kt` (177), `PlayoutSink.kt` (70), `TapRenderersFactory.kt` (69), `dsp/MvzAudioProcessorChain.kt` (38).
- `playback/`: `AutoDirectorFeed.kt` (77), `BitPerfectOutput.kt` (156), `HeadphoneAutoplay.kt` (65), `MediaArtwork.kt` (59), `PlaybackErrorRecovery.kt` (55), `PlaybackErrors.kt` (68), `QueueEnd.kt` (63), `QueueOps.kt` (31), `ReplayGain.kt` (92), `SessionBitmapLoader.kt` (96), `SleepTimer.kt` (126), `TrackBookmarks.kt` (46), `TrackCrossfade.kt` (52).
- `editor/`: `ClipTransition.kt` (24), `Keyframes.kt` (160), `Markers.kt` (118; only its `Marker`/`MarkerId` types are used), `StudioProject.kt` (309).
- `engine/`: `audio-android/.../PcmTap.kt` (55), `audio-core/.../audio/SampleRing.kt` (95), `audio-core/.../audio/SoundCursor.kt` (39), `scenes/.../analysis/PlaybackMath.kt` (18), `scenes/.../audio/AiffPcm.kt` (57), `scenes/.../render/Adsr.kt` (33), `BlendMode.kt` (24), `FramePacer.kt` (248), `GyroLook.kt` (50), `SceneFactory.kt` (14), `ThermalGovernor.kt` (38), `TouchField.kt` (13), `TransitionCatalog.kt` (79), `UnderlayBlend.kt` (22), `VisualizerRenderer.kt` (313), `VisualizerView.kt` (220), `bridge/StyleSchedule.kt` (31), `fluid/FluidShaderTemplate.kt` (15), `scene/MilkStarterPack.kt` (50), `util/BestEffort.kt` (18).

Largest untested engine-looking files: `LoopRender.kt`, `LoopExtend.kt`, `VideoExporter.kt`, `AudioTranscoder.kt`, `LiveHttpServer.kt`, `LoudnessMeter.kt`, `VisualizerRenderer.kt`, `RecordEncoder.kt`, `CastEncoder.kt`, `FramePacer.kt`, `AiffExtractor.kt`, `Keyframes.kt`, `StudioProject.kt`. Whether each is engine or shell is for the scanners of those directories; this section only says no test names them.

Named by a test but with no behaviour pinned: `LfoStore`, `ModulationController`, `AudioFxController`, `WallpaperPrefsStore`, `MilkPresetMemory`, `PerformanceStore`, `LookStore` appear only in `test/data/prefs/PrefsSchemaTest.kt` as key owners (what keys they write, not what the values mean); `OffscreenSceneRenderer` and `TrackAudio` appear only in a comment (`androidTest/thumbs/StyleThumbnailRenderTest.kt:42-47`).

## 7. Surprises

- HEAD is `57fdf4a`, not the brief's `f13229d`.
- No JVM or device test loads a saved LFO, ADSR, reaction list or saved palette file. The four `LfoStore` keys (`main/data/LfoStore.kt:200`) are only listed by `PrefsSchemaTest`, with stub `encodeReactions = { "reactions/2" }`, `decodeReactions = { null }` (`test/data/prefs/PrefsSchemaTest.kt:125`). The only preset-shaped compatibility record is the three JSON goldens.
- `PrefsSchemaTest.everyFileThatTouchesPrefsIsAnOwnerOrAPassThrough` (`:327-337`) scans `app/src/main` and `engine/*/src/main` source text. Deleting or renaming any of the 30 owner files or 10 pass-through files (among them `wallpaper/VisualizerWallpaperService.kt`, `wallpaper/VisualizerDreamService.kt`, `cast/CastStream.kt`, `ui/ModulationController.kt`, `audio/AudioFxController.kt`, `engine/scenes/.../MilkPresetMemory.kt`) fails this test until its lists are edited. `theOwnersThatCannotRunStillSpellTheirKeys` (`:270-275`) greps `PerformanceStore.kt`, `LookStore.kt`, `MusicLibraryRepository.kt` for quoted key names.
- JVM tests never load libgeode. Everything "native" they see is a checked-in text copy (`core/tests/*.txt`) or a stand-in (`FakeLibrarySearcher`, `snap = { if (it > 45) 60 else 30 }` at `test/data/RecordPrefsStoreTest.kt:19`, `normalizeBank = { it.ifEmpty { DEFAULT } }` at `test/data/MacroStoreTest.kt:24`). `PresetGoldenTest` therefore runs the clamp, default and retire rules in Kotlin over the schema text (`PresetStore`, `SceneParamsCodec`), which makes that Kotlin a second implementation of what native does with the same schema.
- The Auto Director taste rule exists twice: live in Kotlin (`AutoDirectorPolicy.amount`, `main/pro/AutoDirectorPolicy.kt:19`), export in native (`core/viz/Director.cpp:119`), tied only by two equal literals (pair 4).
- `core/tests/link_share_test.cpp:322-333` already pins a native Content-Length reader while `test/cast/RequestLineTest.kt:59-76` pins a Kotlin one (`RequestLineReader.readBody`): two request parsers for the cast server.
- `ReactSourceCatalogDeviceTest` is the only check of the `ReactSource` mirror and runs only on a device; the JVM suite cannot see drift.
- `androidTest` holds three copies of pbuffer EGL setup and RGBA read-back (`WatermarkOverlayTest.kt:40-73,142-158`, `StoreFramesTest.kt:53-87,160-172`, `StyleThumbnailRenderTest.kt:80-114,233-246`) and two copies of the `NativeViz` offscreen frame loop.
- `PresetLinkTest`'s sample payload (`"style"`, nested `"params"`, `test/model/PresetLinkTest.kt:15`) is not the flat shape `PresetStore.fromJson` reads (`"sceneId"`, params at top level); no test decodes a real link into a preset end to end.
- `StyleThumbnailRenderTest` and `StyleLookTest` read the Kotlin `Catalogue`/`ParamScope` (`engine/scenes`), so tools and tests keep the Kotlin catalogue alive.
- `UiTourTest`, `StoreShotsTest`, `StoreReelTest`, `StoreFramesTest`, `StyleThumbnailRenderTest` are tools gated by instrumentation arguments (`uitour`, `storeshots`, `storereel`, `storeframes`, `thumbs`); they assert almost nothing and are skipped in the ordinary run.
- `test/data/ExportFpsTest.kt:6` says the encoder's fps retry is native and pinned by `core/tests/output_test.cpp`; that file's fps cases (`:35-66`) are `FrameClock` timing, and the snap table tested here (`resolvedFps`) has no native twin found by grep.
- `MemoryPrefs.kt`, `RecordingPrefs.kt` and two private `MapPrefs` classes (`GuiSettingsCodecsTest.kt:96`, `NoteResponseStoreTest.kt:75`) are four in-memory `SharedPreferences` fakes.

Unverified: nothing was run, so "fails"/"passes" statements are from reading. Native coverage columns come from file-name and keyword greps of `core/tests/*.cpp`, not from reading each native test in full. Section 6 is a name match, not a call-graph check. Graphify was not used (tests reference production code by name; Grep was enough).


---

# 14. Build, gates and CI

## Scope and what was read

- Working tree is `main` at `57fdf4a`, one commit after the brief's `f13229d`; `git diff --stat f13229d HEAD` shows only three new files under `docs/agents/` (not used as a source).
- Read in full: every Gradle file (`settings.gradle.kts`, `build.gradle.kts`, `gradle.properties`, `gradle/libs.versions.toml`, wrapper and daemon-JVM properties, `build-logic/*`, `app/build.gradle.kts`, `engine/*/build.gradle.kts`), `app/proguard-rules.pro`, all four `detekt-baseline.xml`, `config/detekt/detekt.yml`, `app/src/main/baseline-prof.txt`, root `CMakeLists.txt`, `core/CMakeLists.txt`, `compile_flags.txt`, `.gitmodules`, `.editorconfig`, `.pre-commit-config.yaml`, `.claude/settings.json`, `.github/dependabot.yml`, `.github/pull_request_template.md`, workflows `native-host-tests.yml`, `build-lint.yml`, `arch-map.yml`, `android.yml`, `ship-apk.yml`, `release.yml`, `shader-check.yml`, the gate scripts (`engine-kotlin-gate.sh`, `layer-gate.sh`, `no-bpm-bias-gate.sh`, `network-gate.sh`, `actions-pin-gate.sh`, `check-manifest.sh`, `ktlint-changed.sh`, `check_shaders.py`, `hostlink/hostlink.sh`, `clangd-stubs/android/log.h`, `agent-setup.sh`, `release-permissions.txt`, `store-stats.sh`), `res/values/{colors,dimens,themes}.xml`, every `res/xml/*.xml`.
- Read in outline only (triggers, concurrency, jobs, Gradle and class-name lines): `ui-tour.yml`, `render-thumbs.yml`, `store-assets.yml`, `pages.yml`. Read by header only: the other `tools/` scripts (asset, brand, store and SDK helpers), `tools/sound_bench/sound_bench.cpp`, `tools/glslcheck/*`. The `res/values/strings*.xml` files (1,742 lines) were searched for arrays, integers and numeric facts, not read line by line.
- Roughly 60 files, about 6,500 lines read in full.

## 1. Module graph as built

| Module | Plugins | Kind | Depends on | Path:line |
|---|---|---|---|---|
| `:app` | `com.android.application`, `kotlin.compose`, `geode.kotlin-common`, `ksp`, `hilt` | Android app | `implementation(:engine:scenes)`, `implementation(:engine:audio-android)`, Oboe AAR, Media3 (exoplayer, common, session, cast, muxer), Compose, Hilt, Nav3, Glance, Billing | `app/build.gradle.kts:6-12`, `:257-258`, `:260-287` |
| `:engine:scenes` | `geode.android-library` | Android library (namespace `dev.geode.engine.scenes`) | `api(:engine:audio-core)`, coroutines-android | `engine/scenes/build.gradle.kts:1-12` |
| `:engine:audio-android` | `geode.android-library` | Android library (`dev.geode.engine.audioandroid`) | `api(:engine:audio-core)`, media3 exoplayer + common | `engine/audio-android/build.gradle.kts:1-13` |
| `:engine:audio-core` | `geode.jvm-library` | **JVM-only** (`org.jetbrains.kotlin.jvm`), no dependencies | none | `engine/audio-core/build.gradle.kts:1-6`, `build-logic/src/main/kotlin/geode.jvm-library.gradle.kts:1-4` |
| `build-logic` | `kotlin-dsl` | included build with three convention plugins | ktlint, detekt, Kotlin and Android Gradle plugins | `settings.gradle.kts:2`, `build-logic/build.gradle.kts:26-31` |

- Modules are declared at `settings.gradle.kts:26-32`. `GeodeNative` (all 373 `external fun`s, `System.loadLibrary("geode")` at `engine/audio-core/src/main/kotlin/dev/geode/engine/bridge/GeodeNative.kt:7`) lives in the JVM-only module.
- Convention plugins: `geode.kotlin-common` applies ktlint (`android.set(true)`) and detekt with the per-module `detekt-baseline.xml` and `config/detekt/detekt.yml` (`build-logic/src/main/kotlin/geode.kotlin-common.gradle.kts:1-21`), JDK toolchain 21 (`:28-32`), `jvmTarget` 17 (`:37-41`), and five test system properties pointing at `core/tests/*.txt` (`:60-68`).
- SDK levels: `compileSdk = 37`, `minSdk = 33`, `targetSdk = 36` in `:app` (`app/build.gradle.kts:39`, `:45-46`); libraries `compileSdk = 37`, `minSdk = 33` (`build-logic/src/main/kotlin/geode.android-library.gradle.kts:7-10`).
- Versions: AGP 9.5.1, Kotlin 2.4.21, KSP 2.3.13, Hilt 2.60.1, Media3 1.11.1, Oboe 1.11.0 (`gradle/libs.versions.toml:2-6`, `:34`); Gradle 9.9.0 (`gradle/wrapper/gradle-wrapper.properties:4`).
- NDK `28.0.13004108` (`app/build.gradle.kts:41`); ABI filters `arm64-v8a`, `x86_64` (`:51`); CMake `3.22.1`, path `../CMakeLists.txt` (`:133-138`); the only CMake argument is `-DANDROID_STL=c++_shared` (`:55`); `prefab = true` for the Oboe AAR (`:194`).
- C++: `CMAKE_CXX_STANDARD 20`, required, no extensions (`CMakeLists.txt:4-6`). Compile options `-std=c++20 -fvisibility=hidden -Wall -Wextra`, Release adds `-O2 -flto` (`CMakeLists.txt:12-18`). There is no `-Werror` in the app build. Link options: 16 KB page flags and Release `-flto` (`CMakeLists.txt:8-10`, `:19-22`). `CMAKE_BUILD_TYPE` is forced to `Release` in the cache for every variant, debug included (`CMakeLists.txt:67`).
- Shared objects in the APK:
  - `libgeode.so`: `add_library(geode SHARED ...)` from 11 JNI files (`CMakeLists.txt:127-138`), linking the static `geode_core` (`:142`).
  - `libprojectM-4.so`: `add_subdirectory(third_party/projectm)` with `BUILD_SHARED_LIBS ON`, `ENABLE_PLAYLIST OFF` (`CMakeLists.txt:67-70`), after two patches from `tools/` are applied with `git apply` at configure time (`:33-64`).
  - `liboboe.so`: prebuilt from the Oboe AAR via Prefab (`CMakeLists.txt:87`, `app/build.gradle.kts:131-132`, `:267`).
  - `libc++_shared.so`: implied by `ANDROID_STL=c++_shared`.
- Packaging: `jniLibs.useLegacyPackaging = false` (uncompressed, page-aligned .so), `dex.useLegacyPackaging = true` (`app/build.gradle.kts:140-149`). Release: R8 minify, resource shrink, `debugSymbolLevel = "SYMBOL_TABLE"` (`:99-110`). `checkNativePageAlignment` runs after `assembleRelease` and `bundleRelease` and fails on any .so with LOAD alignment under 16384 (`:198-253`).
- Third-party submodules (`.gitmodules:1-24`):

| Submodule | What | How it is built | Path:line |
|---|---|---|---|
| `projectm` | MilkDrop preset engine, v4.1.7 + 2 patches | shared `libprojectM-4.so` | `CMakeLists.txt:33-70` |
| `taglib` | audio tag reader | static `tag`, hidden visibility, no zlib | `CMakeLists.txt:74-81` |
| `cgltf` | glTF parser, header-only | compiled inside `scene/object/GltfImport.cpp` | `core/CMakeLists.txt:183-184` |
| `meshoptimizer` | 4 source files | static `geode_meshopt` | `core/CMakeLists.txt:175-185` |
| `basis_universal` | texture encoder/transcoder + zstd, C++17 | static `geode_basisu` (32 objects) | `core/CMakeLists.txt:193-234` |
| `kissfft` | FFT, header-only | not in CMake; included by relative path at `core/sound/Fft.cpp:11` | `.gitmodules:16-18` |
| `libdatachannel` | WebRTC data/media (with libjuice, libsrtp, usrsctp, plog) | static `LibDataChannel::LibDataChannelStatic`, `EXCLUDE_FROM_ALL` | `CMakeLists.txt:108-123`, `core/CMakeLists.txt:242` |
| `mbedtls` | DTLS for libdatachannel | static `mbedtls mbedx509 mbedcrypto`, user config `core/cast/mbedtls_user_config.h` | `CMakeLists.txt:96-107` |

## 2. Native link line today

`libgeode.so` = `target_link_libraries(geode PRIVATE geode_core log android)` (`CMakeLists.txt:142`).

`geode_core` (static) carries, as PUBLIC unless noted:

| Library | Kind | Path:line |
|---|---|---|
| `projectM` | in-tree shared | `core/CMakeLists.txt:168` |
| `tag` (TagLib) | in-tree static | `core/CMakeLists.txt:168` |
| `GLESv3` | system | `core/CMakeLists.txt:168` |
| `EGL` | system | `core/CMakeLists.txt:168` |
| `log` | system | `core/CMakeLists.txt:168`, `CMakeLists.txt:142` |
| `android` | system (`libandroid.so`) | `core/CMakeLists.txt:168`, `CMakeLists.txt:142` |
| `nativewindow` | system (AHardwareBuffer for `viz/frame/CastTee`) | `core/CMakeLists.txt:167-168` |
| `oboe::oboe` | Prefab shared | `core/CMakeLists.txt:170` |
| `geode_meshopt` | in-tree static | `core/CMakeLists.txt:185` |
| `geode_basisu` | in-tree static | `core/CMakeLists.txt:234` |
| `LibDataChannel::LibDataChannelStatic` | in-tree static, PRIVATE | `core/CMakeLists.txt:242` |

NDK headers the native code includes today: `EGL/egl.h` (`core/api/geode_viz_api.cpp:13`, `core/viz/frame/RecordTap.hpp:2`, `core/viz/frame/CastTee.hpp:2`), `EGL/eglext.h` and `android/hardware_buffer.h` (`core/viz/frame/CastTee.cpp:3`, `:5`), `android/asset_manager.h` (`core/api/geode_api.h:4`, `core/viz/Renderer.hpp:3`, `core/viz/ShaderSource.hpp:2`), `android/native_window_jni.h` and `android/asset_manager_jni.h` (`app/src/main/cpp/geode_viz_jni.cpp:2-3`), `oboe/Oboe.h` (`core/audio/output/OboeOutput.hpp:9`). No file includes `media/*`, `aaudio/*`, `android/choreographer.h`, `android/looper.h`, `android/sensor.h` or `android/thermal.h`.

Not linked, needed by the port:

- `mediandk` (AMediaCodec, AMediaMuxer, AMediaExtractor, AMediaFormat, AImageReader): absent from both CMake files. This is the one hard gap for the NDK codec work.
- `aaudio` / `OpenSLES`: not linked directly; `liboboe.so` owns them. Needed only if code calls AAudio without Oboe.
- `jnigraphics` (AndroidBitmap): absent; needed only if bitmaps cross JNI as `Bitmap`.
- `camera2ndk`, `binder_ndk`: absent, not asked for.
- Already covered by the linked `android` library: ANativeWindow, AChoreographer, ALooper, ASensorManager, AThermal, AAssetManager (API facts from the NDK, not from this repo; unverified here).
- No header stubs exist for the new NDK APIs on the host: the host stub trees hold only `core/tests/stub/android/asset_manager.h`, `core/tests/fakegl/GLES3/gl3.h`, `core/tests/fakegl/android/{log.h,asset_manager.h}`; clangd has `tools/clangd-stubs/android/log.h` (`compile_flags.txt:6-7`).

## 3. CI workflows

| Workflow | Trigger | Jobs and what they run | Concurrency | Path:line |
|---|---|---|---|---|
| `native-host-tests.yml` | every `pull_request`, `workflow_dispatch`, `workflow_call` | one job, 20 min timeout, `submodules: recursive`; about 95 `g++` compile-and-run pairs in 15 steps | group per PR head ref (or SHA), `cancel-in-progress: true` | `:8-11`, `:18-20`, `:26-35` |
| `build-lint.yml` | every `pull_request`, `workflow_dispatch` | one job, 45 min: JDK 21, SDK install, `:app:assembleDebug`, upload APK, `tools/check-manifest.sh`, JVM unit tests of all four modules, detekt of all four, `:app:lintDebug`, `ktlintCheck` | group per PR head ref, `cancel-in-progress: true` | `:8-10`, `:15-17`, `:58-102` |
| `arch-map.yml` | `pull_request` to `main`, types opened/synchronize/reopened/edited | one job, 5 min, `fetch-depth: 0`: self-tests then runs `layer-gate`, `no-bpm-bias-gate`, `actions-pin-gate`, `network-gate`, `cmp` of the notices files, `engine-kotlin-gate` (numstat, `--files`, `--list-grew`) | group `arch-map-${{ github.ref }}`, `cancel-in-progress: true` | `:7-10`, `:15-17`, `:44-203` |
| `shader-check.yml` | `pull_request` touching shader paths, `workflow_dispatch` | `python3 tools/check_shaders.py` with the NDK's `glslc` | per head ref, cancel true | `:7-17`, `:22-24`, `:47-48` |
| `android.yml` | `workflow_dispatch` only | `sound-rates` (g++ without `-Werror`, real music from branch `claude/test-music`), `build-debug-apk` (glslcheck one style, detekt, shader check, assemble, network gate + banned Maven deps, manifest check, unit tests, lint, ktlint), `instrumented-tests` (emulator API 36, `tools/connected-tests.sh`), `pre-release` (calls `ship-apk.yml`) | per ref, cancel true | `:11-12`, `:24-26`, `:35-399` |
| `ship-apk.yml` | `push` to `main`, `workflow_dispatch`, `workflow_call` | `host-tests` (calls `native-host-tests.yml`, only on push), then `ship-apk`: unit tests (push only), reserve number, `:app:assembleDebug -PreleaseNumber=N`, publish a GitHub pre-release | group `geode-ship-${{ github.sha }}`, `cancel-in-progress: false` | `:18-43`, `:56-58`, `:67-77`, `:121-143` |
| `release.yml` | `workflow_dispatch` only | checks every check-run on the commit is green and that a "Native host tests" run exists, `:app:bundleRelease`, manifest, permission list, R8, baseline profile and signing checks | per ref, no cancel | `:24-30`, `:36-38`, `:53-66`, `:156-235` |
| `ui-tour.yml`, `render-thumbs.yml`, `store-assets.yml` | `workflow_dispatch` only | emulator runs of `dev.geode.journey.UiTourTest`, `dev.geode.thumbs.StyleThumbnailRenderTest`, `dev.geode.store.*` | cancel true / false / true | `ui-tour.yml:20-28`, `:103`; `render-thumbs.yml:21-43`, `:111`; `store-assets.yml:28-53`, `:155` |
| `pages.yml` | push to `main` and PRs on doc paths | assembles and deploys the docs site | group `pages`, no cancel | `:19-42` |

- **Cancel on push:** a new push to a PR branch cancels the running `native-host-tests`, `build-lint`, `arch-map` and `shader-check` runs for that PR (`cancel-in-progress: true` in each). On `main`, `ship-apk.yml` is keyed on the commit SHA with `cancel-in-progress: false`, so each merged commit builds on its own and a later push does not cancel it (`ship-apk.yml:48-58`); the host tests it calls are keyed on the SHA for the same reason (`native-host-tests.yml:16-20`).
- **What gates a PR:** the workflows that run on `pull_request` are `build-lint`, `native-host-tests`, `arch-map`, and path-filtered `shader-check` and `pages`. Which of them are *required* checks is a branch-protection setting, not in the repository (unverified). Instrumented tests never run on a PR (`android.yml:10-12`).
- CI installs `platforms;android-36` while the build declares `compileSdk = 37` (`build-lint.yml:50`, `app/build.gradle.kts:39`).

### How a native host test is wired

- Host tests are **not** built by CMake. `core/CMakeLists.txt` has no test target, no `enable_testing`, no `add_test`. Each test is a hand-written `g++` line listing its own sources, followed by a run of the binary, in a `run: |` block of `native-host-tests.yml`.
- To add one:
  1. Add the production `.cpp` (and its `api/geode_*_api.cpp`) to `add_library(geode_core STATIC ...)` in `core/CMakeLists.txt:1-159` so the app build has it. Example: `api/geode_reaction_text_api.cpp` and `viz/ReactionText.cpp` at `core/CMakeLists.txt:10-11`.
  2. Add a new JNI file, if any, to `add_library(geode SHARED ...)` at `CMakeLists.txt:127-138` (example: `geode_shell_port_jni.cpp` at `:138`).
  3. Add `core/tests/<name>_test.cpp` and a compile + run pair in `native-host-tests.yml`, either inside the first step (`:37-296`) or as its own named step. Example of a named step, `native-host-tests.yml:331-338`:
     ```
     - name: Reaction text host tests
       run: |
         g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/reaction_text_test.cpp core/viz/ReactionText.cpp \
           core/api/geode_reaction_text_api.cpp core/viz/ModTargets.cpp ... -o reaction_text_test
         ./reaction_text_test
     ```
  4. If the test proves a text fixture the JVM reads, the fixture path is passed as an argument (`./catalogue_test core/tests/catalogue.txt`, `native-host-tests.yml:308`) and wired to the JVM tests as a system property in `build-logic/src/main/kotlin/geode.kotlin-common.gradle.kts:60-68`.
- Compiler and flags: the runner's `g++` (ubuntu-latest), `-std=c++20 -O1 -Wall -Wextra -Werror`, include paths `-Icore` plus `-Icore/tests/stub` or `-Icore/tests/fakegl`, `-pthread` where threads are used (`native-host-tests.yml:40`, `:88`, `:170`). No sanitizer, no clang, no NDK toolchain, no CMake, no ccache, one sequential job. Basis Universal objects are the exception: `-std=c++17 -O2 -DNDEBUG -w` in parallel (`:245-252`).
- Every test in `core/tests/*.cpp` (94 `*_test.cpp` plus `visual_gate.cpp`) is named in the workflow; none is unwired (checked by name against the file).
- Things a new test or source must not break:
  - Twelve tests compile `core/sound/*.cpp` by glob, several with only `-Icore` (`native-host-tests.yml:93`, `:96`, `:98`, `:101`, `:443`, `:452`, `:461`, `:469`). Any new file in `core/sound/` is compiled into all of them under `-Werror` with no Android stub on the include path.
  - `native-host-tests.yml:248-252` reads the `geode_basisu` source list out of `core/CMakeLists.txt` with `sed` and asserts exactly 32 objects.
  - `tools/hostlink/hostlink.sh:91` reads the `geode_core` list with `sed` up to the first `^)` and keeps only `viz/`, `sound/`, `util/` lines; sources added with `target_sources` (`core/CMakeLists.txt:183`, `:233`, `:239-241`) are invisible to it. hostlink is not run by any workflow.
  - `hidden_hits_test` scans the `core/` tree itself (`native-host-tests.yml:154-155`), and `style_reads_test` takes `--core core/` (`:314`): they read new core sources as text.

## 4. Gates

### `tools/engine-kotlin-gate.sh` (three modes; all run in `arch-map.yml:185-203`)

**Mode 1, numstat (default).** Reads `git diff --numstat -M base...head`. For each path matching the watched regex and not exempt, sums `added - deleted`; fails if the net is greater than 0 (`tools/engine-kotlin-gate.sh:153-171`). Binary rows are skipped (`:161`); for a rename the new path is the one tested (`:158`).

Watched areas, verbatim (`tools/engine-kotlin-gate.sh:135-150`, `A='app/src/main/java/dev/geode'`, wrapped as `^(...)\.kt$`):

```
'engine/[^/]+/src/main/.*'
"$A/(export|audio/dsp)/.*"
"$A/ui/RecordController"
"$A/ui/TakeController"
"$A/data/PerformanceTake"
"$A/editor/(Timeline|Keyframes|AnimatableParams)"
"$A/record/.*"
"$A/scene/.*"
"$A/gltf/.*"
"$A/(.*/)?[^/]*(Gltf|Glb)[^/]*"
"$A/(.*/)?[^/]*Pack[^/]*Import[^/]*"
"$A/ui/ExportController"
```

Exempt, verbatim (`:151`): `exempt='/GeodeNative\.kt$'`. Nothing else is exempt. `NativeViz.kt` is watched: the self-test asserts that growth in `engine/scenes/.../render/bridge/NativeViz.kt` fails (`.github/workflows/arch-map.yml:170`).

**Mode 2, `--files`.** Every `*.kt` under `engine/*/src/main/` must be on the `allowed` list, and every entry must still exist as a file (`tools/engine-kotlin-gate.sh:98-122`). The shrink-only list, verbatim (`:45-96`; `S='engine/scenes/src/main/kotlin/dev/geode'`, `C='engine/audio-core/src/main/kotlin/dev/geode/engine'`):

```
"engine/audio-android/src/main/kotlin/dev/geode/engine/audioandroid/PcmTap.kt"
"$C/audio/SampleRing.kt"
"$C/audio/SoundCursor.kt"
"$C/audio/SoundTuning.kt"
"$C/bridge/GeodeNative.kt"
"$S/analysis/PlaybackMath.kt"
"$S/analysis/SearchMatcher.kt"
"$S/audio/AiffPcm.kt"
"$S/render/Adsr.kt"
"$S/render/BlendMode.kt"
"$S/render/EventRecipe.kt"
"$S/render/FramePacer.kt"
"$S/render/GyroLook.kt"
"$S/render/Lfo.kt"
"$S/render/MilkPresetMemory.kt"
"$S/render/PerformanceSettings.kt"
"$S/render/Reactions.kt"
"$S/render/SceneFactory.kt"
"$S/render/ThermalGovernor.kt"
"$S/render/TouchField.kt"
"$S/render/TransitionCatalog.kt"
"$S/render/TransitionStyle.kt"
"$S/render/UnderlayBlend.kt"
"$S/render/VisualizerRenderer.kt"
"$S/render/VisualizerView.kt"
"$S/render/bridge/ModConfigCodec.kt"
"$S/render/bridge/NativeViz.kt"
"$S/render/bridge/SceneParamsCodec.kt"
"$S/render/bridge/StyleSchedule.kt"
"$S/render/fluid/FluidQuality.kt"
"$S/render/fluid/FluidShaderTemplate.kt"
"$S/render/look/LookApplier.kt"
"$S/render/look/LookSnapshot.kt"
"$S/render/offscreen/OffscreenSceneRenderer.kt"
"$S/render/offscreen/TrackAudio.kt"
"$S/render/scene/BuildStyleReactionsUseCase.kt"
"$S/render/scene/Catalogue.kt"
"$S/render/scene/MilkStarterPack.kt"
"$S/render/scene/ParamFormat.kt"
"$S/render/scene/ParamKeys.kt"
"$S/render/scene/ParamSchema.kt"
"$S/render/scene/ParamScope.kt"
"$S/render/scene/SceneCapabilities.kt"
"$S/render/scene/SceneIds.kt"
"$S/render/scene/SceneParams.kt"
"$S/render/scene/StyleFamilies.kt"
"$S/render/scene/VisualStyleCatalog.kt"
"$S/util/BestEffort.kt"
```

48 entries; all 48 exist today and they are exactly the 48 `.kt` files under `engine/*/src/main/`.

**Mode 3, `--list-grew`.** Reads the PR's diff of the gate script itself; any added line matching `(\$[SC]|engine/[^/ ]+/src/main)/[^ ]*\.kt` fails (`tools/engine-kotlin-gate.sh:124-133`). It runs only once the base commit has the list (`arch-map.yml:198-203`).

What this means for the port, on every PR:

- Net Kotlin lines across all watched paths must be 0 or negative. Only `GeodeNative.kt` may grow freely. A new thin wrapper in `NativeViz.kt` counts as growth and must be paid for by deletions in the same PR.
- A deleted engine-module file must have its `allowed` entry removed in the same PR, or `--files` fails as stale (`:113-118`).
- An engine-module file cannot be renamed, split or moved within `engine/*/src/main/`: the new path would need a new list entry, which `--list-grew` refuses. It can only shrink in place, be deleted, or leave the engine modules.
- `src/test` is not watched (`engine/[^/]+/src/main/.*`), so deleting a JVM test earns no credit and adding one costs nothing.
- Kotlin under `:app` outside the watched patterns (for example `ui/AutoVisualsController.kt`, `wallpaper/`, `cast/`, `playback/`, `audio/` other than `audio/dsp/`) is not counted either way.
- Entries that will have to change as files go: every line of the `allowed` list above; the header comment's file names (`:9-28`); and the already dead watched entries `ui/TakeController`, `data/PerformanceTake` (`:140-141`, files absent), `editor/AnimatableParams` (`:142`, absent), `scene/.*` and `gltf/.*` (`:144-145`, directories absent). The self-test's sample paths in `arch-map.yml:153-182` are synthetic strings and do not need the files to exist.

### `tools/layer-gate.sh`

- Fails on any `dev.geode.ui` reference in `*.kt` under `app/src/main/java` or `engine` outside `app/src/main/java/dev/geode/(ui|di)/` (`tools/layer-gate.sh:15-22`).
- Fails on `getSharedPreferences(` or `GeodePrefsFiles(` in `app/src/main/java` outside `dev/geode/data/` and `GeodeContainer.kt` (`:28-35`).
- No allow-list beyond those two path filters. Port impact: only for Kotlin that moves between packages.

### `tools/no-bpm-bias-gate.sh`

- Scans `core app engine docs tools .claude/skills .github AGENTS.md CLAUDE.md GLOSSARY.md README.md CHANGELOG.md` (`:29`) in 22 file types including `.cpp .hpp .h .inc .kt .glsl .md .yml .txt .sh` (`:45-48`).
- Five patterns: `prior`, `preferred`, `default`, `literal` (a `bpm...` name assigned a non-zero number at end of line), and `onetwenty` (120 as a tempo, `60/120`, `120/60`, a beat length of 0.5) (`:34-43`).
- The first four skip `*/src/(test|androidTest)/`, `core/tests/` and `tools/glslcheck/` (`:50-51`); `onetwenty` skips nothing (`:52`).
- Allow-list: the gate script and `arch-map.yml` themselves, any line containing `kFastestBpm` or `kSlowestBpm`, and lines in AGENTS.md or `.claude/skills` that mention the gate (`:54-57`).
- Port impact: ported C++ and its host tests are scanned. A line such as `bpm = 93.0f;` in non-test C++ fails; any 120 used as a tempo fails even in `core/tests`.

### `tools/network-gate.sh`

- Kotlin/Java under `app/src/main` and `engine/*/src/main`: `java.net.(Socket|ServerSocket|DatagramSocket|HttpURLConnection|URL)`, `.openConnection(`, `HttpsURLConnection` fail outside a path containing `/dev/geode/cast/` (`:15-22`).
- Native under `core` and `app/src/main/cpp`: an `#include` of `sys/socket.h`, `netinet/`, `arpa/inet.h`, `netdb.h`, `ifaddrs.h`, `net/if.h`, `curl/`, `rtc/`, `juice/`, `srtp`, `usrsctp.h`, `mbedtls/net`, `mbedtls/ssl.h`, `openssl/ssl.h` fails outside `core/cast/` (`:24-32`).
- `.gitmodules` URLs matching a banned list (curl, boost, asio, grpc, openssl, webrtc, sentry, firebase and others) fail (`:34-42`). `android.yml:247-253` adds a banned-Maven-dependency check on the release classpath (manual workflow only).
- Port impact: none unless the port adds a socket header or a submodule.

### `tools/actions-pin-gate.sh`

- Every `uses:` in `.github/workflows/*.yml` must end in `@<40 hex>`; `./` local references are exempt (`:12-14`). Any new workflow step for host tests must pin its actions.

### `tools/check-manifest.sh` (after `:app:assembleDebug`, `build-lint.yml:75-76`)

- Reads the merged debug manifest; fails if INTERNET is present without the string `dev.geode.cast.CastService` (`:12-14`), if `AD_ID` is present (`:15-17`), if `com.android.vending.BILLING` is missing (`:18-19`), if any `uses-feature` is required, if no `LEANBACK_LAUNCHER` category, if no `android:banner` (`:23-36`), or if `targetSdkVersion` is under 36 (`:37-39`).
- Port impact: a `<uses-feature>` for a sensor, or a library manifest that adds one as required, fails here.

### `tools/release-permissions.txt` (checked in `release.yml:189-205`)

- The merged release manifest may request only the 16 permissions listed at `tools/release-permissions.txt:16-31`. A new permission for sensors or audio (for example `HIGH_SAMPLING_RATE_SENSORS`) must be added there in the same change.

### `tools/check_shaders.py`

- Resolves `//#include` against the allow-list in `core/viz/ShaderIncludes.inc` (`:23`, `:130-136`), compiles every `*_frag.glsl`, `*_vert.glsl` and the two Silk bodies with `glslc` after bumping `#version 300 es` to `310 es` (`:163-187`), with 18 define variants for `fluid_display_frag.glsl` (`:28-31`).
- Lints the "time rule": no statement may combine `uSpeed` with `uTime`, `uFlowPhase`, `uFlowTime`, `uSoundTimeSped` or `SND_*_TIME` (`:77-127`).
- Hard-codes facts that mirror C++: the Silk preamble with `SIM_STATE_SCALE = 8.0` (`:37-69`) mirrors `core/viz/compute/SimGlsl.cpp`, and the look count `range(9)` mirrors FluidLook. These are second copies of engine facts in Python.

### Other

- `tools/ktlint-changed.sh`: ktlint CLI 1.5.0 by pinned SHA-256 on changed files matching `^(app|engine/[^/]+)/(src/.*\.kts?|[^/]*\.kts)$` (`:16-20`, `:35-38`). Local only; CI runs `./gradlew ktlintCheck` (`build-lint.yml:102`).
- detekt: thresholds in `config/detekt/detekt.yml` (`LongMethod` 320, `CyclomaticComplexMethod` 85, `LargeClass` 1100 with four file excludes at `:49-53`, `ReturnCount` 8). Android lint: `abortOnError = true`, `HardcodedText` fatal (`app/build.gradle.kts:161-165`).
- `tools/hostlink/hostlink.sh`: compiles every `viz/`, `sound/`, `util/` source of `geode_core` on the host with the project flags, stubs projectM/AAsset/log C symbols, links one `.so` with `--no-undefined` against Mesa GLES/EGL (`:91-109`). Not run in CI. Needs network (clones projectM headers, `:40-45`).
- `tools/glslcheck`: Node + headless Chrome renderer; reads `core/tests/catalogue.txt` and `core/viz/ShaderIncludes.inc` (`tools/glslcheck/check.mjs:55-56`); one style is rendered in the manual `android.yml:102-106` only.
- `tools/sound_bench/sound_bench.cpp`: accuracy bench; its `--quick` run is the last host-test step (`native-host-tests.yml:466-470`).

## 5. Things that name Kotlin classes by string

Break the build or a gate if the class is deleted or moved:

- `app/proguard-rules.pro:14`: `dev.geode.engine.bridge.GeodeNative` (keep native methods and names).
- `app/src/main/cpp/*_jni.cpp`: 373 symbols `Java_dev_geode_engine_bridge_GeodeNative_*`; the package and class name of `GeodeNative` are fixed by static registration (no `RegisterNatives`, no `JNI_OnLoad`). The only `FindClass` is `java/lang/String` (`app/src/main/cpp/jni_util.hpp:17`, `:46-47`).
- `app/proguard-rules.pro:58`: `dev.geode.render.scene.SceneParams` field names, read by reflection at `engine/scenes/src/main/kotlin/dev/geode/render/bridge/SceneParamsCodec.kt:70`. Both files are on the port list; the rule goes stale when they go.
- `app/proguard-rules.pro:65`: any `androidx.glance.appwidget.action.ActionCallback` implementation (shell, stays).
- `tools/engine-kotlin-gate.sh:47-96`: the 48 file paths above; a deleted file with its entry left in fails `--files`.
- `app/src/main/AndroidManifest.xml:118` `.GeodeApp`; `:140` `.ui.MainActivity`; `:234` `.ui.wallpaper.WallpaperOptionsActivity`; `:239` `.wallpaper.VisualizerWallpaperService`; `:258` `.wallpaper.VisualizerDreamService`; `:283` `.playback.PlaybackService`; `:328` `.audio.PlaybackCaptureService`; `:340` `.export.ExportService`; `:351` `.cast.CastService`; `:373` `.audio.GeodeNotificationListener`; `:384` `.widget.NowPlayingWidgetReceiver`; `:401` `.tile.PlaybackTileService`; `:414` `.tile.TileLongPressActivity`.
- `app/src/main/res/xml/wallpaper.xml:11`: `dev.geode.ui.wallpaper.WallpaperOptionsActivity`.
- `tools/check-manifest.sh:12` and `.github/workflows/release.yml:173`: the string `dev.geode.cast.CastService`.
- `app/src/test/java/dev/geode/data/prefs/PrefsSchemaTest.kt:92-199`: source paths of every prefs owner, including `engine/scenes/src/main/kotlin/dev/geode/render/MilkPresetMemory.kt` (`:140`), `ui/ModulationController.kt` (`:163`), `audio/AudioFxController.kt` (`:147`), `data/LfoStore.kt` (`:124`), `data/PerformanceStore.kt` (`:197`), `data/LookStore.kt` (`:198`); pass-through files at `:217-226` include `wallpaper/VisualizerWallpaperService.kt`, `wallpaper/VisualizerDreamService.kt`, `cast/CastStream.kt`. The test reads source text (`:273`) and asserts the set of files that touch SharedPreferences equals these lists (`:336`).
- `build-logic/src/main/kotlin/geode.kotlin-common.gradle.kts:60-68`: system properties `geode.catalogue`, `geode.customize`, `geode.paramsSchema`, `geode.modTargets`, `geode.macroEditor`, consumed by `engine/scenes/.../render/scene/Catalogue.kt:24`, `app/.../ui/CustomizeCategory.kt:119`, `engine/scenes/.../render/scene/ParamSchema.kt:26`, `engine/scenes/.../render/Lfo.kt:164`, `app/.../data/MacroEditor.kt:147`. They become unused when those Kotlin readers go; the fixtures stay pinned by host tests.
- Gradle task names per module: `:engine:audio-core:test :engine:scenes:testDebugUnitTest :engine:audio-android:testDebugUnitTest` and the matching `:detekt` tasks at `build-lint.yml:81`, `:85`, `:90`; `android.yml:148`, `:158`, `:261`; `ship-apk.yml:124`. Removing an engine module breaks these lines, `settings.gradle.kts:28-32` and `app/build.gradle.kts:257-258`.
- Instrumented test classes named in workflows: `dev.geode.journey.UiTourTest` (`ui-tour.yml:103`), `dev.geode.thumbs.StyleThumbnailRenderTest` (`render-thumbs.yml:111`), `dev.geode.store.$class` (`store-assets.yml:155`).
- `tools/store-stats.sh:13`, `:30-36`: greps `pro/FreeTier.kt` (`neonProIds`, `FREE_MAX_SHORT_SIDE`, `FREE_MAX_EXPORT_MS`), `ui/*.kt` (`enum class CustomizeCategory`), `export/VideoExporter.kt` (`enum class ExportQuality`, `ExportCodec`, `ExportRatio`), `data/ExportPrefs.kt` (`EXPORT_FPS_OPTIONS`, `enum class ExportDestination`). Run by hand; prints wrong counts once those enums move to C++.
- `tools/material_symbols.py:58`: writes `app/src/main/java/dev/geode/ui/theme/MaterialSymbols.kt` (shell, stays).

Go stale silently (no failure):

- `app/src/main/baseline-prof.txt:29-30`, `:54`: `dev/geode/render/VisualizerView`, `dev/geode/render/VisualizerRenderer` (both on the gate's port list). `:22`, `:43`: `ui/PlayerSession**`, `ui/*Controller**`. `:42`: `ui/EnginePlumbingKt`. `:28`: `playback/PlaybackEngine**`. `:25`: `dev/geode/data/ThemeStore**` is already stale: no `ThemeStore` exists in any source file.
- `app/detekt-baseline.xml`: entries for engine-side files that the port will delete or shrink: `VideoExporter.kt` (`:7`, `:21`, `:45`, `:52`), `AudioTranscoder.kt` (`:12`, `:17-18`, `:23`, `:47-49`, `:54`), `AudioCapturePump.kt` (`:11`), `AiffExtractor.kt` (`:8`, `:22`, `:46`), `AutoVisualsController.kt` (`:5`, `:24`), `ExportController.kt` (`:9`, `:50`), `MicCapture.kt` (`:28`), `PlaybackCapture.kt` (`:19`, `:34`), `TextureStore.kt` (`:6`, `:43`), `PlayerViewModel.kt` (`:10`, `:38-39`, `:51`). Already stale: `PerformanceTake.kt` (`:15`, `:32-33`) and `TakeStore.kt` (`:42`), files absent.
- `engine/audio-android/detekt-baseline.xml:6-7`: `PcmTap.kt`. `:5`: `SinkClockDriver.kt`, already absent.
- `config/detekt/detekt.yml:50-53`: `ThemePackCatalog.kt` (absent), `VisualStyleCatalog.kt` (on the port list), `BuiltInPresets.kt`, `GeodeNative.kt`.
- Comments only: `native-host-tests.yml:119` ("pinned to the Kotlin TrackAudio and AiffPcm"), `:185`, `:289`, `:294`, `:317`, `:330`.

## 6. Engine data in resources

- No `string-array`, `integer-array`, `array`, `integer`, `bool` or `fraction` resource exists in `app/src/main/res/values/`. No scene list, parameter default or range is stored as a resource value.
- `res/values/colors.xml:4-7`: two brand colours for the splash only. `res/values/dimens.xml:5-11`: widget sizes only. `res/values/themes.xml:10-15`: splash theme only.
- `res/xml/`: backup rules, widget info, wallpaper and dream metadata, FileProvider path, locale config, automotive descriptor. No engine value.
- Engine facts restated as prose in strings (they can drift from the native tables):
  - `strings.xml:42-44`: performance tiers ("Half resolution, 30 fps", "Three-quarter resolution at 60 fps", "Full resolution").
  - `strings.xml:51`: frame caps 90 and 120.
  - `strings.xml:547-548`, `:581-583`: export frame rates 24, 25, 30, 50, 60 as one string each.
  - `strings.xml:613-624`: loudness targets (-14 LUFS, -10 to -12 LUFS, -70 LUFS gate, -1 dBTP).
  - `strings_pro.xml:39-43`, `strings_studio.xml:18`: free and Pro limits (1080p, 4K, 30/60 fps, "the first minute").
- `res/raw/`: `splash_intro.mp4` and three UI sounds; no engine table.

## 7. Agent tooling in the repo

- `.claude/settings.json`: `skillOverrides` (name-only listing for 90 skills, `:3-94`), `enabledPlugins` (shader-skills, systems-programming, code-refactoring, mattpocock-skills, superpowers, clangd-lsp, `:95-102`), `extraKnownMarketplaces` (`:103-128`). **No `hooks` and no `permissions` block.** Nothing in `.claude/` runs on a tool call or a commit.
- `.claude/` holds only `settings.json` and `skills/` (77 skill folders plus a licence file). Repo-specific skills by name: `geode-native-core`, `geode-shaders`, `geode-sound-engine`. Not read.
- `tools/agent-setup.sh`: installs `clangd`, `glslc`, GLES and EGL dev headers with apt (`:18-29`), the Android CLI (`:33-37`), fetches two pinned skill sets into `.claude/skills` and excludes them through `.git/info/exclude` (`:45-71`), fetches the ktlint jar (`:75-76`), installs the plugins from `settings.json` (`:80-88`). It enforces nothing; it always exits 0 (`:94`).
- `.pre-commit-config.yaml`: optional, only active after `pre-commit install`. On commit: `layer-gate` (`:10-15`), `no-bpm-bias-gate` (`:16-20`), `actions-pin-gate` (`:21-26`), `network-gate` (`:27-31`), `cmp` of the notices files (`:32-37`), `store-check` (`:38-43`). On push: `ktlint-changed` (`:44-50`) and `engine-kotlin-gate` in numstat mode against `origin/main` (`:51-57`). The `--files` and `--list-grew` modes are not in pre-commit; only CI runs them.
- `compile_flags.txt:1-12`: clangd flags for the host (`-Icore -Icore/tests/stub -Itools/clangd-stubs` and the glTF/basis include paths).

## 8. Surprises

- Host tests bypass CMake entirely. The app's source list (`core/CMakeLists.txt`) and each test's source list (`native-host-tests.yml`) are two hand-kept lists. A file added to one and not the other is caught only when the APK link or that test fails.
- Host tests run `g++ -Werror`; the app builds with NDK clang and no `-Werror`. A warning clean under clang can fail the host job and the reverse. `android.yml:46` builds `sound_rate_test` without `-Werror`.
- No sanitizer runs anywhere (no ASan, UBSan or TSan flag in any workflow), although several host tests are thread tests (`-pthread` at `native-host-tests.yml:88`, `:91`, `:117`, `:137`, `:219`, `:399`).
- `CMAKE_BUILD_TYPE` is forced to `Release` for all native code, debug APK included (`CMakeLists.txt:67`), so debug builds get `-O2 -flto` native code.
- `mediandk` is not linked and no `media/Ndk*` header is included anywhere: all codec work is Kotlin today.
- `NativeViz.kt` is a watched file, not an exempt one (`tools/engine-kotlin-gate.sh:151`, `arch-map.yml:170`). Only `GeodeNative.kt` grows freely.
- The gate's `allowed` list blocks moving or splitting a file inside the engine modules, since any new path is a new entry.
- The gate watches paths that no longer exist (`ui/TakeController`, `data/PerformanceTake`, `editor/AnimatableParams`, `scene/`, `gltf/`), and three baselines name absent files (`PerformanceTake.kt`, `TakeStore.kt`, `SinkClockDriver.kt`), as does `detekt.yml` (`ThemePackCatalog.kt`) and the baseline profile (`ThemeStore`).
- `GeodeNative`, the JNI class, sits in the JVM-only module `:engine:audio-core`, which has no Android dependency (`engine/audio-core/build.gradle.kts:1-6`). Any `external fun` taking a `Surface`, `AssetManager` or `Context` must be typed `Any`/`Object` there or the class must move, and moving it renames 373 JNI symbols.
- CI installs SDK platform 36 while `compileSdk` is 37 (`build-lint.yml:50`, `android.yml:132`, `ship-apk.yml:110`, `release.yml:84`).
- `arch-map.yml` runs only for PRs into `main` (`:8-9`); a PR into another branch skips every architecture gate while `build-lint` and the host tests still run.
- `release.yml:61` requires a check-run named "Native host tests" on the commit; renaming that job or workflow breaks the release.
- In this checkout the `libdatachannel` and `mbedtls` submodules are not initialised (`git submodule status` shows `-`), and `cgltf`, `kissfft`, `taglib` show as modified. Their CMake behaviour was read from the root `CMakeLists.txt` only.

## Not verified

- Which checks branch protection requires on `main`.
- Whether stale detekt baseline entries are tolerated (assumed from detekt's behaviour, not from this repo).
- That `libandroid` at API 33 exports every AChoreographer, ASensor and AThermal call the port wants (NDK fact, not in the repo).
- The four emulator and pages workflows beyond their triggers, concurrency and class-name lines.
- `strings*.xml` line by line.


---

# Guide notes

## Guide notes (fetched 2026-11-12 from developer.android.com; minSdk 33)

## JNI tips (training/articles/perf-jni)
Design rules, in the page's order of importance:
1. "Minimize marshalling of resources across the JNI layer": less data, less often.
2. "Avoid asynchronous communication between code written in a managed programming language and code written in C++ when possible": keep async UI updates in the UI's language; one thread makes a blocking native call and notifies the UI thread.
3. "Minimize the number of threads that need to touch or be touched by JNI."
4. Keep interface code in few, easily identified C++ and Java locations.

Mechanics:
- JNIEnv is per thread, never shared; share JavaVM and use GetEnv.
- Native-created threads (pthread_create, std::thread) have no JNIEnv until AttachCurrentThread; must DetachCurrentThread before exit (pthread_key_create destructor is the suggested way). "It's usually best to use Thread.start() to create any thread that needs to call in to Java code."
- An attached native thread never frees local refs automatically: delete them by hand.
- FindClass from an attached native thread uses the system class loader and cannot see app classes: look classes up in JNI_OnLoad and keep a NewGlobalRef.
- Cache jclass (as global ref), jmethodID, jfieldID once.
- "Most apps should call RegisterNatives() from JNI_OnLoad()"; build with a version script or -fvisibility=hidden so only JNI_OnLoad is exported.
- @FastNative / @CriticalNative: public CTS-tested API from Android 14; work on 8-13 without guarantee; require RegisterNatives for safety below 12; not for long-running or lock-taking methods. @CriticalNative: no objects, no JNIEnv/jclass params.
- Arrays: prefer Get/Set<Type>ArrayRegion for copy in/out; every Get...Elements needs a Release; JNI_ABORT to drop changes.
- Strings: NewStringUTF needs Modified UTF-8; GetStringUTFRegion with a stack buffer avoids allocation for short strings.
- Raw shared data: "If there's no clear winner, use a direct byte buffer" (GetDirectBufferAddress).
- No JNI calls with an exception pending; C++ exceptions must not cross the JNI boundary.
- Pointers to native structs are stored in a Java long.

## AChoreographer (ndk/reference/group/choreographer, android/choreographer.h, libandroid)
- AChoreographer_getInstance(): API 24. "This must be called on an ALooper thread." Instance is per thread.
- AChoreographer_postFrameCallback64: API 29. One-shot; re-post to keep running. Frame time is CLOCK_MONOTONIC ns.
- AChoreographer_postVsyncCallback(choreographer, AChoreographer_vsyncCallback, data): API 33. One-shot. Callback gets AChoreographerFrameCallbackData (does not outlive the callback).
- AChoreographerFrameCallbackData_getFrameTimeNanos: API 33; "should not be used to advance animation clocks", use ...getFrameTimelineExpectedPresentationTimeNanos(data, index) for that. Also ...getFrameTimelinesLength, ...getPreferredFrameTimelineIndex, ...getFrameTimelineVsyncId, ...getFrameTimelineDeadlineNanos: all API 33.
- AChoreographer_registerRefreshRateCallback / unregister: API 30, thread-safe.
- Page note: the frame callback is the place for per-frame state update and rendering; "Input and sensor events should not be handled in the Choregrapher callback."
- OPEN POINT for the port: today Kotlin's Choreographer.FrameCallback hands frameTimeNanos to native pacerOnVsync. To keep pacing identical, the native callback must feed the same time value (postFrameCallback64's frameTimeNanos is the equivalent of the Java FrameCallback's). Switching to expected-presentation time would change dt inputs: not part of this port.

## ANativeWindow (ndk/reference/group/a-native-window, android/native_window.h)
- ANativeWindow_fromSurface(env, surface) (android/native_window_jni.h): returns an acquired reference; release with ANativeWindow_release. ANativeWindow_acquire/release are ref counts.
- ANativeWindow_setFrameRate(window, frameRate, compatibility): API 30; same as ...WithChangeStrategy(..., ONLY_IF_SEAMLESS). ...WithChangeStrategy: API 31. ANativeWindow_clearFrameRate: API 31.
- Compatibility: ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT = 0 (the Java code passes Surface.FRAME_RATE_COMPATIBILITY_DEFAULT with the two-argument overload, i.e. only-if-seamless: ANativeWindow_setFrameRate is the exact twin), FIXED_SOURCE = 1, AT_LEAST = 2.
- frameRate 0 = no preference (matches the Kotlin comment). "If this ANativeWindow is consumed by something other than the system compositor, e.g. a media codec, this call has no effect."
- ANativeWindow_setBuffersGeometry(window, w, h, format): scales buffers to the window; 0 restores base.

## Thermal (ndk/reference/group/thermal, android/thermal.h, libandroid)
- AThermal_acquireManager / releaseManager / getCurrentThermalStatus / registerThermalStatusListener / unregister: API 30. Status callback is "called on system binder thread pool" (so native needs an atomic hand-over, not a JNI call).
- AThermal_getThermalHeadroom(manager, forecastSeconds): API 31. "no benefit to calling this function more frequently than about once per second, and attempted to call significantly more frequently may result in the function returning NaN."
- AThermalStatus: NONE 0, LIGHT 1, MODERATE 2, SEVERE 3, CRITICAL 4, EMERGENCY 5, SHUTDOWN 6, ERROR -1.

## Stable APIs (ndk/guides/stable_apis)
- libandroid: asset, choreographer, configuration, input, looper, native window, sensor, storage, surface texture, trace (API 23), thermal. libnativewindow for newer native-window functions.
- libEGL: API 9; EGL lets native code list configs, create/destroy contexts and surfaces, swap.
- libGLESv3: GLES 3.0 API 18, 3.1 API 21, 3.2 API 24 (device must still be queried).
- libmediandk: AMediaCodec / AMediaExtractor / AMediaMuxer / AMediaFormat.
- libaaudio: API 26; the page recommends Oboe.
- libjnigraphics: AndroidBitmap_getInfo / lockPixels / unlockPixels, API 8 (native can fill a Java Bitmap's pixels, it cannot create one).
- APIs newer than minSdk are not callable by default.

## Still to fetch before the PRs that need them (not yet read)
- NdkMediaCodec / NdkMediaMuxer / NdkMediaExtractor function pages and API levels (the group page is 370 KB; read by offset). Key open question: AMediaMuxer output formats (is fragmented MP4 available? Cast uses Media3's FragmentedMp4Muxer).
- Sensor group page (saved raw under tool-results): ASensorManager_getInstanceForPackage, ASensorEventQueue_registerSensor, ASENSOR_TYPE_GAME_ROTATION_VECTOR, looper rules.
- EGL: EGL_ANDROID_presentation_time and EGL_ANDROID_recordable for encoder surfaces (Khronos registry), eglPresentationTimeANDROID.
- AAudio / Oboe input stream for the mic.
- ALooper (ALooper_prepare, ALooper_pollOnce, ALooper_wake, ALooper_addFd).
- NDK header check of every API level above, in the NDK version the build pins.

## Android guides, notes 2 (fetched 2026-11-12)

All five URLs fetched with WebFetch and returned the full page text; no fetch failed.
Quotes are verbatim from the fetched pages. Lines marked "Implies" are my reading for the Geode design, not page text.
Several things the brief expected are **not on these pages**; each section says which.

## 1. Capture a heap dump (`/studio/profile/capture-heap-dump`)

**The `#jni-references` section no longer exists.** The page has no text on JNI global references or a "JNI heap".
- Headings present: Why you should profile your app memory; Heap dump overview; Detect duplicate bitmaps; Find memory leaks; Trigger memory leaks for testing; Export and import a heap dump recording; Additional resources.
- The heap menu lists only three heaps: "**App heap (default)**", "**Image heap**", "**Zygote heap**". No JNI heap entry.
- `/studio/profile/memory-profiler` (the link jni-tips still uses for `#jni-references`) now serves this same page, also without the section.
- `/studio/views/capture-heap-dump-views` has no JNI section either.
- The only surviving official sentence on the topic is on jni-tips: "To browse global JNI references and see where global JNI references are created and deleted, use the **JNI heap** view in the Memory Profiler in Android Studio 3.2 and higher." Its target is gone, so how leaked global refs look in the profiler is undocumented on these pages.

What the page does say:
- A heap dump shows "which objects in your app are using up memory at the time of the capture". It is a Java-heap tool.
- "**Native Size**: Total amount of native memory used by this object type (in bytes). You will see memory here for some objects allocated in Java because Android uses native memory for some framework classes, such as `Bitmap`."
- "**Shallow Size**: Total amount of Java memory used by this object type". "**Retained Size**: Total size of memory being retained due to all instances of this class".
- "While dumping the heap, the amount of Java memory might increase temporarily. This is normal because the heap dump occurs in the same process as your app".
- Capture needs "**Profiler: run 'app' as debuggable (complete data)**"; for an exact moment call `dumpHprofData()`.
- Leak filter: "**Show activity/fragment leaks**". "detecting `Activity` leaks is critical, because leaking the host `Activity` will retain the entire Compose hierarchy and all active state trees in memory."
- Leak causes listed: "Long-lived references to `Activity` or `Context`", "lambdas that capture `Context`", "Forgetting to clean up listeners or observers in the `onDispose` block of a `DisposableEffect`", "Caches that hold objects longer than necessary."
- "if it leaks memory, it can retain that memory even while it's in the background ... Eventually, the system is forced to kill your app process to reclaim the memory."
- Ways to provoke leaks: rotate repeatedly; "Switch between your app and another app while in different activity states."

Implies:
- A heap dump will not show the core's own memory (PCM rings, FBO textures, AHardwareBuffers, shader caches). "Native Size" covers only native memory hanging off Java objects. The core needs its own byte accounting and a different tool (native allocation profiling, `dumpsys meminfo`).
- If native later caches a `JavaVM` and global refs, a global ref to a Surface holder, callback or Activity is a GC root: it would appear in a heap dump as a retained Activity/Context and trip the activity-leak filter. Today's "no global refs" design has nothing to leak here; keep it that way or pair every `NewGlobalRef` with a delete at surface/service teardown.
- The Kotlin shell is where the page's leak causes apply: Surface callbacks, service connections and `DisposableEffect` blocks that hold a native handle must release in `onDispose`/`onDestroy`.
- Heap dumps need a debuggable build, so they measure debug, not release.

Changed / version notes: "The **Native Size** column isn't available for devices running OS versions lower than Android 7.0" (irrelevant at minSdk 33). Duplicate-bitmap detection "starting with Android Studio Narwhal 4". "Beginning with Android Studio Quail 2" the profiler has a LeakCanary task. JNI heap view documentation removed.

## 2. Overview of memory management (`/topic/performance/memory-overview`)

Not on this page: `onTrimMemory` levels and what to release at each, any statement on how native (malloc/GL/AHardwareBuffer) memory is counted, the low memory killer by name, 16 KB pages. It only names `TRIM_MEMORY_EVENTS`.

What the page says:
- "any memory an app modifies---whether by allocating new objects or touching mapped pages---remains resident in RAM and cannot be paged out."
- Exception: "any files mmapped in without modification, such as code, can be paged out of RAM if the system wants to use that memory elsewhere."
- Static data is mmapped and shared, including "traditional project elements like native code in `.so` files."
- "Android shares the same dynamic RAM across processes using explicitly allocated shared memory regions (either with ashmem or gralloc). For example, window surfaces use shared memory between the app and screen compositor".
- "This (PSS) total is what the system considers to be your physical memory footprint." PSS "accounts for both dirty and clean pages that are shared with other processes---but only in an amount that's proportional to how many apps share that RAM."
- "Android sets a hard limit on the heap size for each app ... it can receive an `OutOfMemoryError`." `getMemoryClass()` "returns an integer indicating the number of megabytes available for your app's heap." (This is the Dalvik/ART heap.)
- "paired allocations and deallocations of large chunks should result in reclaiming all (or nearly all) the physical memory used. However, reclaiming memory from small allocations can be much less efficient" (said of the Dalvik heap).
- GC "in the middle of an intensive processing loop like an animation or during music playback ... can increase processing time ... past the recommended 16ms threshold".
- "Android keeps apps that are not foreground---that is, not visible to the user or running a foreground service like music playback--- in a cache."
- "If your app has a cached process and it retains resources that it currently does not need, then your app ... affects the system's overall performance. As the system runs low on resources like memory, it kills processes in the cache. The system also accounts for processes that hold onto the most memory and can terminate them to free up RAM."
- "The less memory your app consumes while in the cache, the better its chances are not to be killed ... However ... it's possible for cached processes to be terminated at any time no matter their resource utilization."
- Low-memory signals under stress: "SIGABRT: This is a fatal, native crash for your process due to requesting allocations of size larger than free memory, while the system is already under memory pressure." and "`TRIM_MEMORY_EVENTS`: These callbacks are available on Android 4.1 (API level 16) and higher, and provide detailed memory alerts for your process."

Implies:
- The Java heap limit (`getMemoryClass`, `OutOfMemoryError`) does not bound the core's buffers; nothing on this page caps native memory except the kill decision. The footprint the system judges is PSS, and every touched native page (rings, decoded frames, CPU-side texture copies) is dirty and unpageable. Size native caches by PSS, not by Java heap class.
- Only unmodified mmapped files are reclaimable without a kill. Read-only assets (shader source, tables, packs) should be mmapped read-only and not copied into malloc memory.
- A failed large native allocation under pressure is a SIGABRT, not a catchable error. Allocate rings, FBO chains and codec buffers once at prepare, check for failure, and never grow them on a real-time thread.
- Prefer few large paired allocations over many small ones so freeing actually returns pages.
- While the playback or export foreground service runs, the process is not cached. Once neither runs and no UI is visible, the process is cached and can be killed "at any time": native state that matters (export progress, unsaved params) must already be persisted through the shell, and no native thread may assume it gets to finish.
- On leaving the foreground with no service, the core should drop what it can rebuild: FBO textures, AHardwareBuffers, shader program caches, EGL surface (and context if cheap to rebuild), codec instances; keep only the PCM ring if playback continues. The shell must forward `onTrimMemory` to a native release call, since native gets no such callback itself. Level meanings are not on this page (see the ComponentCallbacks2 reference).
- The GC point supports the port: no JVM allocation on GL or audio threads means no GC pause inside a 16 ms frame or an audio callback.
- Test with `stressapptest` (`-s 20 -M 990` example on the page) to see the SIGABRT and trim paths.

Changed / version notes: the page still speaks of "ART and Dalvik" and a generational Dalvik heap; no recent-version changes are stated.

## 3. Processes and threads overview (`/guide/components/processes-and-threads`)

Not on this page: the importance hierarchy (foreground / visible / service / cached), foreground-service rules, and numeric ANR limits. It defers to "Processes and app lifecycle" and to the responsiveness guide. The only ANR wording is "more than a few seconds".

What the page says:
- "By default, all components of the same application run in the same process and thread, called the *main* thread."
- `android:process` on `<activity>`, `<service>`, `<receiver>`, `<provider>` or `<application>` can move a component to another process.
- "Android might decide to shut down a process at some point, when resources are required by other processes that are more immediately serving the user. Application components running in the process that's shut down are consequently destroyed."
- "it more readily shuts down a process hosting activities that are no longer visible on screen ... The decision of whether to terminate a process, therefore, depends on the state of the components running in that process."
- "The system does *not* create a separate thread for each instance of a component. All components that run in the same process are instantiated in the UI thread, and system calls to each component are dispatched from that thread." Lifecycle callbacks "always run in the UI thread of the process."
- "Performing long operations in the UI thread, such as network access or database queries, blocks the whole UI. When the thread is blocked, no events can be dispatched, including drawing events."
- "if the UI thread is blocked for more than a few seconds, the user is presented with the 'application not responding' (ANR) dialog."
- "the Android UI toolkit is *not* thread-safe." The two rules: "Don't block the UI thread." and "Don't access the Android UI toolkit from outside the UI thread."
- "If you have operations to perform that aren't instantaneous, make sure to do them in separate *background* or *worker* threads."
- Ways back to the UI thread: `Activity.runOnUiThread`, `View.post`, `View.postDelayed`; the Kotlin sample uses `lifecycleScope.launch` with `withContext(Dispatchers.IO)`.
- Binder: when a call "originates in another process, the method executes in a thread chosen from a pool of threads ... It's not executed in the UI thread". "`IBinder` methods must be implemented to be thread-safe." Same for `ContentProvider` `query/insert/delete/update/getType`.
- "a service's `onBind()` method is called from the UI thread of the service's process".

Implies:
- All four services (playback, export, wallpaper, dream) and `MainActivity` share one process and one main thread, so every lifecycle callback of every service is serialized on it. A JNI call from any of them that blocks (joining the render thread, waiting for a codec flush, a mutex the GL or audio thread holds, `eglMakeCurrent` teardown) stalls Compose and every other service at once.
- JNI entry points called from the shell must be non-blocking: post a command to the core's queue and return; state comes back by polling. Surface destroy is the hard case: the shell must not return from `surfaceDestroyed` before native stops using the window, so that wait must be short and bounded, never behind a frame or a codec call.
- Polling native state from the main thread is fine only if the read is lock-free or wait-free (atomics, a seqlock, a snapshot swap), never a lock a worker can hold.
- Native threads have no importance of their own: the page ties process survival to component state. A render, audio or encode thread keeps the process alive for nothing; only a running service or visible activity does. Export and background playback therefore need their foreground service for as long as the native work runs, and native must stop work when the shell says the component is gone.
- Process death destroys all components and all native threads together with no native callback. Do not depend on destructors, thread joins or `atexit` to finalize an MP4 or flush state.
- If native ever calls back into Kotlin, the callback arrives on a native thread and must not touch Compose state or Views directly; it must post to main. Keeping today's poll-only model satisfies rule 2 by construction.
- The media session binder, the notification listener and any `FileProvider` call can enter on binder pool threads. If such a path reaches a JNI call, the core's command channel must accept calls from more than one thread.
- Moving a service to its own `android:process` would give it a separate copy of the core and its buffers; the one-engine design assumes one process.

Changed / version notes: none stated. The page no longer mentions `AsyncTask`; the samples are coroutines and `Executor`. It notes "under special circumstances, an app's main thread might not be its UI thread."

## 4. Use the NDK with other build systems (`/ndk/guides/other_build_systems`)

Not on this page: sysroot flags, libc++ linking flags, lld, unwinder, 16 KB page-size linker flags, `--build-id`, fortify or stack-protector flags. It points build-system authors to the Build System Maintainers Guide (AOSP) for those.

What the page says:
- "The content described on this page requires at least NDK r19."
- "The NDK contains official support for ndk-build and CMake. Most users should refer to one of those guides for building application code. The purpose of this document is to describe how to build existing code that uses other build systems. This is often the case with third-party dependencies that are not Android-specific, such as OpenSSL and libbzip2."
- "The Clang compiler in the NDK is useable with only minimal configuration required to define your target environment."
- "pass the appropriate target with `-target` when invoking Clang", e.g. `$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/clang++ --target=aarch64-linux-android21 foo.cpp`.
- "The format of the prefix or target argument here is the target triple with a suffix denoting the `minSdkVersion`. This suffix is only used with clang/clang++; the binutils tools (such as `ar` and `strip`) do not require a suffix".
- Triples: arm64-v8a `aarch64-linux-android`; x86-64 `x86_64-linux-android`; armeabi-v7a `armv7a-linux-androideabi`; x86 `i686-linux-android`.
- Host tags: `linux-x86_64`, `darwin-x86_64` ("fat binaries that include M1 support"), `windows-x86_64`.
- "Invoking Clang directly with `--target` will be more reliable, as that is the most tested workflow, and there are occasionally argument forwarding bugs in the scripts."
- Autoconf/make variables: `AR=$TOOLCHAIN/bin/llvm-ar`, `CC="$TOOLCHAIN/bin/clang --target=$TARGET$API"`, `CXX="$TOOLCHAIN/bin/clang++ --target=$TARGET$API"`, `LD=$TOOLCHAIN/bin/ld`, `RANLIB=.../llvm-ranlib`, `STRIP=.../llvm-strip`, then `./configure --host $TARGET`.
- "The tools selected in this sample are correct for NDK r22 and newer. Older NDKs may require different tools."
- "Autoconf projects are generally not buildable on Windows." "Not all make projects support cross compiling ... It is very likely that the project will not build without modifications."

Implies:
- None of this applies to `libgeode.so` or `libprojectM-4.so`: they build through CMake under AGP, which sets target, API level and tools from `minSdk` and `abiFilters`. Do not hand-set `--target` or tool paths in `CMakeLists.txt`.
- It applies only if a third-party dependency is built outside CMake (an autoconf or make library; the prebuilt `liboboe.so` if it were ever rebuilt by hand; Mbed TLS or libdatachannel if not driven by CMake). Then: `--target=aarch64-linux-android33` and `--target=x86_64-linux-android33` (API suffix = minSdk 33), the r28 `llvm-ar`/`llvm-ranlib`/`llvm-strip`, and one build per ABI.
- A prebuilt built this way must match the app's API level, ABIs and C++ runtime choice (shared libc++); this page does not state the STL rule, so check the C++ library support page before mixing.
- The core's host tests in `core/tests` are a host build and are outside this page.

Changed / version notes: needs NDK r19+; older NDKs use `make_standalone_toolchain.py` (legacy path). Tool list valid for r22+ (LLVM binutils). Triple-prefixed clang wrappers still exist but direct `--target` is preferred.

## 5. JNI tips (`/ndk/guides/jni-tips`): same page, differences only

Confirmed the same page. Headings in order: General tips; JavaVM and JNIEnv; Threads; jclass, jmethodID, and jfieldID; Local and global references; UTF-8 and UTF-16 strings; Primitive arrays; Region calls; Exceptions; Extended checking; Native libraries; Faster native calls with `@FastNative` and `@CriticalNative`; 64-bit considerations; Unsupported features/backwards compatibility; three FAQs (`UnsatisfiedLinkError`, `FindClass`, sharing raw data).

Sections not in the list given in the brief:
- **64-bit considerations**: "use a `long` field rather than an `int` when storing a pointer to a native structure in a Java field." Applies to every native handle the shell holds.
- **Unsupported features/backwards compatibility**: "`DefineClass` is not implemented." "Beginning in Android 8.0, Android supports unlimited local references." `FindClass` throws `ClassNotFoundException`, not `NoClassDefFoundError`. All older caveats predate minSdk 33.
- Intro pointer to the JNI heap view (quoted in section 1); its target section is gone.

Points in listed sections that bear directly on this design:
- "Avoid asynchronous communication between code written in a managed programming language and code written in C++ when possible". "Minimize the number of threads that need to touch or be touched by JNI." Matches poll-only, no callbacks.
- "Android does not suspend threads executing native code." Unattached render, audio and codec threads are never paused by GC.
- "While executing a `@FastNative` or `@CriticalNative` method, the garbage collection cannot suspend the thread ... Do not use these annotations for long-running methods ... should not ... acquire native locks that can be held for a long time." Fits lock-free state polls only.
- These annotations "became CTS-tested public API in Android 14"; "dynamic lookup of native methods is supported only on Android 12+". At minSdk 33 they work, but explicit `RegisterNatives` is "strongly recommended" for such methods.
- "Most apps should call `RegisterNatives()` from `JNI_OnLoad()`" and build "with a version script (preferred) or use `-fvisibility=hidden`".
- "C++ exceptions ... must not be thrown across the JNI transition boundary".
- "Be careful using global references ... a solution with fewer global references is probably better."
