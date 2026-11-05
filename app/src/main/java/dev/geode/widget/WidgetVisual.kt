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
import dev.geode.render.scene.VisualStyleCatalog
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

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

    // The phase one tick moves the lights; a full turn takes this many ticks (about 4 minutes).
    private const val TICKS_PER_TURN = 12

    /** The style's display name; the catalogue's when it has one, else the id in words. */
    fun label(styleId: String): String {
        val catalogued = VisualStyleCatalog.label(styleId)
        if (catalogued != styleId) return catalogued
        return styleId.split('_').joinToString(" ") { word -> word.replaceFirstChar { it.uppercase() } }
    }

    /** The picture for [state]: the style's thumbnail when it has one, else the glow. Never null. */
    fun render(
        context: Context,
        state: WidgetState,
    ): Bitmap = state.styleId?.let { thumbnail(context, it) } ?: glow(state)

    private fun thumbnail(
        context: Context,
        styleId: String,
    ): Bitmap? =
        runCatching {
            context.assets.open("thumbs/$styleId.webp").use { BitmapFactory.decodeStream(it) }
        }.getOrNull()

    private fun glow(state: WidgetState): Bitmap {
        val bitmap = Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.BLACK)
        val base = baseHue(state)
        val spread = if (state.artHue >= 0f) state.artSpan.coerceIn(MIN_SPREAD, MAX_SPREAD) else DEFAULT_SPREAD
        val phase = (state.tick % TICKS_PER_TURN) * (2 * PI / TICKS_PER_TURN)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        LIGHTS.forEachIndexed { i, light ->
            val angle = phase * light.speed + light.offset
            val x = WIDTH * (light.x + DRIFT * cos(angle)).toFloat()
            val y = HEIGHT * (light.y + DRIFT * sin(angle)).toFloat()
            val hue = (base + spread * (i - 1)).mod(1f) * HUE_DEGREES
            val color = Color.HSVToColor(LIGHT_ALPHA, floatArrayOf(hue, light.saturation, light.value))
            paint.shader = RadialGradient(x, y, WIDTH * light.radius, color, Color.TRANSPARENT, Shader.TileMode.CLAMP)
            canvas.drawRect(0f, 0f, WIDTH.toFloat(), HEIGHT.toFloat(), paint)
        }
        return bitmap
    }

    /**
     * The artwork's own hue when it has one, so the widget matches the cover beside it. Without one,
     * a hue hashed from the style id: each style keeps its colour, and stepping styles visibly
     * changes the picture even before any thumbnail exists.
     */
    private fun baseHue(state: WidgetState): Float =
        when {
            state.artHue >= 0f -> state.artHue
            state.styleId != null -> abs(state.styleId.hashCode() % HUE_STEPS) / HUE_STEPS.toFloat()
            else -> FALLBACK_HUE
        }

    /**
     * The artwork's palette (native, [GeodeNative.artPalette]: read at [GeodeNative.ART_HUE] and
     * [GeodeNative.ART_SPAN]), for [WidgetState.artHue] and [WidgetState.artSpan]; null when it has no colour.
     */
    fun paletteOf(art: Bitmap): FloatArray? {
        val small = Bitmap.createScaledBitmap(art, PALETTE_SIDE, PALETTE_SIDE, true)
        val pixels = IntArray(PALETTE_SIDE * PALETTE_SIDE)
        small.getPixels(pixels, 0, PALETTE_SIDE, 0, 0, PALETTE_SIDE, PALETTE_SIDE)
        if (small !== art) small.recycle()
        val palette = FloatArray(GeodeNative.ART_PALETTE_SIZE)
        return palette.takeIf { GeodeNative.artPalette(pixels, it) }
    }

    private class Light(
        val x: Double,
        val y: Double,
        val radius: Float,
        val speed: Double,
        val offset: Double,
        val saturation: Float,
        val value: Float,
    )

    private val LIGHTS =
        listOf(
            Light(x = 0.25, y = 0.35, radius = 0.55f, speed = 1.0, offset = 0.0, saturation = 0.75f, value = 0.95f),
            Light(x = 0.70, y = 0.60, radius = 0.50f, speed = -1.0, offset = 2.1, saturation = 0.65f, value = 0.85f),
            Light(x = 0.50, y = 0.15, radius = 0.35f, speed = 2.0, offset = 4.2, saturation = 0.40f, value = 1.0f),
        )

    private const val DRIFT = 0.12
    private const val LIGHT_ALPHA = 200
    private const val HUE_DEGREES = 360f
    private const val HUE_STEPS = 360
    private const val MIN_SPREAD = 0.04f
    private const val MAX_SPREAD = 0.18f
    private const val DEFAULT_SPREAD = 0.10f

    // The theme's accent hue (#9CC7FF), for a widget that has neither a cover nor a style.
    private const val FALLBACK_HUE = 0.59f
    private const val PALETTE_SIDE = 32
}
