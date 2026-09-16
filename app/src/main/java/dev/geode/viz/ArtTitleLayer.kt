package dev.geode.viz

import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.text.TextPaint
import android.text.TextUtils

/** Where the cover-art + title block sits on screen. */
internal enum class OverlayPosition {
    BOTTOM_LEFT,
    BOTTOM_CENTER,
    TOP_LEFT,
    TOP_RIGHT,
}

/** How large the block is, as a fraction of the frame's short side. */
internal enum class OverlaySize(
    val artFraction: Float,
    val titleFraction: Float,
    val artistFraction: Float,
) {
    SMALL(0.07f, 0.032f, 0.024f),
    MEDIUM(0.10f, 0.042f, 0.030f),
    LARGE(0.14f, 0.052f, 0.038f),
}

internal data class ArtTitleOptions(
    val enabled: Boolean = false,
    val position: OverlayPosition = OverlayPosition.BOTTOM_LEFT,
    val size: OverlaySize = OverlaySize.MEDIUM,
    val showArtwork: Boolean = true,
    val showText: Boolean = true,
)

/**
 * Draws a rounded cover-art thumbnail plus title/artist text, anchored to one corner (or the
 * bottom centre). Text is drawn with a drop shadow so it reads over any scene.
 */
internal data class ArtTitleLayer(
    private val options: ArtTitleOptions,
) : OverlayLayer {
    override val enabled: Boolean
        get() = options.enabled && (options.showArtwork || options.showText)

    override fun draw(
        canvas: Canvas,
        frame: OverlayFrame,
    ) {
        if (!enabled) return
        val w = frame.width.toFloat()
        val h = frame.height.toFloat()
        val short = minOf(w, h)
        if (short <= 0f) return

        val artwork = frame.artwork?.takeIf { options.showArtwork }
        val hasArt = artwork != null
        val title = frame.title?.takeIf { options.showText && it.isNotBlank() }
        val artist = frame.artist?.takeIf { options.showText && it.isNotBlank() }
        if (!hasArt && title == null && artist == null) return

        val margin = short * MARGIN_FRACTION
        val artSize = short * options.size.artFraction
        val gap = short * GAP_FRACTION
        val lineGap = short * LINE_GAP_FRACTION

        val titlePaint = title?.let { textPaint(short * options.size.titleFraction, bold = true) }
        val artistPaint = artist?.let { textPaint(short * options.size.artistFraction, bold = false) }
        val stacked = options.position == OverlayPosition.BOTTOM_CENTER
        val textMaxWidth =
            if (stacked) {
                w - margin * 2f
            } else {
                (w - margin * 2f - (if (hasArt) artSize + gap else 0f)).coerceAtLeast(0f)
            }
        val titleLine = title?.let { titlePaint?.let { p -> ellipsize(it, p, textMaxWidth) } }
        val artistLine = artist?.let { artistPaint?.let { p -> ellipsize(it, p, textMaxWidth) } }

        val titleHeight = titlePaint?.let(::lineHeight) ?: 0f
        val artistHeight = artistPaint?.let(::lineHeight) ?: 0f
        val textBlockHeight =
            titleHeight + artistHeight + (if (titleHeight > 0f && artistHeight > 0f) lineGap else 0f)

        if (stacked) {
            val blockHeight = (if (hasArt) artSize + gap else 0f) + textBlockHeight
            var y = h - margin - blockHeight
            if (artwork != null) {
                drawArt(canvas, artwork, (w - artSize) / 2f, y, artSize)
                y += artSize + gap
            }
            drawTextBlock(canvas, titleLine, titlePaint, artistLine, artistPaint, w / 2f, y, lineGap, Paint.Align.CENTER)
            return
        }

        val top = options.position == OverlayPosition.TOP_LEFT || options.position == OverlayPosition.TOP_RIGHT
        val rightAligned = options.position == OverlayPosition.TOP_RIGHT
        val blockHeight = maxOf(if (hasArt) artSize else 0f, textBlockHeight)
        val blockTop = if (top) margin else h - margin - blockHeight
        val artLeft = if (rightAligned) w - margin - artSize else margin
        if (artwork != null) drawArt(canvas, artwork, artLeft, blockTop + (blockHeight - artSize) / 2f, artSize)

        val textAlign = if (rightAligned) Paint.Align.RIGHT else Paint.Align.LEFT
        val textX =
            when {
                !hasArt && rightAligned -> w - margin
                rightAligned -> artLeft - gap
                hasArt -> artLeft + artSize + gap
                else -> margin
            }
        drawTextBlock(
            canvas,
            titleLine,
            titlePaint,
            artistLine,
            artistPaint,
            textX,
            blockTop + (blockHeight - textBlockHeight) / 2f,
            lineGap,
            textAlign,
        )
    }

    private fun drawArt(
        canvas: Canvas,
        bitmap: Bitmap,
        left: Float,
        top: Float,
        size: Float,
    ) {
        if (bitmap.width <= 0 || bitmap.height <= 0) return
        val radius = size * CORNER_FRACTION
        val rect = RectF(left, top, left + size, top + size)
        // Center-crop the source into the square via the shader's own matrix, rather than a
        // separate scaled bitmap: one draw call, and nothing extra to recycle.
        val scale = size / minOf(bitmap.width, bitmap.height).toFloat()
        val dx = left - (bitmap.width * scale - size) / 2f
        val dy = top - (bitmap.height * scale - size) / 2f
        val matrix =
            Matrix().apply {
                setScale(scale, scale)
                postTranslate(dx, dy)
            }
        val shader =
            BitmapShader(bitmap, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP).apply {
                setLocalMatrix(matrix)
            }
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { this.shader = shader }
        canvas.drawRoundRect(rect, radius, radius, paint)
    }

    private fun drawTextBlock(
        canvas: Canvas,
        title: String?,
        titlePaint: TextPaint?,
        artist: String?,
        artistPaint: TextPaint?,
        x: Float,
        top: Float,
        lineGap: Float,
        align: Paint.Align,
    ) {
        titlePaint?.textAlign = align
        artistPaint?.textAlign = align
        var y = top
        if (title != null && titlePaint != null) {
            val fm = titlePaint.fontMetrics
            canvas.drawText(title, x, y - fm.ascent, titlePaint)
            y += fm.descent - fm.ascent + lineGap
        }
        if (artist != null && artistPaint != null) {
            val fm = artistPaint.fontMetrics
            canvas.drawText(artist, x, y - fm.ascent, artistPaint)
        }
    }

    private fun lineHeight(paint: TextPaint): Float = paint.fontMetrics.let { it.descent - it.ascent }

    private fun ellipsize(
        text: String,
        paint: TextPaint,
        maxWidth: Float,
    ): String? =
        if (maxWidth <= 0f) {
            null
        } else {
            TextUtils.ellipsize(text, paint, maxWidth, TextUtils.TruncateAt.END).toString().takeIf { it.isNotEmpty() }
        }

    private fun textPaint(
        sizePx: Float,
        bold: Boolean,
    ): TextPaint =
        TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = sizePx
            typeface = Typeface.create(Typeface.DEFAULT, if (bold) Typeface.BOLD else Typeface.NORMAL)
            setShadowLayer(sizePx * 0.35f, 0f, sizePx * 0.06f, Color.argb(200, 0, 0, 0))
        }

    private companion object {
        const val MARGIN_FRACTION = 0.035f
        const val GAP_FRACTION = 0.02f
        const val LINE_GAP_FRACTION = 0.01f
        const val CORNER_FRACTION = 0.18f
    }
}
