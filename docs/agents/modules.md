# Gradle modules

`:app` is the only Gradle module. The headings below group facts by the code that used to be separate modules.
One fact per line. Add a fact as a new bullet under its module.

```
:app ──externalNativeBuild──► CMakeLists.txt ──► libgeode.so + libprojectM-4.so
```

## Native bridge (`app/src/main/java/dev/geode/engine/`)

- Packages: `dev.geode.engine.bridge` (the `*Native.kt` objects), `dev.geode.engine.audioandroid` (`PcmTap.kt`).
- The `*Native` objects (`GeodeNative`, `VizNative`, ...), one per JNI source file, hold every JNI `external fun`; `GeodeNative.ensureLoaded()` loads the library for all of them.
- The PCM rings are native (`core/audio/ingest/PcmRing`); the shell holds a ring as a `Long` handle and calls `IngestNative.pcmRing*`.
- Which rings exist, their sizes and lifetimes are native (`core/audio/ingest/RingKinds`): the player's and the mic's kept for the process (`pcmRingShared`), the output mix's made and freed by its owner.
- The user's sound tuning (Settings › Audio) is a list of floats kept by `data/SoundInputSettings.kt`.
- The tuning's defaults, ranges and clamp are native (`core/sound/Tuning`, `geode_sound_tuning_*`).

## Scene params and modulation

- The shell has no parser of the params schema (`ParamSchema`, `ParamSpec` are gone).
- `SceneParams` lives in `engine/bridge/SceneParams.kt`: an opaque holder of the `geode_viz_set_params` frame and the saved palette's id.
- `SceneParams` knows no param: a value is read and set by name through `geode_params_get` and `geode_params_set`, the defaults are `geode_params_defaults`.
- JVM tests install `TestParams` (`app/src/test/java/dev/geode/engine/bridge/`), which answers from `core/tests/params_schema.txt`.
- The modulation document is native (`core/viz/ModDocument`): the waves, envelopes and routes as saved text, read, edited one field at a time and pushed to a renderer as that text.
- `core/viz/ModCatalog` publishes what the editors list (`geode_mod_catalog_text`): every source, target, wave shape, curve, band, mode, group and recipe, the slider ranges and the slot counts.
- `core/viz/ModControls` says which controls of a wave or envelope slot apply as it stands (`geode_mod_lfo_shows`, `geode_mod_adsr_shows`): shape, timing and moment for a steady wave, its period off beat timing, the knob picker on a Macro target.
- The C ABI is `core/api/geode_modulation_api.h` (`geode_mod_*`, `geode_viz_set_lfos`, `geode_viz_set_adsrs`, `geode_viz_set_reactions`); the shell holds no wave, envelope or route type.
- Customize › Music's moments and its action menu's order and light/motion groups are `moment` and `music` rows of `core/viz/ModCatalog`; the shell keeps only the localised labels.
- The Customize event recipes are native: titles and sentences in `core/viz/ModCatalog`, routes, toggle and merge in `core/viz/ReactionDna` (`geode_mod_recipe_state`, `geode_mod_reactions_edit`).
- The style catalogue: `engine/bridge/StyleCatalogue` asks native (`CatalogueNative.catalogue*`).
- The value units and a control's value text are native (`core/viz/ParamText`); `GeodeNative.PARAM_UNIT_*` names the three a row sets itself.
- A Customize slider's maths is native (`core/viz/SliderMap`, `geode_slider_*`): the Reset offer, the whole-number snap and the wave period's log track; `ui/CustomizeTabs` and `ui/CustomizeAdvanced` only call it.

## `:app`

- Package: `dev.geode.*`.
- Holds everything else: UI, playback service, stores, editor model, export, wallpaper, widget.
- Its packages are listed in [app-packages.md](app-packages.md).
- `util/BestEffort` (`bestEffort(TAG, "what")`) lives here.

## Build logic

- The convention plugin lives in `build-logic/src/main/kotlin/`.
- `geode.kotlin-common` sets up ktlint, detekt and the toolchain.
- The version catalog is `gradle/libs.versions.toml`.
