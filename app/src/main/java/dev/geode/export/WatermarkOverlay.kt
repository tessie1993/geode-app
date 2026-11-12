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

    // Every size, colour and position is native; only the drawing is Android's.
    private fun rasteriseTile(shortSide: Int): Bitmap {
        val look = FloatArray(PAINT_FLOATS).also { GeodeNative.exportWatermarkPaint(shortSide, placement, it) }
        val textPx = look[0]
        val paint =
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = Color.WHITE
                alpha = look[1].toInt()
                textSize = textPx
                typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
                letterSpacing = look[2]
                setShadowLayer(look[3], 0f, 0f, Color.argb(look[4].toInt(), 0, 0, 0))
            }
        val metrics = paint.fontMetrics
        val tile = FloatArray(RASTER_FLOATS)
        GeodeNative.exportWatermarkRaster(paint.measureText(text), metrics.ascent, metrics.descent, textPx, placement, tile)
        return Bitmap.createBitmap(tile[0].toInt(), tile[1].toInt(), Bitmap.Config.ARGB_8888).also { bitmap ->
            Canvas(bitmap).apply {
                rotate(tile[2], tile[3], tile[4])
                drawText(text, tile[5], tile[6], paint)
            }
        }
    }

    private companion object {
        const val PAINT_FLOATS = 5
        const val RASTER_FLOATS = 7
    }
}
