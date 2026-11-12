# Gradle modules

One fact per line. Add a fact as a new bullet under its module.

```
:app ──► :engine:scenes ──api──► :engine:audio-core (JVM, pure Kotlin)
   └───► :engine:audio-android ──api──┘
:app ──externalNativeBuild──► CMakeLists.txt ──► libgeode.so + libprojectM-4.so
```

## `:engine:audio-core`

- Packages: `dev.geode.engine.audio`, `dev.geode.engine.bridge`.
- `GeodeNative` holds every JNI `external fun`.
- `SampleRing` is the PCM ring's handle; the ring, its writers' int16 and 8-bit conversions and its readers are native (`core/audio/ingest/PcmRing`).
- `SampleRing` is freed when the holder is collected.
- `SoundCursor` is Cast's 16-bit reader of the ring.
- `SoundFrameLayout` is the sound frame layout.
- `SoundTuning` is the user tuning (Settings › Audio, kept in `SoundInputSettings`): a float holder.
- `SoundTuning`'s defaults, ranges and clamp are native (`core/sound/Tuning`, `geode_sound_tuning_*`).

## `:engine:audio-android`

- Package: `dev.geode.engine.audioandroid`.
- `PcmTap` is the Media3 PCM tap: it hands the chain's direct buffer and its format to the native ring and reads no sample.

## `:engine:scenes`

- Packages: `dev.geode.analysis`, `dev.geode.render.*`, `dev.geode.audio`, `dev.geode.util`.
- The GL adapter: `VisualizerView`, `VisualizerRenderer`, `render/bridge/NativeViz`.
- `MilkPresetMemory` is the one MilkDrop preset every new native renderer starts on: process-wide, saved across restarts, a bundled preset on first launch.
- The look snapshot is `render/look` (#345).
- `LookSnapshot` is the one value every host draws: style, params, LFOs, envelopes, the user's reactions, sound tuning, reduced motion, transition, underlay.
- `LookSource.pin` freezes the snapshot for a render.
- `gentleFlight` is the dream's override.
- `LookApplier` pushes through `LookTarget`, which `NativeViz` implements: the live renderer on the frame bits native asks for, an export on what changed.
- Offscreen rendering for export lives here.
- Scene ids and params live here.
- `render/scene/ParamSchema` is the shell's one read of the native params schema (each `SceneParams` field's slot, kind, default, range and fade, from `geode_params_schema_text`).
- `ParamSchema` also reads the built-in palettes' names from the same text.
- `render/bridge/SceneParamsCodec` packs the wire by `ParamSchema`.
- `render/scene/Catalogue` is the shell's one read of the native style catalogue (ids, kinds, `StyleFamilies`, `ParamScope`, ranges).
- The catalogue's display labels are in `VisualStyleCatalog`.
- `render/ModTable` (in `Lfo.kt`) is the shell's read of the native modulation-target table (`core/viz/ModTargets`: each target's scope and chained slot, slot defaults, each source's natural polarity).
- `render/EventRecipe` holds the Customize event recipes' titles; their routes, toggle and merge are native in `core/viz/ReactionDna` (`geode_event_recipe_*`, pinned by `viz_math_test`).
- `render/scene/ParamFormat` holds the value units.
- `render/PerformanceSettings` holds the live quality presets.
- Two pure helpers are left in `dev.geode.analysis`: `PlaybackMath`, `SearchMatcher`.

## `:app`

- Package: `dev.geode.*`.
- Holds everything else: UI, playback service, stores, editor model, export, wallpaper, widget.
- Its packages are listed in [app-packages.md](app-packages.md).

## Build logic

- Convention plugins live in `build-logic/src/main/kotlin/`.
- `geode.kotlin-common` sets up ktlint, detekt and the toolchain.
- `geode.jvm-library` and `geode.android-library` are the library plugins.
- The version catalog is `gradle/libs.versions.toml`.
