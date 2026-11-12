# Engine deep scan: everything under `engine/` on origin/main

Scanned commit: origin/main = a019454626940c143a5e14e8f52cf7e16bae59d2 (the Graphify index is built from the same commit). Read-only: nothing in the repository was touched.
How the numbers were taken: line counts by `git show origin/main:<path> | wc -l`; "callers outside the module" are counts of files that reference the type, taken with `git grep` on origin/main (Graphify `file_neighbors` was used to confirm the picture for `NativeViz` and `VisualizerRenderer`); tests were listed, not run.
Not compared: the port branch `ccr-0a1f1dcf-87b4sp-engine-to-cpp` is not on origin, so its progress is unknown to this scan.

Paths below are relative to `/home/user/geode-app`. `S/` = `engine/scenes/src/main/kotlin/dev/geode/`, `ST/` = `engine/scenes/src/test/kotlin/dev/geode/`, `C/` = `engine/audio-core/src/main/kotlin/dev/geode/engine/`.

## 1. Totals

| | files | lines |
|---|---|---|
| `engine/audio-android` main | 1 | 55 |
| `engine/audio-core` main | 5 | 3000 (GeodeNative 2659) |
| `engine/scenes` main | 47 | 4580 |
| **main Kotlin total** | **53** | **7635** |
| tests (audio-core 2, scenes 14) | 16 | 1392 |
| build files (3 `build.gradle.kts`, 3 `detekt-baseline.xml`) | 6 | 59 |

No manifest, consumer rules, resources or androidTest exist under `engine/`.

Per category (each file counted once, by its main role):

| Category | files | lines | Files |
|---|---|---|---|
| JNI declaration | 1 | 2659 | GeodeNative |
| LOGIC (computes, decides, or runs off main) | 9 | 1610 | NativeViz 509, VisualizerRenderer 313, FramePacer 248, SearchMatcher 131, OffscreenSceneRenderer 128, MilkPresetMemory 116, LookApplier 97, MilkStarterPack 50, PlaybackMath 18 |
| Value holder mirroring native state | 16 | 1248 | SceneParams 272, Reactions 229, ParamKeys 158, SampleRing 95, PerformanceSettings 94, SoundTuning 76, SceneIds 70, LookSnapshot 70, SoundCursor 39, Adsr 32, StyleSchedule 31, BlendMode 24, UnderlayBlend 22, SceneFactory 14, TouchField 13, TransitionStyle 9 |
| Read of a native table | 8 | 770 | Lfo 214, Catalogue 131, ParamScope 94, ParamSchema 90, EventRecipe 86, TransitionCatalog 79, SceneCapabilities 39, StyleFamilies 37 |
| Platform hand-over | 7 | 593 | VisualizerView 220, TrackAudio 158, AiffPcm 57, PcmTap 55, GyroLook 50, ThermalGovernor 38, FluidShaderTemplate 15 |
| Presentation only | 5 | 296 | ParamFormat 153, VisualStyleCatalog 106, BestEffort 18, FluidQuality 13, CyclicPalettes 6 |
| Wire codec | 4 | 287 | SceneParamsCodec 80, ModConfigCodec 79, ReactionRouteCodec 77, BuildStyleReactionsUseCase 51 |
| Dead / unused in production | 3 | 172 | SoundFrameLayout 131, ReactSourceCatalog 29, MilkdropEngine 12 |

Reading of the totals: only 1610 lines are logic in the strict sense, but 2305 more (holders, table reads, codecs) exist only because Kotlin keeps a second copy of state native already owns. The JNI file is 35 % of the engine and is the only part the owner's rule allows to stay.

## 2. Build files

| File | Lines | Content |
|---|---|---|
| `engine/audio-core/build.gradle.kts` | 6 | `geode.jvm-library`, no dependencies: pure JVM, which is why `GeodeNative` types `assets` as `Any` |
| `engine/audio-android/build.gradle.kts` | 13 | android-library; `api` audio-core; Media3 exoplayer and common |
| `engine/scenes/build.gradle.kts` | 12 | android-library; `api` audio-core; coroutines-android |
| `engine/audio-core/detekt-baseline.xml` | 6 | empty baseline |
| `engine/audio-android/detekt-baseline.xml` | 9 | names `SinkClockDriver.kt`, a file that no longer exists |
| `engine/scenes/detekt-baseline.xml` | 13 | names `AdsrEngine.tick`, `LfoEngine` loops and a `MilkdropEngine` catch: code that is gone |

Outside `engine/` but bound to it: `app/proguard-rules.pro` line 14 keeps `GeodeNative` member names, line 58 keeps `SceneParams` field names (the reflection codec depends on it).

## 3. `GeodeNative.kt` and `NativeViz.kt`

