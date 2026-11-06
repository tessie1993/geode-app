package dev.geode.export

import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The non-Pro watermark: the wordmark tiled diagonally across the whole frame, composited over
 * each exported frame. The video stays watchable as a preview, and unusable as a finished one.
 *
 * The compositing is native (`core/viz/frame/ExportPasses`, `WatermarkPass`, #442): drawn on the
 * export's own EGL context straight after the scene, into the default framebuffer the encoder
 * reads, restoring what it changes. What stays here is what needs Android: the text. One tile, the
 * wordmark rotated by [ANGLE_DEGREES], is rasterised once at [prepare] at the size native gives
 * (a share of the frame's short side, rounded to a power of two so the tile can repeat) and
 * handed over as pixels; per frame native draws one full-frame quad repeating it.
 */
class WatermarkOverlay(
    private val text: String,
    private val assets: AssetManager,
) {
    private var handle = 0L
    private var width = 0
    private var height = 0

    /** Needs the export's GL context current; throws when the pass cannot build, so a free render never ships unmarked. */
    fun prepare(
        frameWidth: Int,
        frameHeight: Int,
    ) {
        width = frameWidth
        height = frameHeight
        val bitmap = rasteriseTile(minOf(frameWidth, frameHeight))
        val pixels = ByteBuffer.allocateDirect(bitmap.byteCount).order(ByteOrder.nativeOrder())
        bitmap.copyPixelsToBuffer(pixels)
        handle = GeodeNative.exportWatermarkCreate(assets, pixels, bitmap.width, bitmap.height)
        bitmap.recycle()
        val problem = if (handle == 0L) "out of memory" else GeodeNative.exportPassError(handle)
        if (problem.isNotEmpty()) {
            release()
            error("watermark pass failed: $problem")
        }
    }

    /** Composites the mark over whatever is in the default framebuffer. */
    fun draw() {
        if (handle != 0L) GeodeNative.exportWatermarkDraw(handle, width, height)
    }

    fun release() {
        val owned = handle
        handle = 0L
        if (owned != 0L) GeodeNative.exportPassDestroy(owned)
    }

    private fun rasteriseTile(shortSide: Int): Bitmap {
        val textPx = GeodeNative.exportWatermarkTextPx(shortSide)
        val paint =
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = Color.WHITE
                alpha = TEXT_ALPHA
                textSize = textPx
                typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
                letterSpacing = LETTER_SPACING
                setShadowLayer(textPx * SHADOW_FRACTION, 0f, 0f, Color.argb(SHADOW_ALPHA, 0, 0, 0))
            }
        val textWidth = paint.measureText(text)
        val size = GeodeNative.exportWatermarkTileWidth(textWidth)
        return Bitmap.createBitmap(size, GeodeNative.exportWatermarkTileHeight(size), Bitmap.Config.ARGB_8888).also { tile ->
            Canvas(tile).apply {
                rotate(ANGLE_DEGREES, tile.width / 2f, tile.height / 2f)
                val metrics = paint.fontMetrics
                drawText(text, (tile.width - textWidth) / 2f, tile.height / 2f - (metrics.ascent + metrics.descent) / 2f, paint)
            }
        }
    }

    private companion object {
        const val ANGLE_DEGREES = -24f
        const val TEXT_ALPHA = 92
        const val SHADOW_ALPHA = 70
        const val SHADOW_FRACTION = 0.12f
        const val LETTER_SPACING = 0.04f
    }
}
