package dev.geode.widget

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import dev.geode.engine.bridge.GeodeNative
import dev.geode.viz.ArtPalette

/**
 * The widget's picture of the visual: a home-screen widget cannot run the GL renderer, so it shows
 * the style on screen as a still that changes on [WidgetPublisher]'s tick.
 *
 * A style with a rendered thumbnail (`assets/thumbs/<id>.webp`, see its README) shows that. Any
 * other (no thumbnail yet, MilkDrop, or no style published because no session ran) gets a
 * generated glow: three soft lights over black in the style's hue and the artwork's palette,
 * moved a little on every tick, so a playing widget breathes rather than freezing.
 */
internal object WidgetVisual {
    /** Long side of the bitmap. Thumbnails are 256 px and the glow is soft, so more buys nothing. */
    private const val WIDTH = 256
    private const val HEIGHT = 160

    /** What a picture was drawn from. A picture is drawn again only when its source changes. */
    sealed interface Source

    /** A style's thumbnail: the same for as long as the style is. */
    data class Thumbnail(
        val styleId: String,
    ) : Source

    /** The glow: everything [glow] reads, so it still moves on every tick. */
    data class Glow(
        val styleId: String?,
        val artHue: Float,
        val artSpan: Float,
        val tick: Int,
    ) : Source

    class Picture(
        val bitmap: Bitmap,
        val source: Source,
    )

    /**
     * The picture for [state]: the style's thumbnail when it has one, else the glow. Never null.
     * [previous] is returned as it is when it was drawn from the same source, so a state that only
     * moved the progress bar costs no asset read and no decode.
     */
    fun render(
        context: Context,
        state: WidgetState,
        previous: Picture? = null,
    ): Picture {
        val styleId = state.styleId
        val glowSource = Glow(styleId, state.artHue, state.artSpan, state.tick)
        val drawn = previous?.source
        val sameThumbnail = drawn is Thumbnail && drawn.styleId == styleId
        if (previous != null && (drawn == glowSource || sameThumbnail)) return previous
        // A style that drew the glow last time has no thumbnail to look for again.
        val known = drawn is Glow && drawn.styleId == styleId
        val decoded = if (styleId == null || known) null else thumbnail(context, styleId)
        return if (decoded != null && styleId != null) Picture(decoded, Thumbnail(styleId)) else Picture(glow(state), glowSource)
    }

    private fun thumbnail(
        context: Context,
        styleId: String,
    ): Bitmap? =
        runCatching {
            context.assets.open("thumbs/$styleId.webp").use { BitmapFactory.decodeStream(it) }
        }.getOrNull()

    /**
     * Where native puts the three lights (`core/viz/WidgetGlow`). A JVM test has no native library, so it swaps this for
     * fixed lights; the maths is pinned by `widget_glow_test`.
     */
    internal var lightsOf: (WidgetState) -> FloatArray = { state ->
        GeodeNative.widgetGlow(state.artHue, state.artSpan, state.styleId != null, state.styleId.hashCode(), state.tick)
    }

    private fun glow(state: WidgetState): Bitmap {
        val bitmap = Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.BLACK)
        val lights = lightsOf(state)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        for (i in 0 until LIGHT_COUNT) {
            val at = i * LIGHT_FLOATS
            val color = Color.HSVToColor(LIGHT_ALPHA, floatArrayOf(lights[at + 3], lights[at + 4], lights[at + 5]))
            val centreX = WIDTH * lights[at]
            val centreY = HEIGHT * lights[at + 1]
            paint.shader = RadialGradient(centreX, centreY, WIDTH * lights[at + 2], color, Color.TRANSPARENT, Shader.TileMode.CLAMP)
            canvas.drawRect(0f, 0f, WIDTH.toFloat(), HEIGHT.toFloat(), paint)
        }
        return bitmap
    }

    /** The artwork's palette, for [WidgetState.artHue] and [WidgetState.artSpan]; null when it has no colour. */
    fun paletteOf(art: Bitmap): ArtPalette.Extracted? {
        val small = Bitmap.createScaledBitmap(art, PALETTE_SIDE, PALETTE_SIDE, true)
        val pixels = IntArray(PALETTE_SIDE * PALETTE_SIDE)
        small.getPixels(pixels, 0, PALETTE_SIDE, 0, 0, PALETTE_SIDE, PALETTE_SIDE)
        if (small !== art) small.recycle()
        return ArtPalette.extract(pixels)
    }

    private const val LIGHT_ALPHA = 200
    private const val LIGHT_COUNT = 3
    private const val LIGHT_FLOATS = 6
    private const val PALETTE_SIDE = 32
}