### `C/bridge/GeodeNative.kt` (2659 lines)
- **363 `external fun`**, 132 `const val` mirrors of `GEODE_*` constants, about 1134 comment and blank lines.
- Not a declaration: `init { System.loadLibrary("geode") }` and one real function, `fun frameLiquidMode(due: Int): Int = (due ushr 8) and 0xF` (a bit decode of the frame's due word: logic, belongs behind a native getter or in the due-bit constants).
- Externals by prefix: viz 50, export 25, macro 24, pcm 22, dsp 22, audio 15, playout 14, link 11, recorder 10, cast 9, loudness 8, loop 8, webrtc 7, track 7, surface 7, stage 7, sound 7, record 7, underlay 6, params 6, customize 6, library 5, colour 5, aiff 5, hls 4, frame 4, auto 4, the rest in ones to threes.
- Referenced by 96 files outside the module; heaviest: `data/MacroStore` 26, `export/LoopRender` 23, `audio/dsp/NativeDspProcessor` 18.
- Native side: `core/api/geode_api.h` through `app/src/main/cpp/*_jni.cpp`.

### `S/render/bridge/NativeViz.kt` (509 lines)
- **0 `external fun`.** The whole file is non-declaration code:
  - handle lifecycle: `@Synchronized create/destroy`, a `Cleaner` (`Owned`);
  - latched state replayed on every new handle: UI blur region (6 floats), edge glow options, cast source, a pending still request, the milk texture directory, `onCreated`;
  - `LastSent`, a dedupe cache (reduced motion, transition, user reactions by identity and hand mask);
  - `pollError` change detection; `deliverStill` (direct ByteBuffer to `Bitmap`);
  - `setStyleSchedule` array marshalling; per-slot LFO and ADSR packing; implements `LookTarget`.
- Dead in production: `cut()` (no caller), `releaseScenes()` on NativeViz (callers go through `VisualizerRenderer.releaseScenes`, which calls `destroy`), `writeAudio` (androidTest only: `StoreFramesTest`, `StyleThumbnailRenderTest`), `LastSent.look` and `LastSent.pacedFps` (only `LastSentTest`).
- Category LOGIC. Native target: the renderer handle keeps its own latches and last-sent state (`core/viz/frame/PushVersions` is the existing relative). The `Bitmap` hand-over must stay Kotlin (no NDK bitmap creation).

## 4. Per-file map

Columns: lines; declarations; what it does; thread; category; native counterpart; callers outside the module; saved formats; tests; NDK replacement.

### `engine/audio-android`

| File | Row |
|---|---|
| `engine/audio-android/src/main/kotlin/dev/geode/engine/audioandroid/PcmTap.kt` | 55. `class PcmTap(ring) : TeeAudioProcessor.AudioBufferSink`. `flush` starts a ring epoch and stores the format; `handleBuffer` hands the direct buffer, encoding, channels and rate to `ring.write` (a heap buffer goes through one direct copy). Reads no sample. Thread: Media3 playback thread. **Platform hand-over.** Native: `core/audio/ingest/PcmRing`. Callers: 1 (`playback/PlaybackEngine`). No saved data, no test. NDK: none, the sink is a Media3 Java interface; stays. |

### `engine/audio-core`

| File | Row |
|---|---|
| `C/audio/SampleRing.kt` | 95. `class SampleRing`: handle on `geode_pcm_ring_*`, freed by a Cleaner; `write` overloads (FloatArray, ShortArray, ByteArray, direct ByteBuffer), `heardAt`, `beginEpoch`, `writtenFrames`. Any thread, one writer. **Value holder (handle).** Native: `core/audio/ingest/PcmRing`. Callers: 15 files (`playback/PlaybackEngine`, `wallpaper/SoundSource`, `wallpaper/OutputMixCapture`, `cast/CastStream`). Tests: native `core/tests/pcm_ring_test.cpp`. Stays as a handle; shrinks if capture moves native. |
| `C/audio/SoundCursor.kt` | 39. `class SoundCursor`: Cast's int16 reader of the ring (`pcmCursorTakeS16`). Cast thread. **Value holder (handle).** Native: `Cursor`. Caller: 1 (`cast/CastStream`). Goes when Cast's audio feed is native. |
| `C/audio/SoundFrameLayout.kt` | 131. `object SoundFrameLayout`: 109 float offsets of `GeodeSoundFrame` plus sizes (SCALARS 113, EQ 16, SPECTRUM 64, WAVEFORM 128, CHROMA 12x3, TUNING_FLOATS 30), under `@Suppress("unused")`. Only `TUNING_FLOATS` is read by main code. **Dead** apart from one constant. Native: `core/sound/SoundFrame.h`. Test: `SoundFrameLayoutTest` (177) is its only reader. Delete; move `TUNING_FLOATS` to `SoundTuning`. |
| `C/audio/SoundTuning.kt` | 76. `class SoundTuning`: 30 floats, slot constants (GAIN = 0 … INPUT_TRIM_DB = 29), clamp and defaults through `soundTuningClamp` / `soundTuningSchema`; `fromFloats` accepts 30 floats or the old 29 (pads 0 dB). Main thread, read on GL. **Value holder.** Native: `core/sound/Tuning`. Callers: 10 files (`ui/AudioSettings` 26 refs, `data/SoundInputSettings`). Saved: a float list in `GuiPrefs` (null = defaults); the 29-float legacy rule must survive. Test: `SoundTuningTest` (74). |
| `C/bridge/GeodeNative.kt` | 2659. See section 3. **JNI declaration.** |

### `engine/scenes`: `analysis`, `audio`, `util`

| File | Row |
|---|---|
| `S/analysis/PlaybackMath.kt` | 18. `semitonesToRatio` (2^(s/12)), `formatCountdown`, `snap`. Main. **LOGIC** (small) with one formatter. No native counterpart. Callers: `playback/PlaybackEngine` (pitch ratio into `PlaybackParameters`), `ui/PlaybackSettings`. No test. Port the ratio and snap to `core/audio/dsp`; the formatter is UI and should leave the engine module. |
| `S/analysis/SearchMatcher.kt` | 131. `object SearchMatcher`: `fold` (NFD, Locale.ROOT, spelled-out letters), `terms`, `matches`, `tier`, `filterTracks`. Runs on `Dispatchers.Default` from Library. **LOGIC**, a duplicate: its own KDoc says it is to follow `core/library/Search` and to gain no callers. Callers: `ui/AppShell`, Library browse, `playback` Android Auto search. Test: `SearchMatcherTest` (80). NDK: nothing needed (native Search folds itself; libicu is there from API 31 if wanted). |
| `S/audio/AiffPcm.kt` | 57. `class AiffPcm`: opens an `AssetFileDescriptor`, `detachFd`, hands it to `aiffOpenFd`; read, progress, close. Export thread. **Platform hand-over.** Native: `core/audio/ingest/AiffReader`. Callers: `TrackAudio`, `export/AudioTranscoder`. Stays as the fd opener (content URIs have no NDK path). |
| `S/util/BestEffort.kt` | 18. `bestEffort(tag, what, block)`. Any thread. **Presentation/shell utility** living in the engine module. Callers: 35 files. Move to an app or model module. |

### `engine/scenes`: `render`

| File | Row |
|---|---|
| `S/render/Adsr.kt` | 32. `enum EnvBand(label)`, `data class AdsrConfig` (attack .05, decay .25, sustain .5, release .35, amount .5, band BASS, gate .25), `object AdsrEngine { COUNT = 2 }`. **Value holder**; defaults duplicate `core/viz/Adsr`. Saved: `LfoStore` JSON by enum NAME. No own test (`WireLayoutTest` covers the wire). |
| `S/render/BlendMode.kt` | 24. `enum BlendMode` (8), ordinal was the wire for `layerSet` (since deleted; native owns the blends), `fromOrdinal` defaults to SCREEN. **Value holder.** Append only. |
| `S/render/CyclicPalettes.kt` | 6. Five names of the rows in `shaders/cyclic_palettes.bin`. **Presentation.** |
| `S/render/EventRecipe.kt` | 86. `enum EventRecipe` (30; title, sentence; ordinal is the wire, append only); `routesFor`, `isOn`, `fits`, `toggle` call `eventRecipe*`. Main. **Read of a native table** plus titles. Native: `core/viz/ReactionDna`. Test: native `viz_math_test`. |
| `S/render/FramePacer.kt` | 248. `FrameSink`, `PacedRate`, `FrameRatePolicy` (Native, Capped), `FramePacer`, private `PacerThread` (a HandlerThread "GeodeFramePacer" at display priority with its own Choreographer). The clock maths is native (`pacerOnVsync`, `pacerResume`, `pacerEffectiveCap`); Kotlin owns the thread, the vsync callback, start/stop and `Surface.setFrameRate`. **Off the main thread: LOGIC, breaks the owner's rule.** Native: `core/viz/frame/Pacer`. Callers: VisualizerView, the wallpaper service, the dream, Cast (`PacedRate`). NDK: `AChoreographer_postVsyncCallback` (API 33 = minSdk) and `ANativeWindow_setFrameRate` (30). |
| `S/render/GyroLook.kt` | 50. `class GyroLook : SensorEventListener` on TYPE_GAME_ROTATION_VECTOR; writes `renderer.gyroReading` (x, y, z, w, display rotation) and bumps the session. Main looper. **Platform hand-over.** Native: `core/viz/GyroLook.hpp`. Test: `GyroListensTest` (16), native `gyro_look_test.cpp`. NDK: `ASensorManager` / `ASensorEventQueue`; display rotation still has to be handed over from Kotlin. |
| `S/render/Lfo.kt` | 214. Enums `LfoWave`, `ModSource`, `ModPolarity`, `ModCurve`, `LfoTarget` (60 incl. RETIRED_5/25/37/38; ordinal = wire, name = saved); `data class LfoConfig`; `object ModTable` (parses `geode_mod_targets_text` v2: reach masks, offers, natural polarity, slot defaults); `object LfoEngine { SLOTS = 3 }`. Main. **Read of a native table** plus labels. Native: `core/viz/ModTargets`, `core/viz/Lfo`. Saved: `LfoStore` JSON by enum name. Tests: `ModTableTest` (53), native `mod_targets_test.cpp`. |
| `S/render/MilkPresetMemory.kt` | 116. `object MilkPresetMemory`, `Store`, `PrefsStore` (file `geode-viz`, key `milk_path`), FIRST_RUN_PRESET `geode_bloom.milk`. Restore rule: last shown, else requested, else first run; `loaded()` is called on the GL thread and writes prefs; `firstRun()` is synchronized and installs the starter pack. **LOGIC** plus storage, partly on the GL thread. Native relative: `core/viz/MilkShown.hpp`. Callers: 5 files (`data/VizStateStore`, `GeodePrefsFiles`, `PlayerSession`, wallpaper). Saved: that prefs key, must stay. Test: `MilkPresetMemoryTest` (103). NDK: no SharedPreferences; the rule goes native, Kotlin keeps the read and write on main. |
| `S/render/PerformanceSettings.kt` | 94. `data class PerformanceSettings` (renderScale, fpsCap, marchScale, postFx, adaptive, liquidHot), `mode` match, tiers from `perfTier`, `modeForDevice`; `enum LiquidHotPolicy`, `enum PerformanceMode`. Main. **Value holder** with a small rule (`mode`). Native: `core/viz/frame/Quality`, `LiveFps`. Saved by `data/PerformanceStore`. Test: `PerformanceSettingsTest` (18). |
| `S/render/ReactSourceCatalog.kt` | 29. `ReactSourceInfo` and a parser of "NAME\|Label\|GROUP\|0/1". **Dead**: no main-code caller, only `ReactSourceCatalogTest` (25) and the app's `ReactSourceCatalogDeviceTest`. It is, though, the shape the `ReactSource` enum should be read through. |
| `S/render/ReactionRouteCodec.kt` | 77. Saved-data codec `reactions/2`: `enabled\|SOURCE\|TARGET\|MODE\|amount\|CURVE\|attackSeconds\|releaseSeconds`, clamps (amount ±2, attack ≤ 5, release ≤ 10), 12 routes at most. **Wire codec** (saved format, with clamping logic). Caller: `data/LfoStore`. Test: `ReactionRouteCodecTest` (103). Native has no reader of this text yet. |
| `S/render/Reactions.kt` | 229. `enum ReactSource` (117, a hand-kept mirror of `core/viz/Reactions.hpp` with labels, groups and `isEvent` duplicated), `ReactSourceGroup`, `ReactMode`, `data class ReactionRoute` (MAX_ROUTES 12), `HandReactions`, `object HandMask` (process-wide mirror plus `handSet`). **Value holder**, the largest hand-kept duplicate of a native list. Saved by enum name through the codec. |
| `S/render/SceneFactory.kt` | 14. Interface: `sceneId`, `milkPresetPath`. **Value holder.** |
| `S/render/ThermalGovernor.kt` | 38. `addThermalStatusListener` pushes the status (main); `sample()` on the GL thread reads `getThermalHeadroom(30)` and pushes it. **Platform hand-over.** Native: `core/viz/frame/Thermal`. Caller: VisualizerRenderer only. NDK: `AThermal_registerThermalStatusListener` (30), `AThermal_getThermalHeadroom` (31): fully replaceable. |
| `S/render/TouchField.kt` | 13. `MAX_POINTS = 5`. **Value holder** (constant mirror). |
| `S/render/TransitionCatalog.kt` | 79. `Def`, `Param`, built-in ids, `library(context)` parses `transitionsTable(assets)`. Main. **Read of a native table.** Native: `core/viz/TransitionLibrary.hpp`. |
| `S/render/TransitionStyle.kt` | 9. `enum` CUT, FADE, MELT, SLIDE, ZOOM; lowercase name is the saved transition id. **Value holder.** |
| `S/render/UnderlayBlend.kt` | 22. `enum` SCREEN, MULTIPLY, ADD; ordinal is wire and saved (`BackgroundPrefsStore`). **Value holder.** |
| `S/render/VisualizerRenderer.kt` | 313. `class VisualizerRenderer : GLSurfaceView.Renderer`. The `look` setter bumps per-part versions by identity; `onDrawFrame`: `frameBegin` and its due bits, `LookApplier.apply`, gyro session reset, `feedRing`, `render`, `pollError`, the milk-loaded and director-moment hand-backs; `liquidMode` StateFlow; `submitShader`, `submitLayer` (since gone: native keeps the layer). **Every frame on the GL thread in Kotlin: LOGIC, against ADR 0003.** Native: the `core/viz` renderer. Callers: wallpaper service, `WallpaperRenderer`, `cast/CastStream`, `ui/EnginePlumbing`, `ExportHost`, `StudioRender` and others. NDK: a native render thread with its own EGL context. |
| `S/render/VisualizerView.kt` | 220. `class VisualizerView : GLSurfaceView` (GLES3, preserved context, render when dirty), owns the pacer and gyro; `syncPacer` visibility rule, `trimSpares` on hide, release on detach behind a 200 ms latch, `setUiBlurRegion` by `queueEvent`; top-level `gyroListens`. Main, queues to GL. **Platform hand-over** with a visibility rule (native relative: `core/viz/StageRun`). Callers: 23 files. NDK: a plain `SurfaceView` handing `ANativeWindow_fromSurface` to native EGL; the View itself stays Kotlin. |

### `engine/scenes`: `render/bridge`, `fluid`, `look`, `offscreen`

| File | Row |
|---|---|
| `S/render/bridge/ModConfigCodec.kt` | 79. Float layouts: LFO 9, ADSR head 10 plus targets, reactions 12 x 8; `unpackReactions`. **Wire codec.** Test: `WireLayoutTest` (167). Goes when native owns the look. |
| `S/render/bridge/NativeViz.kt` | 509. See section 3. **LOGIC.** Test: `LastSentTest` (40). |
| `S/render/bridge/SceneParamsCodec.kt` | 80. Reflection codec between `SceneParams` and the float frame, by schema slot name; retired slots carry the default; unpack drops custom palette ids when the overrides are cleared. **Wire codec.** Callers: `ui/ModulationController` (15 refs), `data/PresetStore`, `BuiltInPresets`, `PaletteMaker`. Needs the proguard keep rule. |
| `S/render/bridge/StyleSchedule.kt` | 31. `data class` (cues, beatCutIds, everyBeats = 4, lockedStyleIds). **Value holder.** Native: `core/viz/StyleSchedule`. |
| `S/render/fluid/FluidQuality.kt` | 13. Five tier labels. **Presentation.** Native: `core/viz/fluid/FluidQuality.hpp`. |
| `S/render/fluid/FluidShaderTemplate.kt` | 15. Reads `shaders/fluid_splat_frag.glsl` for the shader editor. **Platform hand-over.** |
| `S/render/look/LookApplier.kt` | 97. `interface LookTarget`, `class LookApplier`: the diff of what to push (identity compare, due bits, hand mask). GL and export threads. **LOGIC.** Native relative: `core/viz/frame/PushVersions`. Test: `LookApplierTest` (179). |
| `S/render/look/LookSnapshot.kt` | 70. `data class LookSnapshot` (scene, params, 3 LFOs, 2 ADSRs, reactions, tuning, reduced motion, transition "fade" 1200 ms, underlay), `gentleFlight()`, `LookUnderlay`, `LookSource`. **Value holder.** No native "look" struct exists: a design item. |
| `S/render/offscreen/OffscreenSceneRenderer.kt` | 128. `OffscreenRenderSpec` and the renderer: `prepare` (offscreen, frame rate, scene, preset, schedule, pinned settings, pre-roll) and `renderFrame` (frame time, keyframed params callback, track target frame, PTS). Export thread. **LOGIC** (orchestration). Callers: `export/LoopRender`, `VideoExporter`, `ui/ExportController`. |
| `S/render/offscreen/TrackAudio.kt` | 158. MediaExtractor and MediaCodec decode loop, pushes direct buffers to `trackFeedPushDirect`; AIFF through `AiffPcm`. Export thread. **Platform hand-over** with a decode loop. Native: `core/audio/ingest/TrackFeed`. NDK: `AMediaExtractor_setDataSourceFd` and `AMediaCodec` (Kotlin opens the fd; `mediandk` is not linked yet, `CMakeLists.txt:140` links `log android` only). |

### `engine/scenes`: `render/scene`

| File | Row |
|---|---|
| `S/render/scene/BuildStyleReactionsUseCase.kt` | 51. Calls `styleReactions` / `styleReactionsHand`, unpacks; `packExact`. **Wire codec** over a JNI call. Native: `core/viz/ReactionDna`. |
| `S/render/scene/Catalogue.kt` | 131. Parses `geode_catalogue_text` v3 (kind, retired, style, family, picker, override, key, range, value rows); `CatalogueStyle`. **Read of a native table.** Callers: 16 files. Test: `CatalogueTest` (109), native `catalogue_test.cpp`. This is the allowed pattern. |
| `S/render/scene/MilkStarterPack.kt` | 50. Copies bundled `milk/*.milk` into `filesDir/milk`, marker `.starter-pack` VERSION 1. Can run on a GL thread through `MilkPresetMemory.firstRun`. **LOGIC** (storage). Caller: `ui/MilkImportController`. Saved: the marker file. Belongs in `data/`, called on IO. |
| `S/render/scene/MilkdropEngine.kt` | 12. `available` = `System.loadLibrary("geode")` succeeded. **Dead/vestigial** (GeodeNative loads the same library). 3 UI callers. |
| `S/render/scene/ParamFormat.kt` | 153. `enum ParamUnit` (BPM unused outside its test) and value formatting. **Presentation.** Test: `ParamFormatTest` (59). Belongs in the UI. |
| `S/render/scene/ParamKeys.kt` | 158. About 147 control-name strings, the identity of controls in the native `CustomizeMap` and lock set. **Value holder**: a hand-kept duplicate of native names. `ui/CustomizeControls` 137 refs. |
| `S/render/scene/ParamSchema.kt` | 90. Parses `geode_params_schema_text` v3; `ParamSpec`; `paletteNames`. **Read of a native table.** Native: `core/viz/ParamTable.inc`, `Params.hpp`. Test: `ParamSchemaTest` (126). |
| `S/render/scene/ParamScope.kt` | 94. `enum ParamScope` (22, names match native), delegates to Catalogue. **Read of a native table.** |
| `S/render/scene/SceneCapabilities.kt` | 39. `enum SceneKind` (9), `SHADER_SCENES`, `kindOf`, `isFluid`, `isCymatics`; `hasShaderLook` has no caller. **Read of a native table.** Callers: `pro/FreeTier`, `AutoVisualsController`. |
| `S/render/scene/SceneIds.kt` | 70. 66 id constants (DEFAULT `silk_web`). **Value holder**: hand-kept subset of the native catalogue. 20 outside files. |
| `S/render/scene/SceneParams.kt` | 272. A data class of about 165 fields whose defaults duplicate the native schema, label lists, sentinels, palette rules (`usesCustomPalette`, `withoutCustomPalette`, `withPaletteStops`). **Value holder** with hidden logic. Callers: 39 files (`ui/CustomizeControls` 143 refs). Saved: field names are the preset JSON keys and the schema names. |
| `S/render/scene/StyleFamilies.kt` | 37. `StyleFamily` and the list from Catalogue. **Read of a native table.** Test: `StyleFamiliesTest` (63). |
| `S/render/scene/VisualStyleCatalog.kt` | 106. Labels per style id and family; id lists per kind. **Presentation.** |

### Tests under `engine/`

| File | Lines | Covers | Fate when the subject moves |
|---|---|---|---|
| `engine/audio-core/src/test/.../SoundFrameLayoutTest.kt` | 177 | the dead offsets | delete with the offsets |
| `engine/audio-core/src/test/.../SoundTuningTest.kt` | 74 | tuning holder, 29-float legacy | keep the legacy case |
| `ST/analysis/SearchMatcherTest.kt` | 80 | folding and tiers | port cases to `library_search_test.cpp` |
| `ST/render/GyroListensTest.kt` | 16 | `gyroListens` | port to `gyro_look_test.cpp` |
| `ST/render/MilkPresetMemoryTest.kt` | 103 | restore rule | port with the rule |
| `ST/render/ModTableTest.kt` | 53 | mod-targets text | stays (table read) |
| `ST/render/PerformanceSettingsTest.kt` | 18 | `mode` | port with `mode` |
| `ST/render/ReactSourceCatalogTest.kt` | 25 | dead parser | delete or revive |
| `ST/render/ReactionRouteCodecTest.kt` | 103 | `reactions/2` | port; it pins a saved format |
| `ST/render/bridge/LastSentTest.kt` | 40 | dedupe cache, incl. dead members | delete with `LastSent` |
| `ST/render/bridge/WireLayoutTest.kt` | 167 | float wires | delete with the wires |
| `ST/render/look/LookApplierTest.kt` | 179 | push diff | port with the diff |
| `ST/render/scene/CatalogueTest.kt` | 109 | catalogue text | stays |
| `ST/render/scene/ParamFormatTest.kt` | 59 | formatting | moves with it to the UI |
| `ST/render/scene/ParamSchemaTest.kt` | 126 | schema text | stays |
| `ST/render/scene/StyleFamiliesTest.kt` | 63 | families | stays |

## 5. Engine-shaped Kotlin outside `engine/` (`app/src/main/java/dev/geode/`)

Only files that compute or decide engine things, or run off the main thread.

Export
- `export/VideoExporter.kt` (632): the export loop on Default; MediaCodec, muxer and offscreen-render orchestration.
- `export/LoopRender.kt` (753): loop render driver; 22 native calls, seam maths already native.
- `export/LoopExtend.kt` (642): repeats an encoded loop to long form; muxer work.
- `export/AudioTranscoder.kt` (510): AAC transcode loop.
- `export/LoudnessMeter.kt` (346), `export/LoudnessTarget.kt` (368): decode for BS.1770 and the platform advice (maths native).
- `export/EncoderSurface.kt` (90): EGL14 context and encoder surface on the export and cast threads.
- `export/ExportRange.kt` (31): the segment's carrier; the clamp is native (`core/output/ExportRange`).
- The export's time left is native (`core/output/RenderEta`); `ExportRun.describeEta` words it.
- `export/ChapterMarkers.kt` (233), `export/MuxerBudget.kt` (37), `export/PcmResampler.kt` (61), `export/WatermarkOverlay.kt` (97): thin or mixed.

Audio
- `audio/AiffExtractor.kt` (172): Media3 extractor that only moves bytes; native `AiffHeaderScan` (the one `AiffReader` uses) reads the header.
- `audio/AudioCapturePump.kt` (121), `MicCapture.kt` (98), `PlaybackCapture.kt` (82): AudioRecord worker threads that write the ring.
- `audio/OboeAudioOutput.kt` (175), `PlayoutSink.kt` (70), `audio/dsp/NativeDspProcessor.kt` (180), `MvzAudioProcessorChain.kt` (38): Media3 glue on the playback thread (must stay Java-side).
- `audio/AudioFxController.kt` (206): platform FX presets.

Cast
- `cast/CastStream.kt` (466): a "cast-render" thread with EGL; drives a fallback `VisualizerRenderer` by hand.
- `cast/CastEncoder.kt` (253): codec drain thread.
- `cast/LiveLink.kt` (132): the handle on the native LAN server (`core/cast/LiveServer`); the HLS cut, the request parser, the routes and the typed pairing are native.
- `cast/WebRtcLink.kt` (145), `ComputerLink.kt` (123), `CastMirror.kt` (100).

Record, wallpaper, viz, playback
- `wallpaper/VisualizerWallpaperService.kt` (472): its own GLSurfaceView subclass and FramePacer; `VisualizerDreamService.kt` (352); `WallpaperRenderer.kt` (36).
- `wallpaper/OutputMixCapture.kt` (126): `Visualizer` capture into the ring; `SoundSource.kt` (148); `WallpaperRunState.kt` (83).
- `viz/BackgroundImage.kt` (119): decode and crop on a worker.
- `playback/PlaybackEngine.kt` (301), `ReplayGain.kt` (92), `SleepTimer.kt` (126), `TrackCrossfade.kt` (52), `AutoDirectorFeed.kt` (77): hand targets to native; `PlaybackQueue` and `QueueOps` are pure queue rules.

## 6. Dependency-ordered port plan

**Step 0. Delete what is dead (no behaviour change, no saved data, no threads).**
`SoundFrameLayout` offsets and their test (keep `TUNING_FLOATS`); `ReactSourceCatalog` (or revive it in step 2); `MilkdropEngine`; `NativeViz.cut`, `NativeViz.releaseScenes`, `LastSent.look`, `LastSent.pacedFps`; `ParamUnit.BPM`; `SceneCapabilities.hasShaderLook`; the stale entries in the two detekt baselines. About 200 lines and one 177-line test.

**Step 1. Trivial: pure logic, no saved format, no thread ownership.**
1. `GeodeNative.frameLiquidMode` to a native getter.
2. `PlaybackMath.semitonesToRatio` and `snap` to `core/audio/dsp`.
3. `SearchMatcher` callers onto `core/library/Search`, then delete it.
4. `ThermalGovernor` to `AThermal_*` inside `core/viz/frame/Thermal`.
5. `PerformanceSettings.mode` to `core/viz/frame/Quality`.
6. `VisualizerView.gyroListens` and the `syncPacer` visibility rule to `core/viz/StageRun`.
7. Move non-engine files out of the module: `BestEffort`, `ParamFormat`, `VisualStyleCatalog`, `FluidQuality`, `CyclicPalettes`, `FluidShaderTemplate`, the countdown formatter.

**Step 2. Duplicated lists become table reads (needs care with names, not with threads).**
Native emits text for: the 117 react sources (labels, groups, `isEvent`), scene ids, control names (`ParamKeys`), `EnvBand`, wave, polarity and curve labels, blend modes, transition styles. Kotlin reads them the way `Catalogue`, `ParamSchema` and `ModTable` already do. The enum NAMES must not change: `LfoStore` saves them.

**Step 3. Wire and saved formats (needs a compatibility test per format before the move).**
1. `ReactionRouteCodec` (`reactions/2`) parse and clamp in native; Kotlin hands the stored string over.
2. `SoundTuning`: native reads the float list, including the 29-float legacy.
3. `MilkPresetMemory` restore rule to `core/viz/MilkShown`; Kotlin keeps only the `geode-viz` / `milk_path` read and write, on main. `MilkStarterPack` to `data/`, off the GL thread.
4. `NativeViz` latches, `LastSent` and `pollError` into the native handle; `LookApplier`'s diff into `core/viz/frame/PushVersions`.
5. A native look: `SceneParams`, `LookSnapshot`, `ModConfigCodec`, `SceneParamsCodec`, `StyleSchedule`, `BuildStyleReactionsUseCase` collapse into one native-owned look that the UI edits by control name. Preset JSON keys and backup stay as they are. This is the widest change (39 UI files) and depends on steps 2 and 3.4.

**Step 4. Needs a design first (threads and surfaces).**
1. `FramePacer` to `AChoreographer` on a native thread.
2. Native render thread with its own EGL context from `ANativeWindow_fromSurface`; `VisualizerRenderer` goes, `VisualizerView` becomes a plain `SurfaceView` shell. The wallpaper's GL view, the dream and Cast's fallback renderer follow the same host.
3. `GyroLook` to `ASensorManager` (display rotation still handed over).
4. `TrackAudio` and `OffscreenSceneRenderer` to `AMediaExtractor` / `AMediaCodec` with an fd from Kotlin (add `mediandk` to the link).
5. Then the app side: export loops, `EncoderSurface`, Cast and Record encoders, capture pumps.

Depends-on summary: 4.2 needs 4.1 and 3.4; 3.5 needs 2 and 3.4; 4.4 is independent of the live path and can run in parallel with 3; 4.5 needs 4.2 and 4.4.

## 7. The five riskiest moves

1. **GLSurfaceView to a native EGL render thread** (`VisualizerView`, `VisualizerRenderer`, plus the wallpaper's own GL view, the dream and Cast's hand-driven fallback). Risk: context loss and the preserved context, PiP and second-screen surfaces, the 200 ms teardown latch, main-thread calls that wait on the GL thread. Test first: a host test of the surface state machine (create, change, destroy, re-create in every order) and a device checklist for PiP, rotation, second screen, wallpaper preview and the lock screen.
2. **`FramePacer` to `AChoreographer`.** Risk: one pacer thread is shared by three hosts; looper ownership, the thermal cap and `setFrameRate` hints move together. Test first: extend `thermal_test.cpp` and the pacer tests to cover start/stop races and two sinks on one clock; device check of frame rates at 60, 90 and 120 Hz panels.
3. **Native ownership of the look** (`SceneParams`, `LookSnapshot`, the codecs). Risk: 39 UI files, the reflection codec and its proguard rule, identity-based change detection, preset and backup compatibility. Test first: a golden test that every bundled and a set of saved user presets round-trip byte-identically through the new path (extend `presets_test.cpp` and `params_schema_test.cpp`).
4. **Export decode to NDK media** (`TrackAudio`, `OffscreenSceneRenderer`). Risk: "export equals live" depends on each frame hearing exactly its audio; codec quirks differ between the Java and NDK wrappers; content URIs need an fd from Kotlin. Test first: `track_feed_test.cpp` and `offline_step_test.cpp` already pin the cut; add a device test comparing decoded PCM hashes, Java path against NDK path, for each container the app accepts.
5. **Saved modulation formats** (`LfoStore` enum names, `reactions/2`, retired ordinals, `milk_path`). Risk: a renamed or re-ordered entry silently resets a user's waves and reactions, and backups restore wrong. Test first: port `ReactionRouteCodecTest` to native and add a fixture of real saved strings (including retired targets) that must parse to the same routes.

## 8. Surprises

- `NativeViz.kt` has no `external fun` at all: 509 lines of handle state, latches and dedupe in Kotlin, on and off the GL thread.
- Kotlin runs every frame on the GL thread (`VisualizerRenderer.onDrawFrame`, `LookApplier`) and owns a display-priority thread (`FramePacer`): both against the owner's rule today.
- `MilkPresetMemory.loaded()` writes SharedPreferences from the GL thread, and `firstRun()` can copy assets there.
- Three files are dead in production (`SoundFrameLayout` but one constant, `ReactSourceCatalog`, `MilkdropEngine`), and a 177-line test exists only to check the dead offsets.
- `ReactSource` (117), `SceneIds` (66), `ParamKeys` (about 147) and the `SceneParams` defaults are hand-kept copies of native lists, although the schema and catalogue texts already carry most of it.
- `SearchMatcher` is a second search beside native `core/library/Search`; its KDoc says so and asks for no new callers.
- Cast's fallback drives `VisualizerRenderer` by calling `onSurfaceCreated(null, null)` and `onDrawFrame(null)` itself.
- `GeodeNative` carries `link`, `webrtc` and `qr` externals ("show on a computer") that AGENTS.md does not describe.
- AGENTS.md is stale in places: it lists `ArtPalette` in the engine's `dev.geode.analysis` (it lives in `app/.../viz/ArtPalette.kt`), names a "presentation clock" in audio-core that does not exist, and the detekt baselines name files and classes that are gone.
- Native links no `mediandk` and uses no `AChoreographer`, `ASensorManager` or `eglCreateContext` yet, so step 4 starts from nothing on the native side.

## 9. Housekeeping note

Besides this file, four read buffers were left in the scratchpad during the scan (`b2.txt`, `b3.txt`, `GN.kt`, `core.txt`). They are dumps of `git show` output, outside the repository.
