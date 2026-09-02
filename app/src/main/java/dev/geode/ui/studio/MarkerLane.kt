package dev.geode.ui.studio

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import dev.geode.editor.Marker
import dev.geode.editor.MarkerId
import dev.geode.editor.MarkerSet
import dev.geode.editor.SnapContext
import dev.geode.editor.SnapMode
import dev.geode.editor.SnapTarget
import kotlin.math.abs

private data class MarkerDrag(
    val id: MarkerId,
    val originMs: Long,
    var deltaMs: Long = 0L,
)

/**
 * Flags for every marker. A tap on empty lane adds one at that time, a tap on a flag selects it,
 * a drag moves it (snapping to the playhead and clip edges but never to other markers).
 */
@Composable
fun MarkerLane(
    markers: MarkerSet,
    scale: TimelineScale,
    selected: MarkerId?,
    snapContext: SnapContext,
    onSelect: (MarkerId?) -> Unit,
    onAdd: (Long) -> Unit,
    onMove: (MarkerId, Long, SnapMode, SnapContext) -> Unit,
    modifier: Modifier = Modifier,
) {
    val density = LocalDensity.current.density
    val hitPx = HIT_DP * density
    var drag by remember { mutableStateOf<MarkerDrag?>(null) }
    val outline = MaterialTheme.colorScheme.onSurface

    fun markerAt(x: Float): Marker? =
        markers.markers.minByOrNull { abs(scale.xOf(it.atMs) - x) }?.takeIf {
            abs(scale.xOf(it.atMs) - x) <=
                hitPx
        }

    Canvas(
        modifier
            .pointerInput(markers, scale) {
                detectTapGestures { offset ->
                    val hit = markerAt(offset.x)
                    if (hit == null) onAdd(scale.msOf(offset.x)) else onSelect(hit.id)
                }
            }.pointerInput(markers, scale) {
                detectDragGestures(
                    onDragStart = { offset ->
                        val hit = markerAt(offset.x) ?: return@detectDragGestures
                        onSelect(hit.id)
                        drag = MarkerDrag(hit.id, hit.atMs)
                    },
                    onDrag = { change, amount ->
                        change.consume()
                        drag?.let { it.deltaMs += (amount.x / scale.pxPerMs).toLong() }
                        drag = drag?.copy()
                    },
                    onDragCancel = { drag = null },
                    onDragEnd = {
                        val active = drag ?: return@detectDragGestures
                        drag = null
                        onMove(
                            active.id,
                            (active.originMs + active.deltaMs).coerceAtLeast(0L),
                            SnapMode.Magnetic(setOf(SnapTarget.PLAYHEAD, SnapTarget.CLIP_EDGES), scale.msOf(SNAP_DP, density)),
                            snapContext,
                        )
                    },
                )
            },
    ) {
        for (marker in markers.markers) {
            val active = drag?.takeIf { it.id == marker.id }
            val atMs = if (active == null) marker.atMs else (active.originMs + active.deltaMs).coerceAtLeast(0L)
            drawFlag(scale.xOf(atMs), Color(marker.colour.argb), marker.id == selected, outline, density)
        }
    }
}

private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawFlag(
    x: Float,
    color: Color,
    selected: Boolean,
    outline: Color,
    density: Float,
) {
    val flag =
        Path().apply {
            moveTo(x, 0f)
            lineTo(x + 9f * density, 5f * density)
            lineTo(x, 10f * density)
            close()
        }
    drawPath(flag, color)
    drawLine(color, Offset(x, 0f), Offset(x, size.height), strokeWidth = if (selected) 3f else 1.5f)
    if (selected) {
        drawPath(
            flag,
            outline,
            style =
                androidx.compose.ui.graphics.drawscope
                    .Stroke(1.5f * density),
        )
    }
}

private const val HIT_DP = 12f
private val SNAP_DP = 10.dp
