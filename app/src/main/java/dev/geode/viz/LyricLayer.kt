package dev.geode.viz

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.text.TextPaint
import android.text.TextUtils

/** Where the synced lyric line sits on screen. */
internal enum class LyricPosition {
    TOP,
    CENTER,
    BOTTOM,
}

/** How large the current/next lines are, as a fraction of the frame's short side. */
internal enum class LyricSize(
    val currentFraction: Float,
    val nextFraction: Float,
) {
    SMALL(0.036f, 0.026f),
    MEDIUM(0.048f, 0.034f),
    LARGE(0.064f, 0.044f),
}

internal data class LyricOptions(
    val enabled: Boolean = false,
    val position: LyricPosition = LyricPosition.BOTTOM,
    val size: LyricSize = LyricSize.MEDIUM,
)

/**
 * Draws the synced lyric line for the current track position, centred, with the next line faded
 * beneath (or above, for [LyricPosition.TOP]) it. Text is drawn with a drop shadow so it reads
 * over any scene, the same way [ArtTitleLayer] does.
 *
 * Reads [OverlayFrame.currentLine] / [OverlayFrame.nextLine] rather than [OverlayFrame.positionMs]
 * itself - the overlay controller that will sit above this turns a position into a line, throttled to
 * when the line actually changes.
 */
internal data class LyricLayer(
    private val options: LyricOptions,
) : OverlayLayer {
    override val enabled: Boolean
        get() = options.enabled

    override fun draw(
        canvas: Canvas,
        frame: OverlayFrame,
    ) {
        if (!enabled) return
        val current = frame.currentLine?.takeIf { it.isNotBlank() }
        val next = frame.nextLine?.takeIf { it.isNotBlank() }
        if (current == null && next == null) return

        val w = frame.width.toFloat()
        val h = frame.height.toFloat()
        val short = minOf(w, h)
        if (short <= 0f) return

        val margin = short * MARGIN_FRACTION
        val lineGap = short * LINE_GAP_FRACTION
        val maxWidth = (w - margin * 2f).coerceAtLeast(0f)

        val currentPaint = current?.let { textPaint(short * options.size.currentFraction, NEXT_ALPHA_FULL) }
        val nextPaint = next?.let { textPaint(short * options.size.nextFraction, NEXT_ALPHA_FADED) }
        val currentLine = current?.let { currentPaint?.let { p -> ellipsize(it, p, maxWidth) } }
        val nextLine = next?.let { nextPaint?.let { p -> ellipsize(it, p, maxWidth) } }

        val currentHeight = currentPaint?.let(::lineHeight) ?: 0f
        val nextHeight = nextPaint?.let(::lineHeight) ?: 0f
        val blockHeight = currentHeight + nextHeight + (if (currentHeight > 0f && nextHeight > 0f) lineGap else 0f)

        val top =
            when (options.position) {
                LyricPosition.TOP -> margin
                LyricPosition.CENTER -> (h - blockHeight) / 2f
                LyricPosition.BOTTOM -> h - margin - blockHeight
            }

        var y = top
        if (currentLine != null && currentPaint != null) {
            val fm = currentPaint.fontMetrics
            canvas.drawText(currentLine, w / 2f, y - fm.ascent, currentPaint)
            y += fm.descent - fm.ascent + lineGap
        }
        if (nextLine != null && nextPaint != null) {
            val fm = nextPaint.fontMetrics
            canvas.drawText(nextLine, w / 2f, y - fm.ascent, nextPaint)
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
        alpha: Int,
    ): TextPaint =
        TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.argb(alpha, 255, 255, 255)
            textAlign = Paint.Align.CENTER
            textSize = sizePx
            typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
            setShadowLayer(sizePx * 0.35f, 0f, sizePx * 0.06f, Color.argb(200, 0, 0, 0))
        }

    private companion object {
        const val MARGIN_FRACTION = 0.06f
        const val LINE_GAP_FRACTION = 0.014f
        const val NEXT_ALPHA_FULL = 255
        const val NEXT_ALPHA_FADED = 130
    }
}
