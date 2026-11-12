# Gradle modules

One fact per line. Add a fact as a new bullet under its module.

```
:app ──► :engine:scenes ──api──► :engine:audio-core (JVM, pure Kotlin)
:app ──externalNativeBuild──► CMakeLists.txt ──► libgeode.so + libprojectM-4.so
```

## `:engine:audio-core`

- Packages: `dev.geode.engine.audio`, `dev.geode.engine.bridge`.
- `GeodeNative` holds every JNI `external fun`.
- `SampleRing` is the PCM ring's handle; the ring, its writers' int16 and 8-bit conversions and its readers are native (`core/audio/ingest/PcmRing`).
- `SampleRing` is freed when the holder is collected.
- `SoundCursor` is Cast's 16-bit reader of the ring.
- `SoundTuning` is the user tuning (Settings › Audio, kept in `SoundInputSettings`): a float holder.
- `SoundTuning`'s defaults, ranges and clamp are native (`core/sound/Tuning`, `geode_sound_tuning_*`).

## `:engine:scenes`

- Packages: `dev.geode.analysis`, `dev.geode.render.*`, `dev.geode.audio`.
- Packages: `dev.geode.render.*`, `dev.geode.audio`.
- `MilkPresetMemory` is the one MilkDrop preset every new native renderer starts on: process-wide, saved across restarts, a bundled preset on first launch.
- Offscreen rendering for export lives here.
- Scene ids and params live here.
- `render/scene/ParamSchema` is the shell's one read of the native params schema (each `SceneParams` field's slot, kind, default, range and fade, from `geode_params_schema_text`).
- `ParamSchema` also reads the built-in palettes' names from the same text.
- `render/bridge/SceneParamsCodec` packs the wire by `ParamSchema`.
- `render/scene/Catalogue` is the shell's one read of the native style catalogue (ids, kinds, `StyleFamilies`, `ParamScope`, ranges).
- The catalogue's display labels are in `VisualStyleCatalog`.
- The modulation document is native (`core/viz/ModDocument`): the waves, envelopes and routes as saved text, read, edited one field at a time and pushed to a renderer as that text.
- `core/viz/ModCatalog` publishes what the editors list (`geode_mod_catalog_text`): every source, target, wave shape, curve, band, mode, group and recipe, the slider ranges and the slot counts.
- `core/viz/ModControls` says which controls of a wave or envelope slot apply as it stands (`geode_mod_lfo_shows`, `geode_mod_adsr_shows`): shape, timing and moment for a steady wave, its period off beat timing, the knob picker on a Macro target.
- The C ABI is `core/api/geode_modulation_api.h` (`geode_mod_*`, `geode_viz_set_lfos`, `geode_viz_set_adsrs`, `geode_viz_set_reactions`); the module holds no wave, envelope or route type.
- Customize › Music's moments and its action menu's order and light/motion groups are `moment` and `music` rows of `core/viz/ModCatalog`; the shell keeps only the localised labels.
- The Customize event recipes are native: titles and sentences in `core/viz/ModCatalog`, routes, toggle and merge in `core/viz/ReactionDna` (`geode_mod_recipe_state`, `geode_mod_reactions_edit`).
- The style catalogue is not in this module: `:app`'s `render/scene/StyleCatalogue` asks native (`GeodeNative.catalogue*`).
- `render/ModTable` (in `Lfo.kt`) is the shell's read of the native modulation-target table (`core/viz/ModTargets`: each target's scope and chained slot, slot defaults, each source's natural polarity; the offer masks are hex wider than 64 bits).
- `render/EventRecipe` holds the Customize event recipes' titles; their routes, toggle and merge are native in `core/viz/ReactionDna` (`geode_event_recipe_*`, pinned by `viz_math_test`).
- The value units and a control's value text are native (`core/viz/ParamText`); `:app`'s `render/scene/ParamUnit` names the three a row sets itself.
- A Customize slider's maths is native (`core/viz/SliderMap`, `geode_slider_*`): the Reset offer, the whole-number snap and the wave period's log track; `ui/CustomizeTabs` and `ui/CustomizeAdvanced` only call it.

## `:app`

- Package: `dev.geode.*`.
- Holds everything else: UI, playback service, stores, editor model, export, wallpaper, widget.
- Its packages are listed in [app-packages.md](app-packages.md).
- `util/BestEffort` (`bestEffort(TAG, "what")`) lives here; no engine module calls it.

## Build logic

- Convention plugins live in `build-logic/src/main/kotlin/`.
- `geode.kotlin-common` sets up ktlint, detekt and the toolchain.
- `geode.jvm-library` and `geode.android-library` are the library plugins.
- The version catalog is `gradle/libs.versions.toml`.
