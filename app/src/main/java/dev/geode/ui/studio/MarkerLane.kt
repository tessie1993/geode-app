package dev.geode.ui.studio

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.geode.R
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
 * a drag moves it (snapping to the playhead and clip edges but never to other markers). TalkBack
 * gets select, add-at-playhead and nudge as custom actions on the lane.
 *
 * [snapContext] is a provider so the playhead is read when a gesture ends, not every time it moves.
 */
@Composable
fun MarkerLane(
    markers: MarkerSet,
    scale: TimelineScale,
    selected: MarkerId?,
    snapContext: () -> SnapContext,
    onSelect: (MarkerId?) -> Unit,
    onAdd: (Long) -> Unit,
    onMove: (MarkerId, Long, SnapMode, SnapContext) -> Unit,
    modifier: Modifier = Modifier,
) {
    val density = LocalDensity.current.density
    val hitPx = HIT_DP * density
    var drag by remember { mutableStateOf<MarkerDrag?>(null) }
    val outline = MaterialTheme.colorScheme.onSurface
    // One flag path, rewound per marker, rather than a new Path per marker per frame.
    val flag = remember { Path() }
    // The gesture coroutine outlives a change of clips; read the latest provider.
    val currentSnapContext by rememberUpdatedState(snapContext)
    val description = pluralStringResource(R.plurals.studio_a11y_marker_lane, markers.markers.size, markers.markers.size)
    val selectedMarker = selected?.let(markers::marker)
    val markerLabel = stringResource(R.string.studio_a11y_marker)
    val selectedState =
        selectedMarker?.let {
            stringResource(R.string.studio_a11y_selected_at, it.name.ifBlank { markerLabel }, clockLabel(it.atMs))
        }
    val a11yActions =
        markerLaneActions(
            labels = laneActionLabels(),
            addLabel = stringResource(R.string.studio_a11y_add_marker_at_playhead),
            markers = markers,
            selected = selectedMarker,
            nudgeMs = scale.msOf(NUDGE_DP, density),
            snapContext = { currentSnapContext() },
            onSelect = onSelect,
            onAdd = onAdd,
            onMove = onMove,
        )

    fun markerAt(x: Float): Marker? =
        markers.markers.minByOrNull { abs(scale.xOf(it.atMs) - x) }?.takeIf {
            abs(scale.xOf(it.atMs) - x) <=
                hitPx
        }

    Canvas(
        modifier
            .semantics {
                contentDescription = description
                selectedState?.let { stateDescription = it }
                customActions = a11yActions
            }.pointerInput(markers, scale) {
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
                            currentSnapContext(),
                        )
                    },
                )
            },
    ) {
        for (marker in markers.markers) {
            val active = drag?.takeIf { it.id == marker.id }
            val atMs = if (active == null) marker.atMs else (active.originMs + active.deltaMs).coerceAtLeast(0L)
            drawFlag(flag, scale.xOf(atMs), Color(marker.colour.argb), marker.id == selected, outline, density)
        }
    }
}

/**
 * The lane's TalkBack actions: step the selection, add a marker at the playhead, and nudge the
 * selected marker. A nudge is exact (no snapping): its whole point is a small, precise move.
 */
private fun markerLaneActions(
    labels: LaneActionLabels,
    addLabel: String,
    markers: MarkerSet,
    selected: Marker?,
    nudgeMs: Long,
    snapContext: () -> SnapContext,
    onSelect: (MarkerId?) -> Unit,
    onAdd: (Long) -> Unit,
    onMove: (MarkerId, Long, SnapMode, SnapContext) -> Unit,
): List<CustomAccessibilityAction> =
    buildList {
        markers.markers.stepFrom(selected?.id, forward = true, idOf = Marker::id, timeOf = Marker::atMs)?.let { next ->
            action(labels.next) { onSelect(next.id) }
        }
        markers.markers.stepFrom(selected?.id, forward = false, idOf = Marker::id, timeOf = Marker::atMs)?.let { previous ->
            action(labels.previous) { onSelect(previous.id) }
        }
        action(addLabel) { onAdd(snapContext().playheadMs) }
        if (selected == null) return@buildList
        if (selected.atMs > 0L) {
            action(labels.nudgeEarlier) {
                onMove(selected.id, (selected.atMs - nudgeMs).coerceAtLeast(0L), SnapMode.Free, snapContext())
            }
        }
        action(labels.nudgeLater) { onMove(selected.id, selected.atMs + nudgeMs, SnapMode.Free, snapContext()) }
    }

private fun DrawScope.drawFlag(
    flag: Path,
    x: Float,
    color: Color,
    selected: Boolean,
    outline: Color,
    density: Float,
) {
    flag.rewind()
    flag.moveTo(x, 0f)
    flag.lineTo(x + 9f * density, 5f * density)
    flag.lineTo(x, 10f * density)
    flag.close()
    drawPath(flag, color)
    drawLine(color, Offset(x, 0f), Offset(x, size.height), strokeWidth = if (selected) 3f else 1.5f)
    if (selected) drawPath(flag, outline, style = Stroke(1.5f * density))
}

private const val HIT_DP = 12f
private val SNAP_DP = 10.dp
