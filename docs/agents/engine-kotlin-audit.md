# Engine Kotlin audit: `engine/*/src/main` to C++

Started 2026-11-12 on branch `ccr-0a1f1dcf-87b4sp-engine-to-cpp`, from `3b5742f78` (main `a019454` plus the palette stack). Owner's rule: Kotlin is the UI and the Android shell only; everything under `engine/` is engine and goes to `core/`, class by class, one commit per class (`Engine to C++: <Class>`).

- **Before:** 53 files, 7,719 lines of Kotlin under `engine/*/src/main` (`GeodeNative.kt` 2,730 of them).
- The only Kotlin that may stay: `external fun` declarations, and handing a platform object across where the NDK has no API for it. Each "stays" row below names the missing API.
- Nothing here was built or run locally. CI (`native-host-tests.yml`, `build-lint.yml`) is the test; a device is needed where a row says so.

## Checklist

Status: **ported** (C++ owns it, Kotlin class deleted), **deleted** (dead or a duplicate), **moved to UI** (presentation that decides nothing), **stays** (named NDK gap), **pending** (not reached yet).

| Class (file) | Lines | Status |
|---|---|---|
| `ReactionRouteCodec` | 77 | **ported**: `core/viz/ReactionText`, read through `geode_mod_reactions_read` (`core/api/geode_modulation_api.h`), test `core/tests/reaction_text_test.cpp` (the JVM test's cases, plus the text the Kotlin codec wrote). `LfoStore` hands the saved string across; the saved format is unchanged. |
| `MilkdropEngine` | 12 | **deleted**: `available` was "libgeode loaded", from when MilkDrop was a library of its own. `GeodeNative` loads the same library and each of its three callers calls native right beside the check, so it was always true where it was read; the three checks are gone. |
| `ReactSourceCatalog` | 29 | **deleted**: a parser of `geode_react_source_catalog`'s text that only its own two tests called; no screen read it. The device test now checks the `react` rows of `geode_mod_catalog_text`, which Customize reads alone. |
| `SoundFrameLayout` | 131 | **deleted**: 109 float offsets into `GeodeSoundFrame` and its array sizes that no Kotlin read (the frame no longer crosses JNI). Its test's cases (scalars in order, `lib_sound.glsl` naming each in its slot) are in `core/tests/sound_test.cpp`; the one constant in use, the tuning's 30 floats, is `SoundTuning.FLOATS`, still held to the header by `SoundTuningTest`. |
| `TransitionStyle` | 9 | pending |
| `TouchField` | 13 | pending |
| `FluidQuality` | 13 | pending |
| `ReactSourceCatalog` | 29 | **deleted**: a parser of `geode_react_source_catalog`'s text that only its own two tests called; no screen read it. The device test that holds the `ReactSource` mirror to the native catalogue now compares the lines themselves. |
| `SoundFrameLayout` | 131 | **deleted**: 109 float offsets into `GeodeSoundFrame` and its array sizes that no Kotlin read (the frame no longer crosses JNI). Its test's cases (scalars in order, `lib_sound.glsl` naming each in its slot) are in `core/tests/sound_test.cpp`; the tuning's slots are native's (`core/sound/Tuning`). |
| `TransitionStyle` | 9 | pending: its one engine reader is `LookSnapshot`'s default id; the shell no longer names it. |
| `TouchField` | 13 | **deleted**: the finger slots are native's (`TouchField::kMaxPoints`), published as the catalogue's `touchPoints` value; the gestures and the wallpaper size their buffer by it. Pinned in `catalogue_test.cpp` and the fixture. |
| `FluidQuality` | 13 | **moved to UI**: the chips' names are private to `ui/CustomizeControls`, shown for the catalogue's `fluidTiers`. |
| `SceneFactory` | 14 | pending |
| `FluidShaderTemplate` | 15 | **moved to UI**: an asset read for Expert › Shader code, to `:app` under the same package; no engine module used it. |
| `PlaybackMath` | 18 | **ported**: the pitch rate and the slider step are `core/audio/dsp/PlaybackControls` (`geode_playback_pitch_ratio`, `geode_playback_snap`), test `core/tests/playback_controls_test.cpp`; the sleep timer's m:ss countdown is display text, now private to `ui/PlaybackSettings`. |
| `BestEffort` | 18 | pending: shell glue, but `VisualizerView`, `Reactions`, `OffscreenSceneRenderer` and `TrackAudio` in `:engine:scenes` still call it, so it moves when they do. |
| `UnderlayBlend` | 22 | **moved to UI**: to `:app`, same package; its names are saved data (Background, Studio projects) and its ordinals the underlay's wire int, clamped natively. The unused `fromOrdinal` is gone. |
| `BlendMode` | 24 | **shrunk** (20): the unused `fromOrdinal` is gone; the enum stays while `VisualizerRenderer.submitLayer` takes it. |
| `StyleSchedule` | 31 | pending |
| `Adsr` (`EnvBand`, `AdsrConfig`, `AdsrEngine`) | 33 | **deleted**: native owns it (`core/viz/ModDocument`, `core/viz/ModCatalog`, `geode_mod_*`; test `core/tests/mod_document_test.cpp`) |
| `Adsr` (`EnvBand`, `AdsrConfig`, `AdsrEngine`) | 33 | pending |
| `SceneCapabilities` (`SceneKind`) | 37 | **ported**: `core/viz/Catalogue` (`Trait`); the shell asks `geode_catalogue_*` one question at a time (`render/scene/StyleCatalogue` in `:app`), test `core/tests/catalogue_test.cpp`. |
| `StyleFamilies` | 37 | **ported**: `core/viz/Catalogue` (`Trait::FamilyFollower`); `pro/FreeTier` asks for the followers. |
| `ThermalGovernor` | 38 | pending |
| `SceneCapabilities` (`SceneKind`) | 37 | pending |
| `StyleFamilies` | 37 | pending |
| `ThermalGovernor` | 38 | **ported**: `core/viz/frame/ThermalSource` reads the NDK's `AThermal_*` (status listener, 30 s headroom on the won slot) into `ThermalState`; `geode_thermal_push` is gone. |
| `SoundCursor` | 39 | pending |
| `GyroLook` | 50 | pending |
| `MilkStarterPack` | 50 | pending |
| `BuildStyleReactionsUseCase` | 51 | **deleted**: native owns it (`core/viz/ModDocument`, `core/viz/ModCatalog`, `geode_mod_*`; test `core/tests/mod_document_test.cpp`) |
| `MilkStarterPack` | 50 | pending: shell glue (copies the bundled `.milk` assets once), but `MilkPresetMemory` in `:engine:scenes` installs it, so it moves when that does. |
| `BuildStyleReactionsUseCase` | 51 | pending |
| `PcmTap` | 55 | **moved to the shell**: Media3's `TeeAudioProcessor.AudioBufferSink` has no NDK form, so it stays Kotlin, in `:app` under the same package; `:engine:audio-android` is gone. It decides nothing: `owns` is `CaptureController`'s answer (through `PlayerSession`), and a flush starting a ring epoch is the ring's contract (`core/audio/ingest/PcmRing`); no native owner of the source choice for the player exists yet. |
| `AiffPcm` | 57 | pending |
| `PcmTap` | 55 | pending |
| `AiffPcm` | 57 | **deleted**: a handle over `geode_aiff_*` whose last caller is `export/AudioTranscoder`, which now opens the descriptor and holds the handle itself; the export's track opens AIFF natively (`core/audio/ingest/TrackPump`). |
| `LookSnapshot` | 70 | pending |
| `SceneIds` | 70 | pending |
| `SoundTuning` | 76 | **deleted** (#1134): the shell keeps the saved floats; `geode_sound_tuning_resolve` reads them, pinned by `core/tests/sound_tuning_test.cpp`. |
| `TransitionCatalog` | 79 | **deleted**: native hands Customize's picker its lists (`geode_transition_picker`: the built-ins offered, from `TransitionCatalog::builtInIds` and `transition_choice::pickerIds`, then the library's names); `ui/CustomizeAdvanced` only splits them. The full-table read and its parser are gone, as is `VizUiState.transitionStyle`, which nothing read. Pinned in `transition_library_test.cpp` and `transition_choice_test.cpp`. |
| `SceneIds` | 70 | **deleted**: the id list is native; the shell names an id as text where a rule is about one style. |
| `TransitionCatalog` | 79 | pending |
| `SceneParamsCodec` | 80 | pending |
| `ModConfigCodec` | 81 | **deleted**: native owns it (`core/viz/ModDocument`, `core/viz/ModCatalog`, `geode_mod_*`; test `core/tests/mod_document_test.cpp`) |
| `EventRecipe` | 86 | **deleted**: native owns it (`core/viz/ModDocument`, `core/viz/ModCatalog`, `geode_mod_*`; test `core/tests/mod_document_test.cpp`) |
| `ParamSchema` | 90 | pending |
| `PerformanceSettings` | 94 | pending |
| `ParamScope` | 94 | **ported**: `geode_catalogue_scope_of` and `geode_catalogue_reaches`; the shell holds a scope by native's name. |
| `SampleRing` | 95 | pending |
| `VisualStyleCatalog` | 106 | **moved to UI**: labels are string resources (`render/scene/StyleLabels` in `:app`); its id lists are native. |
| `LookApplier` | 97 | **ported**: the live frame is `geode_viz_frame` (`core/viz/frame/LiveFrame`, test `core/tests/live_frame_test.cpp`); `NativeViz.setLook` pushes a look's changed parts when they change; `PushVersions` and `LastSent` are gone. |
| `VisualStyleCatalog` | 106 | pending |
| `MilkPresetMemory` | 116 | pending |
| `OffscreenSceneRenderer` | 128 | pending |
| `SearchMatcher` | 131 | **deleted**: a second search beside `core/library/Search`. The search sheet (`ui/AppShell`) asks native once per query (`geode_search_sheet`): a fourth field after the album, one hit per URI shown as the device's copy, at most 30 tracks, playlists and presets by name; pinned in `core/tests/library_search_test.cpp`. |
| `Catalogue` | 131 | pending |
| `OffscreenSceneRenderer` | 128 | **ported**: `core/output/ExportFrame` (the prepare order, the pre-roll, each frame's audio and draw), `geode_export_job_*` (`core/api/geode_export_job_api.h`), test `core/tests/export_frame_test.cpp`. `export/ExportScene` opens the renderer and the track's descriptor and applies the look between the job's two prepare calls. The unused `paramsAt` and `styleSchedule` export parameters are gone. |
| `SearchMatcher` | 131 | pending |
| `Catalogue` | 131 | **ported**: the shell asks `geode_catalogue_*` one question at a time (`render/scene/StyleCatalogue` in `:app`), test `core/tests/catalogue_test.cpp`. |
| `ParamFormat` | 153 | **ported**: `core/viz/ParamText` (the unit each control reads in, and its value text), `geode_param_text`, test `core/tests/param_text_test.cpp` (the JVM test's cases). The three units a row names itself are `ParamUnit` in `:app`. The unreachable tempo and count units are gone. |
| `TrackAudio` | 158 | **ported**: `core/audio/ingest/TrackPump` (the decode and hand-out order, AIFF first) over `core/audio/ingest/android/TrackDecoder` (`AMediaExtractor`, `AMediaCodec`, linked `mediandk`), test `core/tests/track_pump_test.cpp`. Needs a device for the codec path. |
| `ParamKeys` | 158 | pending |
| `Lfo` (`LfoTarget`, `LfoConfig`, `ModTable`, …) | 219 | **deleted**: native owns it (`core/viz/ModDocument`, `core/viz/ModCatalog`, `geode_mod_*`; test `core/tests/mod_document_test.cpp`) |
| `VisualizerView` | 220 | pending |
| `Reactions` (`ReactSource`, `ReactionRoute`, `HandMask`) | 229 | **deleted**: native owns it (`core/viz/ModDocument`, `core/viz/ModCatalog`, `geode_mod_*`; test `core/tests/mod_document_test.cpp`). The process-wide push of the hand mask is `LfoStore.pushHand`. |
| `FramePacer` | 248 | pending |
| `SceneParams` | 254 | **in progress** (#1150): its option label lists are gone; every picker's labels are `GEODE_PARAM_CHOICES` rows in `core/viz/ParamTable.inc`, read through `ParamSchema.choices` (test `core/tests/params_schema_test.cpp`). The Folds menu and the kaleidoscope switch rule are native too (`SceneParams::kSymmetryFolds`, `kaleidoscopeFolds`; `ParamSchema.folds`, `GeodeNative.paramsKaleidoscopeFolds`). The fields, defaults (the sentinel constants kept for them, pinned by `ParamSchemaTest`) and the palette slot rules remain. |
| `VisualizerRenderer` | 313 | pending |
| `NativeViz` | 522 | pending |
| `GeodeNative` | 2,736 | declarations; its non-declaration members pending |
