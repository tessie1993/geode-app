package dev.geode.engine.bridge

// The JNI declarations of geode_customize_jni.cpp (CLAUDE.md), and the customize calls geode_viz_jni.cpp carries.
object CustomizeNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * The Customize panel's map (core/viz/CustomizeMap) as its versioned line table: the ten
     * categories, their Basics and Pro sections, and the controls in each. Static; parse once
     * (the app's CustomizeMapTable does).
     */
    external fun customizeText(): String

    /** Every control a roll can move (control keys, one per line), so the panel offers to keep it; ask once. */
    external fun customizeLockableKeys(): String

    /** What the panel offers on [styleId]: `CATEGORY` and `CATEGORY/section` lines, in panel order. */
    external fun customizeShown(styleId: String): String

    /** How many fields of the wire frame [input] differ from the defaults (core/viz/CustomizeMap `changedCount`). */
    external fun customizeChangedCount(input: FloatArray): Int

    /** [input] with every field [category]'s controls write put back to its default, into [output]. */
    external fun customizeReset(
        category: String,
        input: FloatArray,
        output: FloatArray,
    )

    /** [input] with every field [category]'s controls write taken from the look in [from], into [output]. */
    external fun customizeBorrow(
        category: String,
        input: FloatArray,
        from: FloatArray,
        output: FloatArray,
    )

    /**
     * What each control in [keys] (one Customize map key a line) draws as on the look [params] and [styleId]: rows of
     * widget, field, words, value, range, default, unit and steps, or none where it hides (core/viz/CustomizeRows).
     * [query] is the panel's search, [transition] whether a style transition is chosen, [liquidMode] the renderer's.
     */
    external fun customizeRows(
        styleId: String,
        keys: String,
        query: String,
        transition: Boolean,
        liquidMode: Int,
        params: FloatArray,
    ): String

    /** Whether the panel search [query] matches [text] (core/viz/CustomizeRows `matches`): blank matches all, else case-blind. */
    external fun customizeMatches(
        query: String,
        text: String,
    ): Boolean

    /** Whether the panel search [query] has nothing in it, so whole sections show (core/viz/CustomizeRows `blank`). */
    external fun customizeQueryBlank(query: String): Boolean

    /** A panel sentence by its id with "%1$s".. filled by [args], at most four (core/viz/PanelText `format`); "" for an unknown id. */
    external fun panelFormat(
        id: String,
        args: Array<String>,
    ): String

    /** A word or sentence the Customize panel draws, by its id (core/viz/PanelText); "" for an unknown id. */
    external fun panelText(id: String): String

    /** A style's display name (core/viz/StyleNames). */
    external fun styleLabel(styleId: String): String

    /** A Styles page family's display name (core/viz/StyleNames). */
    external fun styleFamilyLabel(family: String): String

    /** Your colours' editor state on the look [params]' own colours (core/viz/PalettePanel). */
    external fun paletteMakerStart(params: FloatArray): String

    /** Your colours' editor state on the look [params]' hue ramp alone. */
    external fun paletteMakerFromLook(params: FloatArray): String

    /** Your colours' editor state on a saved palette: its [stops], else its [base] and [span] as a ramp. */
    external fun paletteMakerOpen(
        stops: IntArray,
        base: Float,
        span: Float,
    ): String

    /** [state] after [op] (`PALETTE_MAKER_*`) with [arg]: the stop to select or the selected stop's new colour. */
    external fun paletteMakerEdit(
        state: String,
        op: Int,
        arg: Int,
    ): String

    /** The packed colours of an editor [state]. */
    external fun paletteMakerStops(state: String): IntArray

    /** An editor [state] as Your colours draws it (rows in core/viz/PalettePanel.hpp). */
    external fun paletteMakerView(state: String): String

    /** A saved palette's colours as a stack layer: its [stops], else its ramp. */
    external fun paletteSavedColours(
        stops: IntArray,
        base: Float,
        span: Float,
    ): IntArray

    /** A saved palette's strip: "RRGGBB" joined by ','. */
    external fun paletteSavedSwatch(
        stops: IntArray,
        base: Float,
        span: Float,
    ): String

    /** Look's palette list on a style the stack does not draw ([savedIds] and [savedNames] one a line). */
    external fun paletteListView(
        params: FloatArray,
        customId: String,
        savedIds: String,
        savedNames: String,
    ): String

    /** The palette stack card on the look [params]; [savedCount] is how many palettes the user saved. */
    external fun paletteStackCard(
        stack: String,
        params: FloatArray,
        savedCount: Int,
    ): String

    const val PALETTE_MAKER_SELECT: Int = 0
    const val PALETTE_MAKER_SET: Int = 1
    const val PALETTE_MAKER_ADD: Int = 2
    const val PALETTE_MAKER_REMOVE: Int = 3
}
