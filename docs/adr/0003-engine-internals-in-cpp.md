---
status: accepted
---

# Engine internals in C++, Kotlin only as the shell

Decided with the user on 2026-10-31: the audio and visual **engine** lives in `core/` (C++), and Kotlin is the **shell** around it (see GLOSSARY). A Kotlin file keeps engine logic only until its port lands; the port writes a native test in `core/tests` first, moves the logic, reduces the Kotlin to a call through `core/api/geode_api.h` and `GeodeNative`, and deletes the Kotlin copy in the same PR. Live and export must stay identical.

Answers from the grilling:

- **Scene and style metadata is engine.** Ids, families, capabilities, parameter scopes and ranges move to `core/viz` (`SceneRegistry`). Display labels stay in Kotlin, keyed by id, so Android string resources can translate them.
- **The Pro rule stays in Kotlin** (`pro/FreeTier`). Native says which style opens a family; Kotlin decides what free users get.
- **`ParamRandomizer` and `ParamBlend` are engine**, ported after customize-audio-sync's PRs (#108, #118, #129) merge.
- **Live and offline analysis belong to the sound engine** (`core/sound`, #153). The sound-wiring session deletes the Kotlin path; this ADR does not port it.
- **Tests:** each port gets new native tests written for it (Matt Pocock `tdd`, the `core/tests/viz_math_test.cpp` style: plain `check()`, host compiler, CI's "Native host tests" step). JVM tests of moved logic go with it.

## Classification (2026-10-31)

| Kotlin | Lines | Kind | C++ target | Blocker |
|---|---|---|---|---|
| `render/TouchField`, `scene/CymaticsMath`, most of `render/VisualSafety` | 740 | Engine, dead copy of `core/viz` | already native | none: **PR 1** deletes it |
| `export/LoudnessMeter` (`LoudnessAnalyser`: K-weighting, gating, true peak) | ~420 of 657 | Engine | `core/audio/dsp/Loudness` | none: PR 2 |
| `export/PcmResampler` | 136 | Engine | `core/audio/dsp` | none: PR 3 |
| `scene/ParamRandomizer`, `render/ParamBlend` | 388 | Engine | `core/viz/Params` | #108, #118, #129 |
| `scene/VisualStyleCatalog`, `StyleFamilies`, `SceneCapabilities`, `ParamScope`, `ParamKeys` (ids only), `SceneParams` ranges, `MarchBudget`, `SceneIds`, `MilkStarterPack`, `TouchTransform`, `render/TransitionCatalog` | 2542 | Engine (metadata) | `core/viz/SceneRegistry` | #108, #118, #129 |
| `analysis/*` (`AnalysisEngine`, `OfflineAnalyzer`, `FeatureTimeline`, `FrameAccumulator`, `AnalysisCache`, `BeatTuning`, `LiveInputProfile`, `SceneSuggester`, `KeyPalette`, `AudioFeatures`), audio-core `ReactiveAnalyzer`, `DrumChannels`, `PulseReplay`, `MidSideWindow`, `AnalysisFft`, `render/LiveSignal`, `wallpaper/IdleFeatures` | ~2100 | Engine | `core/sound` | sound-wiring removes them |
| audio-core `SampleRing`, `AudioPresentationClock`, `PresentationMapping`, `ClockSegment`; app `audio/PcmRingBuffer` | 367 | Engine (per-buffer ring and clock maths) | `core/audio` | how `core/sound` takes PCM (sound-wiring) |
| `render/Lfo`, `Adsr`, `Reactions`, `ReactionRouteCodec`, `EventRecipe`, `bridge/*Codec`, `FeatureFrameLayout`, `GeodeNative` | ~1500 | Shell: config types, ordinals, wire layouts | none | `EventRecipe` re-pointed by sound-wiring |
| `render/VisualizerView`, `VisualizerRenderer`, `FramePacer`, `GyroLook`, `ThermalGovernor`, `MilkPresetMemory`, `offscreen/*`, `bridge/NativeViz`, `scene/GlUtil`, `fluid/*` | ~1400 | Shell: GL thread, Choreographer, sensors | none | none |
| audio-android `PcmTap*`, `SinkClock*`; app `playback/*`, `audio/*` (DSP is native already), `AiffPcm`/`AiffExtractor` | ~3700 | Shell: Media3, services, capture, container IO | none | none |
| `export/*` other (`VideoExporter`, `LoopRender`, `LoopExtend`, `StudioExporter`, `AudioTranscoder`, `LoudnessTarget`, clip effects, `CubeLut`) | ~5400 | Shell: MediaCodec, Muxer, Media3, advice text | none | none |
| `analysis/ArtPalette`, `PlaybackMath`, `BarTrim`, `SearchMatcher`, `viz/BackgroundImage` | ~340 | Shell: artwork colours, sleep fade, search | none | `BarTrim` goes with the BPM labels |

## Consequences

- PRs 1–3 are free to land now; the metadata and randomizer ports wait for the Customize PRs, the ring and clock for sound-wiring.
- Every port adds its sources to the host-test line in `.github/workflows/android.yml`, so a pure-maths file must not include GL or JNI headers.
- AGENTS.md says which logic is native after each port.
