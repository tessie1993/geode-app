# AGENTS.md: MuzViz Studio Pro (internal name Geode)

Android music player, reactive visualizer and video studio. Kotlin and Compose with Hilt, plus a C++20 native core (`libgeode.so`).
applicationId `com.muzviz.studiopro`. Kotlin packages stay `dev.geode.*`. minSdk 26, targetSdk 36, compileSdk 37.

Read before you change structure:
- [`docs/ANDROID_ARCHITECTURE_GUIDE.md`](docs/ANDROID_ARCHITECTURE_GUIDE.md): the target rules. Where the code disagrees, the code is the debt.
- [`docs/ANDROID_ARCHITECTURE_REFERENCE.md`](docs/ANDROID_ARCHITECTURE_REFERENCE.md): the reasons and code shapes behind those rules (layers, modularization, Navigation 3).
- [`docs/adr/`](docs/adr/): decisions. [`0001`](docs/adr/0001-target-module-split.md) is the target module split and the Nav3 plan.
- [`GLOSSARY.md`](GLOSSARY.md): domain words. Name things with them.
- [`docs/AUDIO_CHAIN.md`](docs/AUDIO_CHAIN.md): read it before touching the playback audio path.

## Modules

```
:app ──► :engine:scenes ──api──► :engine:audio-core (JVM, pure Kotlin)
   └───► :engine:audio-android ──api──┘
:app ──externalNativeBuild──► CMakeLists.txt ──► libgeode.so + libprojectM-4.so
```

| Module | Holds | Packages |
|---|---|---|
| `:engine:audio-core` | `GeodeNative` (every JNI `external fun`), the PCM ring (`SampleRing`), feature ring, presentation clock | `dev.geode.engine.audio`, `dev.geode.engine.bridge` |
| `:engine:audio-android` | Media3 PCM tap (`PcmTap`), sink clock driver | `dev.geode.engine.audioandroid` |
| `:engine:scenes` | Live and offline analysis (`AnalysisEngine`, `OfflineAnalyzer`, `FeatureTimeline`, `AnalysisCache`); the GL adapter (`VisualizerView`, `VisualizerRenderer`, `render/bridge/NativeViz`); offscreen rendering for export; scene ids, params, catalogues | `dev.geode.analysis`, `dev.geode.render.*`, `dev.geode.audio`, `dev.geode.util` |
| `:app` | Everything else: UI, playback service, stores, editor model, export, wallpaper, widget | `dev.geode.*` |

Convention plugins live in `build-logic/src/main/kotlin/`: `geode.kotlin-common` (ktlint, detekt, toolchain), `geode.jvm-library`, `geode.android-library` and `geode.provenance`. The version catalog is `gradle/libs.versions.toml`.

The native core is built by the root `CMakeLists.txt`, plus `core/CMakeLists.txt` for the `geode_core` static library. `app/src/main/cpp/*_jni.cpp` only marshal values into the `extern "C"` ABI in `core/api/geode_api.h`. The code itself is in `core/analysis` (FFT, onsets, tempo, key, structure), `core/viz` (renderer, scene registry, `scenes/`, `fluid/`, `compute/`; per frame the renderer runs LFO → ADSR → `Reactions` (the reaction matrix) → `MotionField` → `safety::apply`, then steps `CameraRig`, the one camera every render path shares), `core/audio/player` (AMediaCodec decode, mixer, Oboe output), `core/audio/dsp` and `core/library` (TagLib). The submodules are `third_party/{projectm,kissfft,oboe,taglib}`. projectM gets `tools/projectm-v4.1.7-render-fbo-backport.patch` at configure time. Shaders ship in `app/src/main/assets/shaders/`; styles include `lib_*` files by `//#include`, allow-listed in `core/viz/ShaderSource.cpp`. 3D styles read the rig through `lib_camera`, sound events through `lib_react`.

## `:app` packages (`app/src/main/java/dev/geode/`)

