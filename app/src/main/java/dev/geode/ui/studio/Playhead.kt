package dev.geode.ui.studio

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.geode.ui.opaline.creative.CreativeColors
import dev.geode.ui.opaline.creative.creativeSurface

val LANE_HEADER_WIDTH: Dp = 76.dp
val LANE_HEIGHT: Dp = 52.dp
val RULER_HEIGHT: Dp = 28.dp
val MARKER_LANE_HEIGHT: Dp = 30.dp
val KEYFRAME_LANE_HEIGHT: Dp = 34.dp

/** Time ruler with tick labels; a tap or drag on it moves the playhead. */
@Composable
fun TimeRuler(
    scale: TimelineScale,
    onScrub: (Long) -> Unit,
    modifier: Modifier = Modifier,
) {
    val density = LocalDensity.current.density
    Canvas(
        modifier
            .width(with(LocalDensity.current) { scale.contentPx.toDp() })
            .height(RULER_HEIGHT)
            .pointerInput(scale) {
                detectTapGestures { offset -> onScrub(scale.msOf(offset.x)) }
            }.pointerInput(scale) {
                detectDragGestures { change, _ ->
                    change.consume()
                    onScrub(scale.msOf(change.position.x))
                }
            },
    ) {
        val stepMs = rulerStepMs(scale)
        val textPaint =
            android.graphics.Paint().apply {
                color = CreativeColors.textSecondary.toArgb()
                textSize = 10f * density
                isAntiAlias = true
            }
        var ms = 0L
        while (ms <= scale.contentMs) {
            val x = scale.xOf(ms)
            val major = ms % (stepMs * 5) == 0L
            drawLine(
                CreativeColors.textPrimary.copy(alpha = if (major) 0.7f else 0.3f),
                Offset(x, size.height),
                Offset(x, size.height - if (major) 12f * density else 6f * density),
                strokeWidth = 1f,
            )
            if (major) drawContext.canvas.nativeCanvas.drawText(clockLabel(ms), x + 3f * density, 11f * density, textPaint)
            ms += stepMs
        }
    }
}

/** The vertical playhead: a pearl (glowing white) line with a small bubble drag handle on top. */
@Composable
fun Playhead(
    scale: TimelineScale,
    playheadMs: Long,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier.fillMaxWidth().fillMaxHeight()) {
        val x = scale.xOf(playheadMs)
        drawLine(
            brush = Brush.verticalGradient(listOf(Color.White.copy(alpha = 0.9f), CreativeColors.mint.copy(alpha = 0.75f))),
            start = Offset(x, 0f),
            end = Offset(x, size.height),
            strokeWidth = 2f,
        )
        drawPlayheadBubble(x)
    }
}

/** A small pearl bubble sitting on the ruler edge, standing in for a drag handle. */
private fun DrawScope.drawPlayheadBubble(x: Float) {
    val radius = 6f
    drawCircle(
        brush = Brush.radialGradient(listOf(Color.White.copy(alpha = 0.5f), Color.Transparent), radius = radius * 2.4f),
        radius = radius * 2.4f,
        center = Offset(x, radius),
    )
    drawCircle(CreativeColors.glassFill, radius = radius, center = Offset(x, radius))
    drawCircle(
        CreativeColors.glassRim,
        radius = radius,
        center = Offset(x, radius),
        style = Stroke(1.5f),
    )
    drawCircle(
        Color.White.copy(alpha = 0.7f),
        radius = radius * 0.35f,
        center = Offset(x - radius * 0.3f, radius - radius * 0.3f),
    )
}

/**
 * Spacer that pads a lane's content out to the full scrollable width so every lane scrolls
 * together; draws the translucent glass strip a lane's content sits on, tinted by lane kind.
 */
@Composable
fun LaneContentBox(
    scale: TimelineScale,
    height: Dp,
    modifier: Modifier = Modifier,
    tint: Color? = null,
    content: @Composable () -> Unit,
) {
    Box(
        modifier
            .width(with(LocalDensity.current) { scale.contentPx.toDp() })
            .height(height)
            .creativeSurface(shape = LANE_STRIP_SHAPE, tint = tint),
    ) { content() }
}

private val LANE_STRIP_SHAPE = RoundedCornerShape(10.dp)

/** A tick every 1, 2, 5, 10, 30 or 60 seconds, whichever keeps labels at least ~60 px apart. */
private fun rulerStepMs(scale: TimelineScale): Long {
    val candidates = longArrayOf(200L, 500L, 1_000L, 2_000L, 5_000L, 10_000L, 30_000L, 60_000L, 300_000L)
    return candidates.firstOrNull { scale.widthOf(it) >= 24f } ?: candidates.last()
}

fun clockLabel(ms: Long): String {
    val totalSeconds = ms / 1000
    val tenths = (ms % 1000) / 100
    return if (ms < 60_000L) "%d.%ds".format(totalSeconds, tenths) else "%d:%02d".format(totalSeconds / 60, totalSeconds % 60)
}
