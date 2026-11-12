package dev.geode.export

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.model.WatermarkChoice
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The wordmark over each exported frame, as [choice] asks on frames of [use]: tiled across the frame
 * (the free default), in a corner, or an end card. Whether frames are marked, where, how large and
 * opaque, and when an end card shows are native (`core/viz/frame/ExportPasses`, `WatermarkPass`, #442),
 * drawn on the export's own EGL context after the scene. What stays here is what needs Android: the
 * text, rasterised once at [prepare] into the tile native sizes and handed over as pixels.
 */
class WatermarkOverlay(
    private val choice: WatermarkChoice,
    context: Context,
    use: Int = choice.use,
) {
    private val text = context.getString(R.string.pro_watermark_text)
    private val assets = context.assets
    private val placement = GeodeNative.exportWatermarkPlacement(choice.required, choice.proAdds, choice.placement, use)

    /** The native pass once [prepare]d (0 when these frames carry no mark); Record hands it to the stage's tee. */
    var handle = 0L
        private set
    private var width = 0
    private var height = 0

    /** Needs the export's GL context current; throws when the pass cannot build, so a free render never ships unmarked. */
    fun prepare(
        frameWidth: Int,
        frameHeight: Int,
        lengthMs: Long = 0L,
    ) {
        val tile = tile(frameWidth, frameHeight) ?: return
        width = frameWidth
        height = frameHeight
        handle = GeodeNative.exportWatermarkCreate(assets, tile.pixels, tile.width, tile.height, placement, choice.corner, lengthMs)
        val problem = if (handle == 0L) "out of memory" else GeodeNative.exportPassError(handle)
        if (problem.isNotEmpty()) {
            release()
            error("watermark pass failed: $problem")
        }
    }

    /** The rasterised wordmark and where it goes, for a native job that draws it itself; null when these frames carry none. */
    fun tile(
        frameWidth: Int,
        frameHeight: Int,
    ): Tile? {
        if (placement == 0) return null
        val bitmap = rasteriseTile(minOf(frameWidth, frameHeight))
        val pixels = ByteBuffer.allocateDirect(bitmap.byteCount).order(ByteOrder.nativeOrder())
        bitmap.copyPixelsToBuffer(pixels)
        return Tile(pixels, bitmap.width, bitmap.height, placement, choice.corner).also { bitmap.recycle() }
    }

    class Tile(
        val pixels: ByteBuffer,
        val width: Int,
        val height: Int,
        val placement: Int,
        val corner: Int,
    )

    /** Composites the mark over whatever is in the default framebuffer; [timeMs] is the frame's time in the video. */
    fun draw(timeMs: Long = 0L) {
        if (handle != 0L) GeodeNative.exportWatermarkDraw(handle, width, height, timeMs)
    }

    fun release() {
        val owned = handle
        handle = 0L
        if (owned != 0L) GeodeNative.exportPassDestroy(owned)
    }

    private fun rasteriseTile(shortSide: Int): Bitmap {
        val textPx = GeodeNative.exportWatermarkTextPx(shortSide, placement)
        val paint =
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = Color.WHITE
                alpha = GeodeNative.exportWatermarkAlpha(placement)
                textSize = textPx
                typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
                letterSpacing = LETTER_SPACING
                setShadowLayer(textPx * SHADOW_FRACTION, 0f, 0f, Color.argb(SHADOW_ALPHA, 0, 0, 0))
            }
        val textWidth = paint.measureText(text)
        val size = GeodeNative.exportWatermarkTileWidth(textWidth, textPx, placement)
        val tileHeight = GeodeNative.exportWatermarkTileHeight(size, textPx, placement)
        return Bitmap.createBitmap(size, tileHeight, Bitmap.Config.ARGB_8888).also { tile ->
            Canvas(tile).apply {
                rotate(GeodeNative.exportWatermarkAngle(placement), tile.width / 2f, tile.height / 2f)
                val metrics = paint.fontMetrics
                drawText(text, (tile.width - textWidth) / 2f, tile.height / 2f - (metrics.ascent + metrics.descent) / 2f, paint)
            }
        }
    }

    private companion object {
        const val SHADOW_ALPHA = 70
        const val SHADOW_FRACTION = 0.12f
        const val LETTER_SPACING = 0.04f
    }
}
