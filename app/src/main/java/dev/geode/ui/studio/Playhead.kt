package dev.geode.ui.studio

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.geode.R

val LANE_HEADER_WIDTH: Dp = 76.dp
val LANE_HEIGHT: Dp = 52.dp
val RULER_HEIGHT: Dp = 28.dp
val MARKER_LANE_HEIGHT: Dp = 30.dp
val KEYFRAME_LANE_HEIGHT: Dp = 34.dp

/**
 * Time ruler with tick labels; a tap or drag on it moves the playhead. It is also the playhead's
 * accessible control: TalkBack reads the time and adjusts it through [onScrub], since the line
 * itself is only paint over the lanes.
 */
@Composable
fun TimeRuler(
    scale: TimelineScale,
    playheadMs: () -> Long,
    onScrub: (Long) -> Unit,
    modifier: Modifier = Modifier,
) {
    val labelColor = MaterialTheme.colorScheme.onSurfaceVariant
    val density = LocalDensity.current.density
    val playheadLabel = stringResource(R.string.studio_a11y_playhead)
    // Read in composition because semantics need the value; only the ruler recomposes on a
    // scrub, and its tick drawing is memoised, so the lanes stay untouched.
    val positionMs = playheadMs().coerceIn(0L, scale.contentMs)
    Canvas(
        modifier
            .width(with(LocalDensity.current) { scale.contentPx.toDp() })
            .height(RULER_HEIGHT)
            .semantics {
                contentDescription = playheadLabel
                stateDescription = clockLabel(positionMs)
                progressBarRangeInfo = ProgressBarRangeInfo(positionMs.toFloat(), 0f..scale.contentMs.toFloat())
                setProgress { target ->
                    onScrub(target.toLong().coerceIn(0L, scale.contentMs))
                    true
                }
            }.pointerInput(scale) {
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
                color = labelColor.toArgb()
                textSize = 10f * density
                isAntiAlias = true
            }
        var ms = 0L
        while (ms <= scale.contentMs) {
            val x = scale.xOf(ms)
            val major = ms % (stepMs * 5) == 0L
            drawLine(
                labelColor.copy(alpha = if (major) 0.9f else 0.4f),
                Offset(x, size.height),
                Offset(x, size.height - if (major) 12f * density else 6f * density),
                strokeWidth = 1f,
            )
            if (major) drawContext.canvas.nativeCanvas.drawText(clockLabel(ms), x + 3f * density, 11f * density, textPaint)
            ms += stepMs
        }
    }
}

/**
 * The vertical playhead line drawn over the lanes at the current time. [playheadMs] is read in
 * the draw phase, so a scrub only redraws this line.
 */
@Composable
fun Playhead(
    scale: TimelineScale,
    playheadMs: () -> Long,
    modifier: Modifier = Modifier,
) {
    val color = MaterialTheme.colorScheme.primary
    Canvas(modifier.fillMaxWidth().fillMaxHeight()) {
        val x = scale.xOf(playheadMs())
        drawLine(color, Offset(x, 0f), Offset(x, size.height), strokeWidth = 2f)
        drawPlayheadCap(x, color)
    }
}

private fun DrawScope.drawPlayheadCap(
    x: Float,
    color: Color,
) {
    val half = 6f
    drawLine(color, Offset(x - half, 0f), Offset(x + half, 0f), strokeWidth = 4f)
}

/** Spacer that pads a lane's content out to the full scrollable width so every lane scrolls together. */
@Composable
fun LaneContentBox(
    scale: TimelineScale,
    height: Dp,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Box(modifier.width(with(LocalDensity.current) { scale.contentPx.toDp() }).height(height)) { content() }
}

/** A tick every 1, 2, 5, 10, 30 or 60 seconds, whichever keeps labels at least ~60 px apart. */
private fun rulerStepMs(scale: TimelineScale): Long {
    val candidates = longArrayOf(200L, 500L, 1_000L, 2_000L, 5_000L, 10_000L, 30_000L, 60_000L, 300_000L)
    return candidates.firstOrNull { scale.widthOf(it) >= 24f } ?: candidates.last()
}

/** The labels every lane's accessibility actions share, resolved once per lane. */
internal class LaneActionLabels(
    val next: String,
    val previous: String,
    val nudgeEarlier: String,
    val nudgeLater: String,
)

@Composable
internal fun laneActionLabels(): LaneActionLabels =
    LaneActionLabels(
        next = stringResource(R.string.studio_a11y_select_next),
        previous = stringResource(R.string.studio_a11y_select_previous),
        nudgeEarlier = stringResource(R.string.studio_a11y_nudge_earlier),
        nudgeLater = stringResource(R.string.studio_a11y_nudge_later),
    )

/**
 * The item after (or before) the one with id [current], in time order, for the lanes'
 * "select next / previous" actions. With nothing of this lane selected it starts from the first
 * (or last) item; past either end there is nothing, so the action is not offered.
 */
internal fun <T, K> List<T>.stepFrom(
    current: K?,
    forward: Boolean,
    idOf: (T) -> K,
    timeOf: (T) -> Long,
): T? {
    val ordered = sortedBy(timeOf)
    val index = ordered.indexOfFirst { idOf(it) == current }
    return if (index < 0) {
        if (forward) ordered.firstOrNull() else ordered.lastOrNull()
    } else {
        ordered.getOrNull(if (forward) index + 1 else index - 1)
    }
}

/** Adds an action that always reports success; the edit's own result is shown like a gesture's. */
internal fun MutableList<CustomAccessibilityAction>.action(
    label: String,
    run: () -> Unit,
) {
    add(
        CustomAccessibilityAction(label) {
            run()
            true
        },
    )
}

/** How far an accessibility nudge moves an item: the same finger-sized step a drag snaps within. */
internal val NUDGE_DP: Dp = 10.dp

fun clockLabel(ms: Long): String {
    val totalSeconds = ms / 1000
    val tenths = (ms % 1000) / 100
    return if (ms < 60_000L) "%d.%ds".format(totalSeconds, tenths) else "%d:%02d".format(totalSeconds / 60, totalSeconds % 60)
}
