package dev.geode.engine.bridge

// The JNI declarations of geode_palette_jni.cpp (CLAUDE.md), and the colour and palette calls geode_viz_jni.cpp carries.
object PaletteNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** Your colours (core/viz/Colour.hpp): a packed 0xRRGGBB from hue (wraps), saturation and value (`geode_colour_from_hsv`). Pure. */
    external fun colourFromHsv(
        h: Float,
        s: Float,
        v: Float,
    ): Int

    /** Hue, saturation and value (0..1) of a packed 0xRRGGBB into [out] (`geode_colour_to_hsv`). Pure. */
    external fun colourToHsv(
        rgb: Int,
        out: FloatArray,
    )

    /** "#RRGGBB", "RRGGBB", "#RGB" or "RGB" as a packed colour, -1 if it is none (`geode_colour_parse_hex`). Pure. */
    external fun colourParseHex(text: String): Int

    /** A packed colour as "#RRGGBB" (`geode_colour_hex`). Pure. */
    external fun colourHex(rgb: Int): String

    /** The mean of an Android bitmap's ARGB pixels, alpha ignored: the eyedropper (`geode_colour_average_argb`). Pure. */
    external fun colourAverageArgb(argb: IntArray): Int

    /** The shell's accent for a palette of hue [base] and [range]: a packed 0xRRGGBB (`geode_colour_palette_accent`). Pure. */
    external fun colourPaletteAccent(
        base: Float,
        range: Float,
    ): Int

    /** A cover's ARGB pixels as [base hue, span, confidence], or null when none is coloured (`geode_art_palette_extract`). Pure. */
    external fun artPaletteExtract(argb: IntArray): FloatArray?

    /**
     * The widget glow's three lights as 6 floats each: x and y (shares of the bitmap), radius (share of its width), hue in
     * degrees, saturation, value (`geode_widget_glow`). [styleHash] is the style id's `hashCode`. Pure.
     */
    external fun widgetGlow(
        artHue: Float,
        artSpan: Float,
        hasStyle: Boolean,
        styleHash: Int,
        tick: Int,
    ): FloatArray

    /** [samples] colours along your colours' loop through [stops] (`geode_palette_ramp`). Pure. */
    external fun paletteRamp(
        stops: IntArray,
        samples: Int,
    ): IntArray

    /** A hue gradient as [count] (<= 5) stops, where the loop puts them (`geode_palette_stops_from_hues`). Pure. */
    external fun paletteStopsFromHues(
        base: Float,
        range: Float,
        count: Int,
    ): IntArray

    /** The stack as Customize › Look draws it: its layers by name and the library's palettes (`geode_palette_stack_editor`). Pure. */
    external fun paletteStackEditor(stack: String): String

    /** [stack] as native keeps it; unreadable text is the unset stack (`geode_palette_stack_normalize`). Pure. */
    external fun paletteStackNormalize(stack: String): String

    /** [stack] with library palette [index] added last (`geode_palette_stack_add_library`). Pure. */
    external fun paletteStackAddLibrary(
        stack: String,
        index: Int,
    ): String

    /** [stack] with the user's own colours [rgb] added last as one palette (`geode_palette_stack_add_colours`). Pure. */
    external fun paletteStackAddColours(
        stack: String,
        name: String,
        rgb: IntArray,
    ): String

    /** [stack] without [layer] (`geode_palette_stack_remove`). Pure. */
    external fun paletteStackRemove(
        stack: String,
        layer: Int,
    ): String

    /** [stack] with [layer] moved [delta] places (`geode_palette_stack_move`). Pure. */
    external fun paletteStackMove(
        stack: String,
        layer: Int,
        delta: Int,
    ): String

    /** [stack] sharing by [blend], SHARE or MIX (`geode_palette_stack_set_blend`). Pure. */
    external fun paletteStackSetBlend(
        stack: String,
        blend: String,
    ): String

    /** [samples] colours of [layer]'s own run (`geode_palette_stack_layer_swatch`). Pure. */
    external fun paletteStackLayerSwatch(
        stack: String,
        layer: Int,
        samples: Int,
    ): IntArray

    /** [samples] colours of library palette [index] (`geode_palette_library_swatch`). Pure. */
    external fun paletteLibrarySwatch(
        index: Int,
        samples: Int,
    ): IntArray

    /** The stack every renderer of the process draws; the unset stack draws the look's own colours (`geode_palette_stack_set`). */
    external fun paletteStackSet(stack: String)
}
