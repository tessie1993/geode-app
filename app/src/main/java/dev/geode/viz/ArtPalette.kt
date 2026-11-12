package dev.geode.viz

import dev.geode.engine.bridge.GeodeNative

/**
 * A cover's colours as a hue gradient, for Customize › Look's "Take the colours from the artwork" and the
 * widget's glow. The reading is native (`core/viz/ArtPalette`); this only hands the pixels over.
 */
object ArtPalette {
    data class Extracted(
        val baseHue: Float,
        val span: Float,
        val confidence: Float,
    )

    /** [pixels] as Android's Bitmap.getPixels gives them; null when none is coloured. */
    fun extract(pixels: IntArray): Extracted? =
        GeodeNative.artPaletteExtract(pixels)?.let { Extracted(baseHue = it[0], span = it[1], confidence = it[2]) }
}
