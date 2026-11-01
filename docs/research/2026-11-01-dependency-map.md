# Geode dependency map

Base: `origin/claude/geode-finish` @ `cab3efa3` (read in a detached worktree). Graphify index is at `1f83f9c`
(6–7 h old, 40 commits behind: no `core/sound/{Rhythm,Harmony,Form,Emotion}`, `core/viz/Director`, styles grid,
Customize simple/advanced, thumbnails). Graph numbers below are used for corroboration only; every count and
path was re-derived with `git grep` / scripts on the merged base. Items PR #207 (`claude/stale-cleanup`) already
removes are excluded (diff from merge-base `1f83f9c`).

## 1. Module and layer map

### Gradle modules

| Module | Kind | Depends on | Notes |
|---|---|---|---|
| `:engine:audio-core` | JVM (`geode.jvm-library`) | nothing | `GeodeNative` (82 `external fun`), `SampleRing`, `SoundInput`, `SoundFrameLayout`, `SoundTuning`, presentation clock. No `android.*` imports (verified) |
| `:engine:audio-android` | Android lib | `api(:engine:audio-core)`, media3 | `PcmTap`, `SinkClockDriver` |
| `:engine:scenes` | Android lib | `api(:engine:audio-core)`, coroutines | GL adapter, `NativeViz`, catalogue, reactions, params |
| `:app` | application | `:engine:scenes`, `:engine:audio-android` (both `implementation`) + Compose, Hilt, Media3, Billing, Glance, Nav3 | `externalNativeBuild` → root `CMakeLists.txt` |

### Native (`libgeode.so` = 4 JNI files + `geode_core` static lib)

```
Kotlin callers ──► GeodeNative.kt (82 externals) ──JNI──► app/src/main/cpp/*_jni.cpp ──► core/api/geode_api.h (C ABI, 100 fns)
                                                                                        │
          core/api/geode_{viz,params,catalogue,dsp,export_dsp,tags,sound}_api.cpp ◄─────┘
                 │ viz/Renderer, SceneRegistry, Director, Macros, ReactionDna, Params, ParamRandomizer, Catalogue
                 │ audio/dsp/DspChain, Loudness, PcmResampler · library/Tags · sound/SoundEngine
core/viz ──► core/sound (4 includes) · core/util · core/api/geode_api.h (16 headers, for shared POD types)
```

| JNI file | `Java_*` fns | C API family used | Kotlin caller(s) of the matching externals |
|---|---|---|---|
| `app/src/main/cpp/geode_viz_jni.cpp` | 53 | `geode_viz_*` (57), `geode_params_*`, `geode_react_*`, `geode_macro(s)_*`, `geode_style_*` | `render/bridge/NativeViz.kt`, `render/scene/{Catalogue,ParamRandomizer,BuildStyleReactionsUseCase}.kt`, `data/{MacroStore,AutoDirectorStore}.kt`, `render/ReactSourceCatalog.kt` |
| `app/src/main/cpp/geode_jni.cpp` | 17 | `geode_dsp_*` | `audio/dsp/NativeDspProcessor.kt` |
| `app/src/main/cpp/geode_tags_jni.cpp` | 3 | `geode_tags_*` | `data/NativeTags.kt` |
| `app/src/main/cpp/geode_export_dsp_jni.cpp` | 9 | `geode_loudness_*`, `geode_resampler_*` | `export/{LoudnessMeter,PcmResampler}.kt` |

