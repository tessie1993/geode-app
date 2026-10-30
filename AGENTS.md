# AGENTS.md: MuzViz Studio Pro (internal name Geode)

Android music player, reactive visualizer and video studio. Kotlin and Compose with Hilt, plus a C++20 native core (`libgeode.so`).
applicationId `com.muzviz.studiopro`. Kotlin packages stay `dev.geode.*`. minSdk 26, targetSdk 36, compileSdk 37.

Read before you change structure:
- [`docs/ANDROID_ARCHITECTURE_GUIDE.md`](docs/ANDROID_ARCHITECTURE_GUIDE.md): the target rules. Where the code disagrees, the code is the debt.
- [`docs/ANDROID_ARCHITECTURE_REFERENCE.md`](docs/ANDROID_ARCHITECTURE_REFERENCE.md): the reasons and code shapes behind those rules (layers, modularization, Navigation 3).
- [`docs/adr/`](docs/adr/): decisions. [`0001`](docs/adr/0001-target-module-split.md) is the target module split and the Nav3 plan; [`0002`](docs/adr/0002-release-before-restructure.md) puts the Play release before steps 2–6. [`docs/ARCHITECTURE_FINISH_PLAN.md`](docs/ARCHITECTURE_FINISH_PLAN.md) is the prioritized plan.
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
| `:engine:audio-core` | `GeodeNative` (every JNI `external fun`), the PCM ring (`SampleRing`), presentation clock, `AnalysisFft` (FFT size and hop for a stream's sample rate) | `dev.geode.engine.audio`, `dev.geode.engine.bridge` |
| `:engine:audio-android` | Media3 PCM tap (`PcmTap`), sink clock driver | `dev.geode.engine.audioandroid` |
| `:engine:scenes` | Live and offline analysis (`AnalysisEngine`, `OfflineAnalyzer`, `FeatureTimeline`, `AnalysisCache`); the GL adapter (`VisualizerView`, `VisualizerRenderer`, `render/bridge/NativeViz`); offscreen rendering for export; scene ids, params, catalogues, the style families (`StyleFamilies`) | `dev.geode.analysis`, `dev.geode.render.*`, `dev.geode.audio`, `dev.geode.util` |
| `:app` | Everything else: UI, playback service, stores, editor model, export, wallpaper, widget | `dev.geode.*` |

Convention plugins live in `build-logic/src/main/kotlin/`: `geode.kotlin-common` (ktlint, detekt, toolchain), `geode.jvm-library`, `geode.android-library` and `geode.provenance`. The version catalog is `gradle/libs.versions.toml`.

The native core is built by the root `CMakeLists.txt`, plus `core/CMakeLists.txt` for the `geode_core` static library. `app/src/main/cpp/*_jni.cpp` only marshal values into the `extern "C"` ABI in `core/api/geode_api.h`. The code itself is in `core/analysis` (FFT, onsets, tempo, key, structure), `core/viz` (renderer, scene registry, `scenes/`, `fluid/`, `compute/`; per frame the renderer runs LFO → ADSR → `Reactions` (the reaction matrix) → `MotionField` → `safety::apply`, then steps `CameraRig`, the one camera every render path shares), `core/audio/player` (AMediaCodec decode, mixer, Oboe output), `core/audio/dsp` and `core/library` (TagLib). The submodules are `third_party/{projectm,kissfft,oboe,taglib}`. projectM gets `tools/projectm-v4.1.7-render-fbo-backport.patch` at configure time. Shaders ship in `app/src/main/assets/shaders/`; styles include `lib_*` files by `//#include`, allow-listed in `core/viz/ShaderSource.cpp`. 3D styles read the rig through `lib_camera`, sound events through `lib_react`. A style family's twelve styles (`arbor_*`, `bulb_*`, `chrys_*`, `lucid_*`, `dive_*`) share `lib_<family>` and are listed in `StyleFamilies`.

## `:app` packages (`app/src/main/java/dev/geode/`)

| Package | Responsibility | Open first |
|---|---|---|
| (root) | `Application`, crash file, the `GeodeContainer` service locator, the app's launch intent (`appLaunchIntent`, for the widget and notifications) | `GeodeApp`, `GeodeContainer`, `RingLog`, `AppLaunch` |
| `di` | Hilt modules. Very thin: most objects are built by hand | `DataModule`, `BillingModule`, `PlayerSessionProvider` |
| `pro` | Pure Kotlin (no `android.*`): Pro entitlement rules, the free tier, the billing and trial interfaces, the one gate use case, the paywall request | `FreeTier` (edit to tune the free tier), `FeatureGate`, `GetFeatureGateUseCase`, `BillingRepository`, `PaywallNavigator` |
| `billing` | Play Billing (PBL 9) behind `BillingRepository`, the fake for debug and tests, entitlement cache, trial store, purchase signature check | `PlayBillingRepository`, `FakeBillingRepository`, `ProSnapshot` |
| `ui` | Every screen, the six ViewModels, `PlayerSession` with its ~20 `*Controller`s, settings screens, theme glue. Studio (`StudioHub`: Edit, Render) owns export, loop render and export defaults | `MainActivity`, `AppShell`, `GeodeAppState`, `PlayerSession`, `StudioHub`, `ExportHost`, `*ViewModel` |
| `ui/studio` | Studio timeline widgets: lanes, playhead, curve editor, toolbar | `TimelineLanes`, `EditorActions` |
| `ui/pro` | Self-contained paywall (own palette, no theme APIs), its ViewModel, Pro badge, locked-destination gate, Settings "Go Pro" row | `PaywallViewModel`, `PaywallScreen`, `ProComponents` |
| `ui/theme` | The one "black glass" theme: design tokens (colour, glass alphas, spacing, radius, motion), the M3 colour scheme and type scale, icons, press feedback, haptics, interface sounds. No screen code; ready to move into `:core:designsystem` | `GeodeTokens`, `GeodeTheme` (`geodeColorScheme`, `geodeTypography`), `StoneIcon`, `StonePress`, `UiSounds` |
| `playback` | `PlaybackEngine` (process-wide holder of ExoPlayer or `NativePlayer`, ring and analysis), `PlaybackService` (MediaLibraryService), queue, sleep timer, ReplayGain | `PlaybackEngine`, `PlaybackService`, `NativePlayer` |
| `audio` | Capture sources (mic, other apps' audio), `AudioBus` (features for non-UI surfaces), platform FX, media chain | `AudioBus`, `AudioCapturePump`, `PlaybackCaptureService`, `TapRenderersFactory` |
| `audio/dsp` | The Media3 processor chain order, native DSP | `MvzAudioProcessorChain` |
| `data` | Stores (SharedPreferences, JSON files) and their repositories, template and preset formats, the GUI prefs (`ThemeStore`, `GuiPrefs`, `UserIntent`), the track list (`TrackLibrary`) and the MediaStore query (`DeviceTrackQuery`) | `PresetStore`, `TemplateRepository`, `VideoTemplate`, `GeodePrefsFiles`, `UserDataRepository` |
| `model` | Pure value types shared across layers, staged for `:core:model` (ADR 0001 step 2). No `android.*` | `DeviceTrack`, `LibraryTrack`, `LyricLine`, `PresetLink` |
| `data/backup` | Manual backup zip (Settings › Backup). Format, allowlist, checks and migrations are pure Kotlin; `BackupRepository` does the picker, filesDir and prefs I/O | `BackupFormat`, `BackupArchive`, `BackupRepository` |
| `editor` | The Studio project model: pure Kotlin, no `android.*` | `Timeline`, `Keyframes`, `Markers`, `AutoCut` |
| `export` | Visualizer export (MediaCodec + offscreen GL), Studio export (Media3 Transformer), loop render, loudness, chapters, captions, `PcmResampler` (hi-res sources down to an AAC-supported rate), the non-Pro watermark | `VideoExporter`, `StudioExporter`, `LoopRender`, `ExportRun`, `ExportService`, `WatermarkOverlay` |
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

- `GeodeDestination` enum in `ui/GeodeAppState.kt` (PLAYER, LIBRARY, VISUALS, STUDIO, SETTINGS). `GeodeAppState.dest` and `studioSection` are saved by name through `rememberSaveable`.
- Studio (`StudioHub` in `ui/StudioRender.kt`) owns every render: Edit (clip editor, `StudioScreen`) and Render (visualizer export `ExportDialog` and `LoopRenderSheet` through `ExportHost`, plus the export defaults). Settings › Export and the Visuals export icon only open it (`GeodeAppState.openStudioRender`).
- `AppRoot` / `AppShell` in `ui/AppShell.kt` switch with `when (appState.dest)` and build the bar and rail from `GeodeDestination.entries`. STUDIO is hidden unless `showsStudio`.
- There is no NavHost and no back stack. Sheets and dialogs are local Compose state. All six ViewModels live in the Activity's store. `PlayerSessionProvider`'s KDoc depends on that, and `PlayerViewModel.onCleared` shuts the session down. Read ADR 0001 § Consequences before you add per-destination stores.

## Pro (one-time unlock)

- Product id `muzviz_pro` (`BillingRepository.PRO_PRODUCT_ID`). Every gate asks a `FeatureGate` from `GetFeatureGateUseCase` (Pro beats the 1-hour trial beats free); what free keeps is `pro/FreeTier.kt` alone. Free gets the loose shader styles and the first style of each style family; the other eleven per family are Pro (`StyleFamilies.proIds`).
- Gates: `PlayerViewModel.startExport`/`startLoopRender` (4K, 60 fps, the 1-minute cap, the full-frame watermark; loop renders need Pro), `StudioViewModel` exports (Pro only), `PlayerSession.selectScene`/`applyPreset` via `PlayerSessionProvider` (styles; auto-visuals skips locked ones silently), `ProGate` on Studio › Edit (Studio › Render is open to free and trial users within `FreeTier`), the wallpaper button, and `ProSnapshot` in the wallpaper and dream services.
- `PaywallNavigator.request` is state; `PaywallHost` (a `Dialog`, drawn from `AppRoot`) shows it. Debug builds bill against `FakeBillingRepository` (`BuildConfig.USE_FAKE_BILLING`) with a debug panel on the paywall.
- Store and console steps: [`docs/play/`](docs/play/).

## DI

- Hilt: `GeodeApp`, `MainActivity` and six `@HiltViewModel`s, resolved through `geodeViewModel()` in `ui/GeodeViewModelFactory.kt`. `DataModule` provides `GeodePrefsFiles`, `@GeneralPrefs SharedPreferences` and `UserDataRepository`, all taken from `GeodeContainer`, plus `@IoDispatcher`. `BackupRepository` is constructor-injected.
- `PlayerSessionProvider` (`@Singleton`) builds one `PlayerSession` by hand. That session builds its repositories and controllers itself, and the ViewModels get repositories from it: `session.playbackRepository`, `session.visualizerRepository`, `session.userDataRepository`.
- Services, the wallpaper and the widget don't inject. They use `PlaybackEngine.acquireForService`, `AudioBus`, `context.geodeContainer` or construct stores directly.

## Build, test, CI

- **Don't build locally.** Push to `claude/**`. `.github/workflows/android.yml` runs detekt, `:app:assembleDebug`, every module's unit tests, `:app:lintDebug`, `ktlintCheck`, plus an emulator job (`connectedDebugAndroidTest`, API 30). Read the results with `gh run list --branch <b>` and `gh run view <id> --log-failed`.
- `ship-apk.yml` (push to `main`) publishes a debug APK. `release.yml` builds the Play AAB unsigned on PRs (no secrets reach PR code) and signed on tags `v*` and manual runs, then checks the merged manifest (no INTERNET, targetSdk), R8 and the baseline profile. `arch-map.yml` holds the two gates described below.
- Debug builds sign with the committed `app/debug.keystore` so CI APKs install over each other.
- Submodules: `git submodule update --init --recursive`, needed only for native work. Your `local.properties` needs `sdk.dir`.
- Tests: JVM unit tests live in `app/src/test` and `engine/*/src/test` (CI runs every module's), instrumented ones in `app/src/androidTest`. Among them: `engine/scenes/src/test/` (style DNA, wire layouts, the saved-reactions codec, randomizer coverage) and `app/src/androidTest/.../ui/ReactivityTabTest.kt` (Compose UI test of the reaction editor, run by the emulator job). CI also runs `core/tests/viz_math_test.cpp` (reaction and camera math, host compiler) and `tools/check_shaders.py` (every style through the NDK's glslc). Put new pure tests in a JVM module where you can; keep parsing that must be tested off-device out of `org.json`, which is a stub in local tests (see `render/ReactionRouteCodec`). Use `docs/DEVICE_CHECKS.md` for anything that needs a device. `tools/glslcheck` and `tools/hostlink` check shaders and the native link on the host.

## Conventions

- ktlint and detekt are baselined (`config/detekt/detekt.yml`), so new code must pass them clean.
- KDoc explains *why*, at length. Match the density of the file you're in.
- Use `bestEffort(TAG, "what")` (`dev.geode.util`) for best-effort failures. Failures go to `RingLog`.
- Scratch files use the prefixes `geode_aac_`, `geode_loop_` and `studio-`; `GeodeContainer` sweeps them. Renders insert with `IS_PENDING`.
- The tap stays first in the audio chain, and the chain never gets float output (see `docs/AUDIO_CHAIN.md`).
- The app is offline by design: there is no network permission. The manifest removes INTERNET explicitly; Play Billing works over IPC to the Play Store app.
- Runtime permissions are asked for in context through `ui/PermissionRequest.kt` (`rememberPermissionRequest`: rationale first, retry after a refusal, app Settings once Android stops asking). Only music access is asked at first run (`SafetyConsent` → `FirstRun`, on `OnboardingScaffold`).

## Large files to split (lines)

`ui/VisualsHub.kt` 1468 · `ui/PlayerSession.kt` 1371 · `ui/CustomizeTabs.kt` 1302 · `ui/BuiltInPresets.kt` 914 · `export/LoopRender.kt` 893 · `ui/StudioScreen.kt` 876 · `ui/AppShell.kt` 762 · `editor/Timeline.kt` 730 · `data/TemplateRepository.kt` 726 · `ui/LibraryScreen.kt` 719 · `ui/PlayerScreen.kt` 709 · `export/LoopExtend.kt` 701 · `ui/VisualizerScreen.kt` 691

## Where the code breaks the architecture guide

- **God object.** Every ViewModel reads `PlayerSession`, which builds ~20 controllers and its repositories by hand. That's a service locator (`GeodeContainer`, `context.geodeContainer`) sitting beside Hilt.
- **Layer rule (enforced).** Nothing outside `ui/` and `di/` may name `dev.geode.ui`; `tools/layer-gate.sh` fails the PR otherwise. Move a type down instead: `model` if it is pure, `data` if it touches storage.
- **Data layer lives in `ui`.** Stores and MediaStore access (`*Controller`, `VizStateStore`, `AutoVisualsPrefsStore`) sit there, and some composables call `contentResolver` themselves (`StudioScreen`, `ui/studio/TimelineLanes`, `TrackInfoEditor`, `DuplicatesTab`, `FolderSettings`, `LoopRenderSheet`).
- **One-shot events.** `PlayerViewModel` exposes `vizApply` and `morphFade` as `SharedFlow`.
- **Mixed module.** `:engine:scenes` is an Android library, but most of its files are pure Kotlin. `editor/*` and much of `data/*` are pure too, except for `org.json` (`TemplateFormat`, `EditorProjectJson`, `PerformanceTake`). There is no `kotlinx.serialization` yet.
- **Navigation.** Enum and `when`, not Nav3.
- **Quality items not set up:** two use cases so far (`pro/GetFeatureGateUseCase`, `render/scene/BuildStyleReactionsUseCase`), almost no ViewModel or repository tests, a hand-written (not generated) baseline profile (`app/src/main/baseline-prof.txt`), no `reportFullyDrawn`, JankStats, Macrobenchmark or GWP-ASan.

## Keeping this map current

- **Gate.** `.github/workflows/arch-map.yml` runs `tools/arch-map-gate.sh` on every PR into `claude/geode-finish` or `main`. The check fails when the PR changes structure without touching `AGENTS.md`. The same workflow runs `tools/layer-gate.sh` (the layer rule above). Structure means `settings.gradle.kts`, any `build.gradle.kts`, any `CMakeLists.txt`, or a file added, deleted or renamed under `app/src/main/java/` or `engine/`. There is no override.
- **When you refactor,** update the tables above in the same PR: module, package, entry point, thread, large-file line counts. Then update the "breaks the guide" list. When a split step from ADR 0001 lands, rewrite the Modules section to match, and record any change of plan as a new ADR.
- Verify every path you name. Keep this file under 200 lines; push detail into `docs/`.
