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
 * Android. Whether a live frame is marked, where, how large and how opaque are native (`core/viz/frame/
 * WatermarkPass`); [placement] 0 is no mark and no [tile].
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
        width = GeodeNative.exportWatermarkTileWidth(textWidth, textPx, placement)
        height = GeodeNative.exportWatermarkTileHeight(width, textPx, placement)
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        Canvas(bitmap).apply {
            rotate(GeodeNative.exportWatermarkAngle(placement), bitmap.width / 2f, bitmap.height / 2f)
            val metrics = paint.fontMetrics
            drawText(text, (bitmap.width - textWidth) / 2f, bitmap.height / 2f - (metrics.ascent + metrics.descent) / 2f, paint)
        }
        tile = ByteBuffer.allocateDirect(bitmap.byteCount).order(ByteOrder.nativeOrder()).also(bitmap::copyPixelsToBuffer)
        bitmap.recycle()
    }

    private companion object {
        const val SHADOW_ALPHA = 70
        const val SHADOW_FRACTION = 0.12f
        const val LETTER_SPACING = 0.04f
    }
}