JNI parity: every `external fun` has a `Java_` symbol and vice versa (82 ↔ 82). Every `geode_*` in `geode_api.h`
has a definition. Non-test externals with no Kotlin caller: `dspBand`, `dspBandCount`, `dspSampleRate`,
`dspSetSampleRate`, `vizSetParam`, `tagsLastError` (all deleted by PR #207), plus `reactSourceCatalog`
(androidTest-only by design: `ReactSourceCatalogDeviceTest`). C API fns with no JNI caller: `geode_sound_*` (7),
`geode_params_blend` (all deleted by PR #207).

### Native directory include graph (cross-directory edges)

| From → To | Includes | Comment |
|---|---|---|
| jni → api | 4 | only through `geode_api.h` |
| api → viz / audio/dsp / sound / library | 8 / 3 / 2 / 1 | façade layer |
| viz → api | 10 (+5 fluid, +1 scenes) | headers include `api/geode_api.h` for POD types (`GeodeSoundFrame`, enums). Header-level cycle api.cpp → viz → api.h; harmless but means `geode_api.h` is the type layer, not only the ABI |
| viz → sound | 4 | sound frame |
| viz ↔ viz/scenes | 17 / 53 | `Renderer.hpp`, `SceneRegistry`, `Catalogue.cpp`, `ParamRandomizer.cpp`, `Overlays.hpp` include scenes |
| viz/fluid ↔ viz/scenes | 5 / 19 | `fluid/{FluidLook,FluidParticles,FluidSim,RippleSim}.hpp` include `viz/scenes/ProgramLoader.hpp`; `fluid/FluidEmitters.cpp` includes `scenes/SceneCommon.hpp` |

No orphan native files: every `core/**/*.cpp` is listed in `core/CMakeLists.txt`; every header is included somewhere.

### `:app` package layering (from imports, merged base)

Intended: `ui/di → data, playback, audio, export, editor, model, pro → engine`. Observed strongly-connected
package groups (cycles) are in §5.

## 2. Hubs

Fan-in = other main-source files that name a type declared in the file; fan-out = distinct main files it names.

| # | File | Fan-in | Fan-out | Lines | Why it matters |
|---|---|---|---|---|---|
| 1 | `app/src/main/java/dev/geode/ui/PlayerSession.kt` | 13 (+9 via `PlayerSessionProvider`) | **72** | 1291 | God object: builds 34 controllers/stores/repositories by hand (graph: 173 symbols, 64 neighbour files). Every ViewModel reaches it |
| 2 | `engine/scenes/src/main/kotlin/dev/geode/render/scene/SceneParams.kt` | **33** | 1 | 276 | Param value type for live, presets, templates, export; ADR 0001 wants it in `:engine:scene-model` |
| 3 | `app/src/main/java/dev/geode/data/PresetStore.kt` | 28 | 3 | 561 | Preset format + store; `org.json` blocks `:core:model` |
| 4 | `app/src/main/java/dev/geode/editor/Timeline.kt` | 23 | 4 | 730 | Studio model root; drags `export.ClipEdit` (Media3) into `editor` (§4) |
| 5 | `app/src/main/java/dev/geode/ui/PlayerModels.kt` | 22 | 4 | 75 | UI state types every controller emits; why controllers cannot leave `ui` |
| 6 | `app/src/main/java/dev/geode/ui/PlayerViewModel.kt` | 20 | 24 | 353 | Owns session teardown (`onCleared`); blocks Nav3 per-entry stores |
| 7 | `app/src/main/java/dev/geode/RingLog.kt` | 20 | 0 | 28 | Root-package logger; makes `data`, `export`, `audio`, `billing` depend on `dev.geode` root (§5) |
| 8 | `app/src/main/java/dev/geode/export/VideoExporter.kt` | 19 | 16 | 664 | Export pipeline hub (offscreen GL, MediaCodec, viz, audio) |
| 9 | `engine/audio-core/src/main/kotlin/dev/geode/engine/bridge/GeodeNative.kt` | 11 | 0 | 451 | Sole JNI surface (82 externals); graph: `NativeViz` alone has weight 42 to it |
| 10 | `engine/scenes/src/main/kotlin/dev/geode/render/bridge/NativeViz.kt` | 10 | — | — | Every live/offscreen renderer call into `libgeode` |
| 11 | `core/api/geode_api.h` | 28 includes | — | — | C ABI + shared POD types for all native code |
| 12 | `core/viz/Program.hpp` / `Params.hpp` / `Scene.hpp` | 19 / 18 / 12 includes | — | — | GL program, param block, scene base for every style |
| 13 | `app/src/main/java/dev/geode/data/ThemeStore.kt` | 17 | 4 | 296 | `GuiPrefs`; read by wallpaper, dream, UI |

### God-object paths

- **PlayerSession:** `{Player,Visuals,Library,Studio,Settings}ViewModel` → `di/PlayerSessionProvider.kt` → `ui/PlayerSession.kt` →
  `QueueController, CaptureController, ExportController, TakeController, EditorController, ModulationController, MusicLibraryController,
  AutoVisualsController, BackgroundController, MilkImportController, PresetLibraryController, TemplateController, TextureController,
  TrackColorController, VisualSettingsController, PlayerSettingsController, ListeningTracker` + `PresetStore, SessionStore, TakeStore,
  LfoStore, VizStateStore, EditorProjectStore, BookmarkStore, PlayerPrefsStore, BackgroundPrefsStore, AutoVisualsPrefsStore` +
  `File{Preset,Session,Take}Repository, SharedPrefs{Favourites,PlayerPrefs}Repository, Session{Playback,Visualizer}Repository`.
  Also reaches `playback/PlaybackEngine.releaseUi`, `PlaybackService`, `export/ExportRun.scope`.
- **GeodeContainer** (`app/src/main/java/dev/geode/GeodeContainer.kt`, 200 lines): named by class in 2 files, but the
  `context.geodeContainer` locator is used in 6 main files: `di/DataModule.kt`, `ui/PlayerSession.kt`,
  `ui/EnginePlumbing.kt:46` and `ui/PerformanceSettings.kt:25` (composables reading `LocalContext.current.geodeContainer.performance`),
  `widget/WidgetPublisher.kt:76` (`appScope`). Holds `PerformanceStore`, `UserDataRepository`, `appScope`, and the scratch/pending sweeps.

## 3. Dead code (merged base, not covered by PR #207)

Method: token counts over all `*.kt/*.kts/*.xml/*.pro` (comments stripped), C++ token counts outside the decl/def pair,
then each hit re-checked with `git grep` and graphify `callers` (all returned 0 callers).

### Delete

| Symbol | Location | Evidence |
|---|---|---|
| `object VisualSafety` (+ `MAX_FLASH_DEPTH`) — whole file | `engine/scenes/src/main/kotlin/dev/geode/render/VisualSafety.kt:12` | 0 references in any `.kt`; KDoc says "the UI's slider ranges quote" it, none do. PR #207 only edits its KDoc |
| `HistoryStore.mostPlayed` | `app/src/main/java/dev/geode/data/HistoryStore.kt:115` | 0 callers |
| `FakeBillingRepository.setProduct` | `app/src/main/java/dev/geode/billing/FakeBillingRepository.kt:112` | 0 callers (debug panel and tests don't use it) |
| `GeodeMotion.REDUCED` | `app/src/main/java/dev/geode/ui/theme/GeodeTokens.kt:100` | only its own KDoc (`:93`) names it; PR #207 drops RELEASE/STATE/ENTER/EXIT but keeps this |
| `SinkClockDriver.diagnostics` + `SinkClockDiagnostics` | `engine/audio-android/src/main/kotlin/dev/geode/engine/audioandroid/SinkClockDriver.kt:35`, `SinkClockContracts.kt:31` | getter never read; class only built by that getter (counters it reports are write-only) |
| C++ `naturalPolarity(ModSource)` | `core/viz/Lfo.hpp:35`, `core/viz/Lfo.cpp:33` | 0 callers native or test. The live copy is Kotlin `LfoConfig.naturalPolarity` (`engine/scenes/src/main/kotlin/dev/geode/render/Lfo.kt:155`, used by `ui/CustomizeAdvanced.kt:514`): either delete the C++ or (ADR 0003) expose it and drop the Kotlin one |
| `ComputePass::storageBuffer` | `core/viz/Compute.hpp:126`, `core/viz/Compute.cpp:233` | 0 call sites |
| `SimUniforms::f4`, `SimUniforms::sampler` | `core/viz/compute/SimPass.hpp:31,33`, `SimPass.cpp:202` | 0 call sites |
| `Director::emotion()` (new since graph) | `core/viz/Director.hpp:97` | 0 call sites incl. tests |

### Low priority (intentional tables, decide per policy)

- `engine/scenes/src/main/kotlin/dev/geode/render/scene/SceneIds.kt`: 33 id constants unused (`JULIA`, `TUNNEL`, … `FRACTAL_TUNNEL`); 3 more test-only (`PLASMA`, `VANISHING`, `KIFS`). The native catalogue is the source of truth now.
- `engine/scenes/src/main/kotlin/dev/geode/render/scene/ParamKeys.kt`: 27 unused key constants (`BEAT_PULSE:18` … `FLOW_FADE:171`).
- `engine/audio-core/src/main/kotlin/dev/geode/engine/audio/SoundFrameLayout.kt`: ~12 unused slot constants (`KICK2:63` … `FLOATS:132`) — keep: they document the wire layout checked by `SoundFrameLayoutTest`.
- Test-only main code: `SoundFrameLayout.SCALAR_NAMES:138`, `ParamFormat.formatParam:136`, `ReactSourceCatalog` + `GeodeNative.reactSourceCatalog` (parity scaffolding), C++ `Director::commits/lastReason`, `Harmony::scaleCount/scaleInfo`, `Catalogue::families/opener`, `ScenePrep` counters.
- Previews `PerformanceSettingsPreview/CustomPreview` (`ui/PerformanceSettings.kt:125,133`) and Hilt `@Provides` (`di/*Module.kt`) are reflection/tooling entry points, not dead.

## 4. Layer-rule checks (merged base)

| Check | Result |
|---|---|
| `tools/layer-gate.sh` (nothing outside `ui/`, `di/` names `dev.geode.ui`) | **passes** |
| `android.*`/`androidx.*` imports in `pro`, `model`, `editor` | none directly; `org.json` none |
| `engine/audio-core` `android.*` imports | none |
| **`editor` transitively Android-bound** | `editor/Timeline.kt:3` and `editor/AnimatableParams.kt:4` import `dev.geode.export.ClipEdit`, which imports `androidx.media3.*` and `androidx.annotation.StringRes` (`export/ClipEdit.kt:3-12`). `editor/AnimatableParams.kt:3` imports `dev.geode.R` (`R.string.studio_*` at `:134-141`). `editor/Keyframes.kt:3` imports `data.PerformanceTake` (uses `org.json`, `data/PerformanceTake.kt:4-5`). `editor` cannot move to JVM `:core:model` as is |
| `data/backup` → `di` | `data/backup/BackupRepository.kt:11` imports `dev.geode.di.IoDispatcher` (qualifier defined in `di/DataModule.kt:25`). Data layer depends on the DI package, and `di` depends on `ui` (`PlayerSessionProvider`) |
| `ui/theme` → `ui` (blocks `:core:designsystem`) | `ui/theme/GeodeTheme.kt:12` imports `dev.geode.ui.ColorDerive` |
| `ui/pro` → `ui` (paywall "self-contained") | `ui/pro/PaywallScreen.kt:93`, `ui/pro/ProComponents.kt:39` import `dev.geode.ui.geodeViewModel` |

### Composables (or UI files) calling `contentResolver`

| File:line | In | Call |
|---|---|---|
| `app/src/main/java/dev/geode/ui/DuplicatesTab.kt:85` | `DuplicatesTab` | `MediaStore.createDeleteRequest` |
| `app/src/main/java/dev/geode/ui/FolderSettings.kt:58` | `PresetFolderGroup` | `takePersistableUriPermission` |
| `app/src/main/java/dev/geode/ui/LoopRenderSheet.kt:75` | `LoopRenderSheet` | `takePersistableUriPermission` |
| `app/src/main/java/dev/geode/ui/StudioScreen.kt:540` | `ClipLookSection` | `takePersistableUriPermission` |
| `app/src/main/java/dev/geode/ui/StudioScreen.kt:863` | `Context.shareVideo` | `ClipData.newUri` |
| `app/src/main/java/dev/geode/ui/TrackInfoEditor.kt:134` | `save()` in composable | `MediaStore.createWriteRequest` |
| `app/src/main/java/dev/geode/ui/studio/TimelineLanes.kt:145,184,196,236` | `TimelineEditor` lambdas | persist permission, `openInputStream`, `openOutputStream` (SRT/chapters I/O) |
| `app/src/main/java/dev/geode/ui/theme/UiSounds.kt:78` | `rememberUiSounds` | `Settings.System.getInt` (minor) |
| `app/src/main/java/dev/geode/ui/Lyrics.kt:57` | `object LyricsLoader` (not a composable) | `openInputStream`; a data loader living in `ui` |

Controllers in `ui/` that also use `contentResolver` (data layer in `ui`, known debt): `BackgroundController`,
`MilkImportController`, `MusicLibraryController`, `PresetLibraryController`, `QueueController`, `TemplateController`.

## 5. Package cycles (Tarjan SCC over Kotlin imports, main sources)

| SCC | Edges that close the cycle (file) |
|---|---|
| `dev.geode` (root) ⇄ `audio, billing, data, editor, export, render.offscreen, viz` | root → `data` (`GeodeApp`, `GeodeContainer`), → `billing` (`GeodeApp`); back-edges via `RingLog` (8 imports), `R` (14), `BuildConfig`, `appLaunchIntent`. Inside: `data → editor` (`EditorProjectJson`, `EditorProjectStore`), `editor → data` (`Keyframes` → `PerformanceTake`); `data → export` (`EditorProjectJson`, `ExportPrefs`, `TemplateRepository`, `VideoTemplate`), `export → data` (`ProjectComposition`); `editor ⇄ export` (`Timeline`/`AnimatableParams` → `ClipEdit`; `ChapterMarkers`, `ProjectComposition`, `SpeedRamp`, `TimedTextOverlay` → editor); `export → viz → render.offscreen → audio`, `export → audio` |
| `playback ⇄ widget` | `playback/PlaybackService.kt:27` → `widget.WidgetPublisher`; `widget/NowPlayingWidget.kt:50-51` → `playback.{MediaArtwork,PlaybackService}` |
| `ui ⇄ di ⇄ data.backup`, `ui ⇄ ui.pro/ui.studio/ui.theme` | `di/PlayerSessionProvider` → `ui.PlayerSession`; 5 ViewModels → `di`; `data/backup/BackupRepository` → `di`; `ui.theme/GeodeTheme` → `ui.ColorDerive`; `ui.pro` → `ui.geodeViewModel`; `ui.studio/{CurveEditor,EditorToolbar,TimelineLanes,TransitionSheet}` → `ui` |
| `:engine:scenes`: `render ⇄ render.bridge ⇄ render.scene` | `render.bridge/{NativeViz,ModConfigCodec}` → `render`; `render.scene/{BuildStyleReactionsUseCase,SceneParams}` → `render`; `render.scene/ParamRandomizer` → `render.bridge`. Matters if `:engine:scene-model` is split out (ADR 0001) |

Native cycles: `viz ⇄ viz/scenes`, `viz/fluid ⇄ viz/scenes` (via `ProgramLoader.hpp`, `SceneCommon.hpp`), header-level `api ⇄ viz` (§1).
Graphify does not report package cycles directly; these come from the import scan, spot-checked against `graphify_file_neighbors`.

## Method notes

- Graphify calls used: `graph_stats`, `graphify_rank_files`, `graphify_file_neighbors` (`PlayerSession.kt`, `GeodeContainer.kt`,
  `GeodeNative.kt`, `geode_api.h`), `graphify_callers` (`naturalPolarity`, `mostPlayed`, `storageBuffer`, `VisualSafety`, `setProduct`).
  The graph has noise (e.g. `GeodeContainer` → `ThemeStoreTest` call edges at conf 0.5) and lacks today's files.
- PR #207 exclusions: 164 symbols / 32 files removed in `1f83f9c..origin/claude/stale-cleanup` (e.g. `PcmSink.kt`,
  `PresentationMapping.kt`, `core/api/geode_sound_api.cpp`, `geode_api.cpp`, Gain/Fluid/Ripple helpers, `ExportPhase.withProgress`, `VideoTemplate.jobsFor`).
