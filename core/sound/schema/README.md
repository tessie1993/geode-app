# core/sound/schema (skeleton, empty)

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). Nothing here yet.

**Owns:** one ordered table of every sound feature: name, kind (relative level, shaped 0..1, clock, trigger, vector), normalisation policy (adaptive percentile, fixed range, z-score, passthrough), smoothing, and interpolation policy (lerp, hold, pulse). Everything else is generated from it and checked in CI.

**Generates (replacing hand copies, as of `cf7e88f`)**
- `core/sound/SoundFrame.h` (the C struct; field kinds live only in comments today, `SoundFrame.h:12-18`)
- `engine/audio-core/.../audio/SoundFrameLayout.kt` (offsets)
- the `SND_*` defines in `app/src/main/assets/shaders/lib_sound.glsl`
- `ReactSource` and its labels in `core/viz/Reactions.hpp` / `Reactions.cpp` (117 sources)
- `GeodeSoundTuning` (its defaults and ranges are already one table, `core/sound/Tuning.cpp`; Kotlin's `SoundTuning.kt` keeps only the slot offsets)

Today `SoundFrameLayoutTest` parses the header and the shader to catch drift. After this step the test compares generated files with the schema instead.

**fosfora:** `audio/schema.rs` (`FEATURES: [FeatureDef; N]` with norm, smooth, decay and interp policies), `audio/features.rs` (the frozen struct, layout-guarded).

**Test that proves it:** CI regenerates every output and fails on a diff. The same step also regenerates `ScaleTable.inc` and `EmotionTable.inc` with `tools/musical_intelligence.py` and `tools/music_emotion.py` and fails on a diff (neither script has a check mode yet; add one).