| Package | Responsibility | Open first |
|---|---|---|
| (root) | `Application`, crash file, the `GeodeContainer` service locator | `GeodeApp`, `GeodeContainer`, `RingLog` |
| `di` | Hilt modules. Very thin: most objects are built by hand | `DataModule`, `PlayerSessionProvider` |
| `ui` | Every screen, the five ViewModels, `PlayerSession` with its ~20 `*Controller`s, settings screens, theme glue | `MainActivity`, `AppShell`, `GeodeAppState`, `PlayerSession`, `*ViewModel` |
| `ui/studio` | Studio timeline widgets: lanes, playhead, curve editor, toolbar | `TimelineLanes`, `EditorActions` |
| `ui/theme` | Theme packs and the Stone design system | `ThemePack`, `ThemePackCatalog`, `StoneTheme`, `StoneIcon` |
| `playback` | `PlaybackEngine` (process-wide holder of ExoPlayer or `NativePlayer`, ring and analysis), `PlaybackService` (MediaLibraryService), queue, sleep timer, ReplayGain | `PlaybackEngine`, `PlaybackService`, `NativePlayer` |
| `audio` | Capture sources (mic, other apps' audio), `AudioBus` (features for non-UI surfaces), platform FX, media chain | `AudioBus`, `AudioCapturePump`, `PlaybackCaptureService`, `TapRenderersFactory` |
| `audio/dsp` | The Media3 processor chain order, native DSP | `MvzAudioProcessorChain` |
| `data` | Stores (SharedPreferences, JSON files) and their repositories, template and preset formats | `PresetStore`, `TemplateRepository`, `VideoTemplate`, `GeodePrefsFiles` |
| `editor` | The Studio project model: pure Kotlin, no `android.*` | `Timeline`, `Keyframes`, `Markers`, `AutoCut` |
| `export` | Visualizer export (MediaCodec + offscreen GL), Studio export (Media3 Transformer), loop render, loudness, chapters, captions | `VideoExporter`, `StudioExporter`, `LoopRender`, `ExportRun`, `ExportService` |
| `viz` | Background image layer | `BackgroundImage` |
| `wallpaper` | Live wallpaper and the dream. Each builds its own `VisualizerView` | `VisualizerWallpaperService`, `VisualizerDreamService` |
| `widget` | Glance now-playing widget | `NowPlayingWidget`, `WidgetPublisher` |

## Entry points (`app/src/main/AndroidManifest.xml`)

- `GeodeApp`: `@HiltAndroidApp`. It installs the crash handler, and its lazy `GeodeContainer` sweeps leftover render scratch and pending MediaStore rows.
- `ui.MainActivity`: the only `@AndroidEntryPoint`, and the single activity (launcher, PiP). It handles `geode://preset`, `geode://template` and `MEDIA_PLAY_FROM_SEARCH`, then calls `AppRoot()` in `ui/AppShell.kt`.
- `playback.PlaybackService`: exported Media3 library service, also used by Android Auto. Media3's `MediaButtonReceiver` handles resumption.
- `audio.PlaybackCaptureService`: MediaProjection foreground service.
- `audio.GeodeNotificationListener` (in `NowPlayingBridge.kt`): lets the app name what another app is playing.
- `export.ExportService`: keeps the process in the foreground during a render. It does not own the render; `ExportRun.scope` does.
- `wallpaper.VisualizerWallpaperService` and `wallpaper.VisualizerDreamService`.
- `widget.NowPlayingWidgetReceiver` (in `NowPlayingWidget.kt`).
- `FileProvider` `${applicationId}.presets`, for sharing presets.

## Data flow and threads

1. **Source → `SampleRing`.** ExoPlayer runs `MvzAudioProcessorChain` on its playback thread, and the `PcmTap` there comes first, before any DSP. The native player's Oboe callback is real-time; the `geode-native-tap` thread (`NativeTapPump`) copies its tap into the ring, and player commands go to the `geode-native-player` executor. Mic and capture use one `AudioCapturePump` worker thread each. There is one ring for all sources.
2. **Analysis.** `AnalysisEngine` loops on `Dispatchers.Default`, calling `GeodeNative.analysis*` (JNI into `core/analysis`), and publishes `features: StateFlow<AudioFeatures>`. It runs only while `AudioBus` has consumers. `PlaybackEngine` wires this on `Main.immediate`.
3. **UI hand-off.** `VisualizerEngineBindings` in `ui/EnginePlumbing.kt` collects `features` on Main and writes `VisualizerRenderer.features`.
4. **Render.** The `GLSurfaceView` GL thread runs `VisualizerRenderer.onDrawFrame`, which calls `NativeViz`, then JNI `geode_viz_jni.cpp`, then the `core/viz` renderer and its scene. Reaction routes are the user's (`LfoStore`) or the style's DNA from `BuildStyleReactionsUseCase`, resolved on the GL thread by `StyleReactionsCache`; `OffscreenSceneRenderer` resolves them the same way, so exports react like the screen. `GyroLook` (in `VisualizerView`) feeds the camera's look while it is visible. The wallpaper and dream feed their renderer from `AudioBus.features()` on their own feeder thread.
5. **Export.** `TrackAnalysisController` runs `OfflineAnalyzer` on Default to build a `FeatureTimeline`, cached by `AnalysisCache`. `VideoExporter` and `LoopRender` run on Default inside `ExportRun.scope`. They render through `OffscreenSceneRenderer` and `EncoderSurface` (EGL on the export thread) into MediaCodec and MediaMuxer, and write MediaStore rows with `IS_PENDING` set. `StudioExporter` uses Media3 `Transformer`.
6. **Persistence.** `PlayerSession.storeScope` is `Dispatchers.IO.limitedParallelism(1)`.

## Navigation today

- `GeodeDestination` enum in `ui/GeodeAppState.kt` (PLAYER, LIBRARY, VISUALS, STUDIO, SETTINGS). `GeodeAppState.dest` is saved by name through `rememberSaveable`.
- `AppRoot` / `AppShell` in `ui/AppShell.kt` switch with `when (appState.dest)` and build the bar and rail from `GeodeDestination.entries`. STUDIO is hidden unless `showsStudio`.
- There is no NavHost and no back stack. Sheets and dialogs are local Compose state. All five ViewModels live in the Activity's store. `PlayerSessionProvider`'s KDoc depends on that, and `PlayerViewModel.onCleared` shuts the session down. Read ADR 0001 § Consequences before you add per-destination stores.

## DI

- Hilt: `GeodeApp`, `MainActivity` and five `@HiltViewModel`s, resolved through `geodeViewModel()` in `ui/GeodeViewModelFactory.kt`. `DataModule` provides `GeodePrefsFiles`, `@GeneralPrefs SharedPreferences` and `UserDataRepository`, all taken from `GeodeContainer`.
- `PlayerSessionProvider` (`@Singleton`) builds one `PlayerSession` by hand. That session builds its repositories and controllers itself, and the ViewModels get repositories from it: `session.playbackRepository`, `session.visualizerRepository`, `session.userDataRepository`.
- Services, the wallpaper and the widget don't inject. They use `PlaybackEngine.acquireForService`, `AudioBus`, `context.geodeContainer` or construct stores directly.

## Build, test, CI

- **Don't build locally.** Push to `claude/**`. `.github/workflows/android.yml` runs detekt, `:app:assembleDebug`, `:app`, `:engine:scenes` and `:engine:audio-core` unit tests, `:app:lintDebug`, `ktlintCheck`, plus an emulator job (`connectedDebugAndroidTest`, API 30). Read the results with `gh run list --branch <b>` and `gh run view <id> --log-failed`.
- `ship-apk.yml` (push to `main`) and `release.yml` (tag `v*`) publish. `arch-map.yml` is the gate described below.
- Submodules: `git submodule update --init --recursive`, needed only for native work. Your `local.properties` needs `sdk.dir`.
- Tests: `app/src/test/.../ui/PresetLinkTest.kt`, `app/src/androidTest/.../BuildVariantTest.kt`, `engine/audio-core/src/test/.../FeatureRingTest.kt`, `engine/scenes/src/test/` (style DNA, wire layouts, the saved-reactions codec, randomizer coverage), and `app/src/androidTest/.../ui/ReactivityTabTest.kt` (Compose UI test of the reaction editor, run by the emulator job). Keep parsing that must be tested off-device out of `org.json`, which is a stub in local tests (see `render/ReactionRouteCodec`). CI also runs `core/tests/viz_math_test.cpp` (reaction and camera math, host compiler) and `tools/check_shaders.py` (every style through the NDK's glslc). Put new pure tests in a JVM module where you can. Use `docs/DEVICE_CHECKS.md` for anything that needs a device. `tools/glslcheck` and `tools/hostlink` check shaders and the native link on the host.

## Conventions

- ktlint and detekt are baselined (`config/detekt/detekt.yml`), so new code must pass them clean.
- KDoc explains *why*, at length. Match the density of the file you're in.
- Use `bestEffort(TAG, "what")` (`dev.geode.util`) for best-effort failures. Failures go to `RingLog`.
- Scratch files use the prefixes `geode_aac_`, `geode_loop_` and `studio-`; `GeodeContainer` sweeps them. Renders insert with `IS_PENDING`.
- The tap stays first in the audio chain, and the chain never gets float output (see `docs/AUDIO_CHAIN.md`).
- The app is offline by design: there is no network permission.

## Large files to split (lines)

`ui/theme/ThemePackCatalog.kt` 2010 · `ui/VisualsHub.kt` 1399 · `ui/PlayerSession.kt` 1374 · `ui/CustomizeTabs.kt` 1300 · `ui/BuiltInPresets.kt` 914 · `export/LoopRender.kt` 889 · `ui/StudioScreen.kt` 881 · `ui/AppShell.kt` 752 · `editor/Timeline.kt` 730 · `data/TemplateRepository.kt` 726 · `ui/LibraryScreen.kt` 719 · `ui/PlayerScreen.kt` 709 · `export/LoopExtend.kt` 698 · `ui/VisualizerScreen.kt` 692

## Where the code breaks the architecture guide

- **God object.** Every ViewModel reads `PlayerSession`, which builds ~20 controllers and its repositories by hand. That's a service locator (`GeodeContainer`, `context.geodeContainer`) sitting beside Hilt.
- **Reverse dependencies into `ui`.** These lower layers import from `ui`, although the README says they never do:
  - `data/SmartPlaylistStore` and `playback/LibraryTree` import `ui.DeviceTrack`.
  - `data/TemplateFormat` imports `ui.PresetLink`.
  - `editor/Subtitles` imports `ui.LyricLine`.
  - `wallpaper/*` import `ui.ThemeStore`.
  - `widget/NowPlayingWidget` imports `ui.MainActivity`.
- **Data layer lives in `ui`.** Stores and MediaStore access (`*Controller`, `VizStateStore`, `AutoVisualsPrefsStore`, `DeviceTrackQuery`, `UserDataRepository`) sit there, and some composables call `contentResolver` themselves (`StudioScreen`, `ui/studio/TimelineLanes`, `TrackInfoEditor`, `DuplicatesTab`, `FolderSettings`, `LoopRenderSheet`).
- **One-shot events.** `PlayerViewModel` exposes `vizApply` and `morphFade` as `SharedFlow`.
- **Mixed module.** `:engine:scenes` is an Android library, but most of its files are pure Kotlin. `editor/*` and much of `data/*` are pure too, except for `org.json` (`TemplateFormat`, `EditorProjectJson`, `PerformanceTake`). There is no `kotlinx.serialization` yet.
- **Navigation.** Enum and `when`, not Nav3.
- **Quality items not set up:** one use case so far (`render/scene/BuildStyleReactionsUseCase`), almost no ViewModel or repository tests, no Baseline Profile, `reportFullyDrawn`, JankStats, Macrobenchmark or GWP-ASan.

## Keeping this map current

- **Gate.** `.github/workflows/arch-map.yml` runs `tools/arch-map-gate.sh` on every PR into `claude/geode-finish` or `main`. The check fails when the PR changes structure without touching `AGENTS.md`. Structure means `settings.gradle.kts`, any `build.gradle.kts`, any `CMakeLists.txt`, or a file added, deleted or renamed under `app/src/main/java/` or `engine/`. There is no override.
- **When you refactor,** update the tables above in the same PR: module, package, entry point, thread, large-file line counts. Then update the "breaks the guide" list. When a split step from ADR 0001 lands, rewrite the Modules section to match, and record any change of plan as a new ADR.
- Verify every path you name. Keep this file under 200 lines; push detail into `docs/`.
