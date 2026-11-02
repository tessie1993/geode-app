package dev.geode.ui.theme

import androidx.compose.ui.graphics.Color
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * The accent rule: the interface takes its one accent colour from the visual that is playing.
 *
 * The black glass is colourless on purpose, so the visual is the colour; the accent (selected
 * tab, active slider, primary button) borrows the visual's main colour so the controls feel part
 * of the picture instead of a fixed brand blue on top of it. A visual's colours are chosen for
 * the visual, though, not for text on black: a deep blue or a dark red would vanish as a label.
 * So the hue is kept and only the lightness is lifted, as little as it takes to clear
 * [MIN_CONTRAST] on the black canvas.
 *
 * [MIN_CONTRAST] is 7:1 (WCAG AAA), not the 4.5:1 body minimum, because the accent is also a
 * background: a primary button puts [onAccent] text on it, and only an accent at least this
 * light leaves room for a dark text colour that still clears 4.5:1 on it. Material's own dark
 * schemes sit higher still (tone 80, about 12:1), so a pastel accent is the M3 norm here.
 *
 * No colour (nothing playing, a visual that has not reported one) or a grey one means
 * [Fallback], the theme's own cool blue. Pure Kotlin on Compose's `Color`, no `android.*`, so a
 * JVM test can drive it.
 */
object VisualAccent {
    /** The accent when the visual gives no colour: the theme's own #9CC7FF. */
    val Fallback = GeodeColors.Primary

    /** Contrast every accent clears against the black canvas. */
    const val MIN_CONTRAST = 7f

    /** A source this transparent says nothing about the visual's colour. */
    private const val MIN_SOURCE_ALPHA = 0.5f

    /**
     * Below this saturation a source is grey, and a grey accent would look like the grey of
     * unselected text and icons, so selection would stop showing: such a visual gets [Fallback].
     */
    private const val MIN_SATURATION = 0.08f

    /** Lightness ceiling, so a near-white source still keeps a trace of its hue. */
    private const val MAX_LIGHTNESS = 0.92f

    private const val SEARCH_STEPS = 24
    private const val ON_ACCENT_LIGHTNESS = 0.16f
    private const val CONTAINER_LIGHTNESS = 0.18f
    private const val CONTAINER_SATURATION = 0.6f
    private const val ON_CONTAINER_LIGHTNESS = 0.86f
    private const val TONE_STEP = 0.02f

    /** The accent for a visual whose main colour is [source]: readable on black, or [Fallback]. */
    fun from(source: Color?): Color {
        if (source == null || source.alpha < MIN_SOURCE_ALPHA) return Fallback
        val opaque = source.copy(alpha = 1f)
        val hsl = Hsl.of(opaque)
        if (hsl.saturation < MIN_SATURATION) return Fallback
        if (onCanvas(opaque) >= MIN_CONTRAST) return opaque
        // Lightness only ever rises here, and contrast on black rises with it, so the lowest
        // lightness that passes is found by bisection between the source's own and the ceiling.
        var low = hsl.lightness
        var high = MAX_LIGHTNESS
        if (onCanvas(hsl.withLightness(high)) < MIN_CONTRAST) return Fallback
        repeat(SEARCH_STEPS) {
            val mid = (low + high) / 2f
            if (onCanvas(hsl.withLightness(mid)) >= MIN_CONTRAST) high = mid else low = mid
        }
        return hsl.withLightness(high)
    }

    /**
     * Text and icons drawn on the [accent] (a filled button's label): the accent's hue, dark,
     * lifted from black only as far as it still clears the body minimum on the accent.
     */
    fun onAccent(accent: Color): Color = toneFrom(accent, from = ON_ACCENT_LIGHTNESS, to = 0f, against = accent)

    /** A dark container of the accent's hue, for a tonal button or a selected chip. */
    fun container(accent: Color): Color {
        val hsl = Hsl.of(accent)
        return hsl.copy(saturation = hsl.saturation * CONTAINER_SATURATION).withLightness(CONTAINER_LIGHTNESS)
    }

    /** Text on [container]: the accent's hue, near white, as dark as it may be while readable. */
    fun onContainer(accent: Color): Color {
        val container = container(accent)
        return toneFrom(accent, from = ON_CONTAINER_LIGHTNESS, to = 1f, against = container)
    }

    /**
     * Walks the accent's hue from lightness [from] towards [to] until it reads on [against] at
     * the body minimum. [to] (black or white) always does, given the accent's own floor.
     */
    private fun toneFrom(
        accent: Color,
        from: Float,
        to: Float,
        against: Color,
    ): Color {
        val hsl = Hsl.of(accent)
        var lightness = from
        val step = if (to > from) TONE_STEP else -TONE_STEP
        while (abs(to - lightness) > TONE_STEP) {
            val candidate = hsl.withLightness(lightness)
            if (contrastRatio(candidate, against) >= GeodeContrast.BODY_MIN) return candidate
            lightness += step
        }
        return hsl.withLightness(to)
    }

    private fun onCanvas(color: Color): Float = contrastRatio(color, GeodeColors.Canvas)
}

/** Hue (degrees), saturation and lightness, the space the accent rule moves lightness in. */
internal data class Hsl(
    val hue: Float,
    val saturation: Float,
    val lightness: Float,
) {
    fun withLightness(value: Float): Color = Color.hsl(hue, saturation.coerceIn(0f, 1f), value.coerceIn(0f, 1f))

    companion object {
        fun of(color: Color): Hsl {
            val r = color.red
            val g = color.green
            val b = color.blue
            val maxC = max(r, max(g, b))
            val minC = min(r, min(g, b))
            val lightness = (maxC + minC) / 2f
            val delta = maxC - minC
            if (delta == 0f) return Hsl(0f, 0f, lightness)
            val saturation = delta / (1f - abs(2f * lightness - 1f))
            val hue =
                when (maxC) {
                    r -> 60f * (((g - b) / delta).mod(6f))
                    g -> 60f * ((b - r) / delta + 2f)
                    else -> 60f * ((r - g) / delta + 4f)
                }
            return Hsl(hue, saturation, lightness)
        }
    }
}
