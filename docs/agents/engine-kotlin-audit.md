# Engine Kotlin audit: `engine/*/src/main` to C++

Started 2026-11-12 on branch `ccr-0a1f1dcf-87b4sp-engine-to-cpp`, from `3b5742f78` (main `a019454` plus the palette stack). Owner's rule: Kotlin is the UI and the Android shell only; everything under `engine/` is engine and goes to `core/`, class by class, one commit per class (`Engine to C++: <Class>`).

- **Before:** 53 files, 7,719 lines of Kotlin under `engine/*/src/main` (`GeodeNative.kt` 2,730 of them).
- The only Kotlin that may stay: `external fun` declarations, and handing a platform object across where the NDK has no API for it. Each "stays" row below names the missing API.
- Nothing here was built or run locally. CI (`native-host-tests.yml`, `build-lint.yml`) is the test; a device is needed where a row says so.

## Checklist

Status: **ported** (C++ owns it, Kotlin class deleted), **deleted** (dead or a duplicate), **moved to UI** (presentation that decides nothing), **stays** (named NDK gap), **pending** (not reached yet).

| Class (file) | Lines | Status |
|---|---|---|
| `ReactionRouteCodec` | 77 | **ported**: `core/viz/ReactionText`, `geode_reactions_text_encode/decode` (`core/api/geode_reaction_text_api.h`), test `core/tests/reaction_text_test.cpp` (the JVM test's cases, plus the text the Kotlin codec wrote). `LfoStore` hands the saved string across; the saved format is unchanged. |
| `MilkdropEngine` | 12 | **deleted**: `available` was "libgeode loaded", from when MilkDrop was a library of its own. `GeodeNative` loads the same library and each of its three callers calls native right beside the check, so it was always true where it was read; the three checks are gone. |
| `ReactSourceCatalog` | 29 | **deleted**: a parser of `geode_react_source_catalog`'s text that only its own two tests called; no screen read it. The device test that holds the `ReactSource` mirror to the native catalogue now compares the lines themselves. |
| `SoundFrameLayout` | 131 | **deleted**: 109 float offsets into `GeodeSoundFrame` and its array sizes that no Kotlin read (the frame no longer crosses JNI). Its test's cases (scalars in order, `lib_sound.glsl` naming each in its slot) are in `core/tests/sound_test.cpp`; the one constant in use, the tuning's 30 floats, is `SoundTuning.FLOATS`, still held to the header by `SoundTuningTest`. |
| `TransitionStyle` | 9 | pending |
| `TouchField` | 13 | pending |
| `FluidQuality` | 13 | pending |
| `SceneFactory` | 14 | pending |
| `FluidShaderTemplate` | 15 | pending |
| `PlaybackMath` | 18 | pending |
| `BestEffort` | 18 | pending |
| `UnderlayBlend` | 22 | pending |
| `BlendMode` | 24 | pending |
| `StyleSchedule` | 31 | pending |
| `Adsr` (`EnvBand`, `AdsrConfig`, `AdsrEngine`) | 33 | pending |
| `SceneCapabilities` (`SceneKind`) | 37 | pending |
| `StyleFamilies` | 37 | pending |
| `ThermalGovernor` | 38 | **ported**: `core/viz/frame/ThermalSource` reads the NDK's `AThermal_*` (status listener, 30 s headroom on the won slot) into `ThermalState`; `geode_thermal_push` is gone. |
| `SoundCursor` | 39 | pending |
| `GyroLook` | 50 | pending |
| `MilkStarterPack` | 50 | pending |
| `BuildStyleReactionsUseCase` | 51 | pending |
| `PcmTap` | 55 | pending |
| `AiffPcm` | 57 | pending |
| `LookSnapshot` | 70 | pending |
| `SceneIds` | 70 | pending |
| `SoundTuning` | 76 | pending |
| `TransitionCatalog` | 79 | pending |
| `SceneParamsCodec` | 80 | pending |
| `ModConfigCodec` | 81 | pending |
| `EventRecipe` | 86 | pending |
| `ParamSchema` | 90 | pending |
| `PerformanceSettings` | 94 | pending |
| `ParamScope` | 94 | pending |
| `SampleRing` | 95 | pending |
| `LookApplier` | 97 | **ported**: the live frame is `geode_viz_frame` (`core/viz/frame/LiveFrame`, test `core/tests/live_frame_test.cpp`); `NativeViz.setLook` pushes a look's changed parts when they change; `PushVersions` and `LastSent` are gone. |
| `VisualStyleCatalog` | 106 | pending |
| `MilkPresetMemory` | 116 | pending |
| `OffscreenSceneRenderer` | 128 | pending |
| `SearchMatcher` | 131 | pending |
| `Catalogue` | 131 | pending |
| `ParamFormat` | 153 | pending |
| `TrackAudio` | 158 | pending |
| `ParamKeys` | 158 | pending |
| `Lfo` (`LfoTarget`, `LfoConfig`, `ModTable`, …) | 219 | pending |
| `VisualizerView` | 220 | pending |
| `Reactions` (`ReactSource`, `ReactionRoute`, `HandMask`) | 229 | pending |
| `FramePacer` | 248 | pending |
| `SceneParams` | 272 | pending |
| `VisualizerRenderer` | 313 | pending |
| `NativeViz` | 522 | pending |
| `GeodeNative` | 2,736 | declarations; its non-declaration members pending |
