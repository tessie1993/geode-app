package dev.geode.stage

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
 * The wordmark Record's tee draws over each copied frame, as pixels: the text is the one thing that needs
 * Android. Whether a live frame is marked, where, and every size, colour and position of the drawing are
 * native (`core/viz/frame/WatermarkPass`); [placement] 0 is no mark and no [tile].
 */
class RecordMark(
    context: Context,
    choice: WatermarkChoice,
    frameWidth: Int,
    frameHeight: Int,
) {
    val placement = GeodeNative.exportWatermarkPlacement(choice.required, choice.proAdds, choice.placement, WatermarkChoice.USE_LIVE)
    val corner = choice.corner
    var tile: ByteBuffer? = null
        private set
    var width = 0
        private set
    var height = 0
        private set

    init {
        if (placement != 0) rasterise(context.getString(R.string.pro_watermark_text), minOf(frameWidth, frameHeight))
    }

    private fun rasterise(
        text: String,
        shortSide: Int,
    ) {
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
        val raster = FloatArray(RASTER_FLOATS)
        GeodeNative.exportWatermarkRaster(paint.measureText(text), metrics.ascent, metrics.descent, textPx, placement, raster)
        width = raster[0].toInt()
        height = raster[1].toInt()
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        Canvas(bitmap).apply {
            rotate(raster[2], raster[3], raster[4])
            drawText(text, raster[5], raster[6], paint)
        }
        tile = ByteBuffer.allocateDirect(bitmap.byteCount).order(ByteOrder.nativeOrder()).also(bitmap::copyPixelsToBuffer)
        bitmap.recycle()
    }

    private companion object {
        const val PAINT_FLOATS = 5
        const val RASTER_FLOATS = 7
    }
}
