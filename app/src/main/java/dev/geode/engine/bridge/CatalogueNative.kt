package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for the style catalogue.
object CatalogueNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * The scene and style catalogue (core/viz/Catalogue), one question per call (`geode_catalogue_*`). A
     * list comes back as its items each followed by a newline. The answers never change in a process, so
     * the shell asks once and keeps them (dev.geode.model.StyleCatalogue).
     *
     * [catalogueStyleIds]: the styles with a trait ([STYLE_ANY] and the rest), in picker order.
     */
    external fun catalogueStyleIds(trait: Int): Array<String>

    /** Whether a style has a trait ([STYLE_ANY] and the rest); false for an unknown trait or id (`geode_catalogue_style_has`). */
    external fun catalogueStyleHas(
        trait: Int,
        styleId: String,
    ): Boolean

    /** The Styles page's families, in order. */
    external fun cataloguePickerNames(): Array<String>

    /** One Styles page family's styles, in order; none for an unknown family. */
    external fun cataloguePickerIds(family: String): Array<String>

    /** The style a saved id opens on: a retired id's replacement, any other id itself. */
    external fun catalogueResolve(styleId: String): String

    /** The style a saved look of this one is filed under: Fluid's and Cymatics' looks share one each. */
    external fun cataloguePresetStyle(styleId: String): String

    /** The scope name of a Customize control (its key); `UNIVERSAL` for a control no scope names. */
    external fun catalogueScopeOf(paramKey: String): String

    /** Whether a scope, by its name, moves the style. */
    external fun catalogueReaches(
        scope: String,
        styleId: String,
    ): Boolean

    /** Whether a style holds a control at a value of its own, which [catalogueOverride] then gives (`geode_catalogue_has_override`). */
    external fun catalogueHasOverride(
        styleId: String,
        paramKey: String,
    ): Boolean

    /** The value a style holds a control at; NaN when the control is the user's. */
    external fun catalogueOverride(
        styleId: String,
        paramKey: String,
    ): Float

    /** A size the controls quote: `fluidTiers`, `touchPoints`, `touchRotationPerDegree`; NaN for any other name. */
    external fun catalogueValue(name: String): Float

    /** The traits [catalogueStyleIds] takes (`GEODE_STYLE_*`). */
    const val STYLE_ANY: Int = 0
    const val STYLE_SHADER: Int = 1

    /** In a shader family and not the style that opens it. */
    const val STYLE_FAMILY_FOLLOWER: Int = 2

    /** What Auto's random step draws from. */
    const val STYLE_AUTO_DRAW: Int = 3
    const val STYLE_FLUID: Int = 4
    const val STYLE_CYMATICS: Int = 5

    /**
     * The catalogue's presets section (core/viz/CataloguePresets): the bundled looks and every bundled
     * preset, as its versioned line table (`geode_catalogue_presets_text`). Static; parse once (data/BuiltInPresets does).
     */
    external fun cataloguePresetsText(): String

    /**
     * The config params schema (core/viz/ConfigTable.inc) as its versioned line table: per row the group
     * (lfo, adsr, macro, palette, look, clip, theme), its index in the group, name, kind, default, control
     * range and unit (`geode_config_schema_text`). The stores' clamps and defaults are to read it. Static; parse once.
     */
    external fun configSchemaText(): String

    /**
     * What Customize's transition picker lists, as UTF-8: the built-in ids it offers, RS (U+001E), then the
     * GL Transitions library's names, each list's entries separated by US (U+001F); `core/viz/TransitionLibrary.hpp`.
     * [assets] is the app's `AssetManager`, typed as Any like [VizNative.vizCreate]'s.
     */
    external fun transitionPicker(assets: Any): ByteArray
}
