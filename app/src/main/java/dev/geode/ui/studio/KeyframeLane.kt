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
import androidx.compose.ui.graphics.Path
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
import dev.geode.editor.AnimatableParams
import dev.geode.editor.Keyframe
import dev.geode.editor.KeyframeId
import dev.geode.editor.KeyframeTrack
import dev.geode.editor.SnapContext
import dev.geode.editor.SnapMode
import dev.geode.editor.SnapTarget
import kotlin.math.abs

private data class KeyDrag(
    val id: KeyframeId,
    val originMs: Long,
    var deltaMs: Long = 0L,
)

/**
 * The keys of one track as diamonds. A tap selects, a tap on empty lane adds a key at that time
 * holding the track's value there, and a drag slides a key with snapping. TalkBack gets select,
 * add-at-playhead and nudge as custom actions on the lane.
 *
 * [snapContext] is a provider so the playhead is read when a gesture ends, not every time it moves.
 */
@Composable
fun KeyframeLane(
    track: KeyframeTrack,
    scale: TimelineScale,
    selected: KeyframeId?,
    snapContext: () -> SnapContext,
    onSelect: (KeyframeId?) -> Unit,
    onAdd: (Long) -> Unit,
    onMove: (KeyframeId, Long, SnapMode, SnapContext) -> Unit,
    modifier: Modifier = Modifier,
) {
    val density = LocalDensity.current.density
    val hitPx = HIT_DP * density
    var drag by remember(track.paramId, track.clipId) { mutableStateOf<KeyDrag?>(null) }
    val fill = MaterialTheme.colorScheme.secondary
    val outline = MaterialTheme.colorScheme.onSurface
    val line = MaterialTheme.colorScheme.onSurfaceVariant
    // One diamond path, rewound per key, rather than a new Path per key per frame.
    val diamond = remember { Path() }
    // The gesture coroutine outlives a change of markers or clips; read the latest provider.
    val currentSnapContext by rememberUpdatedState(snapContext)
    val description =
        pluralStringResource(
            R.plurals.studio_a11y_key_lane,
            track.keys.size,
            paramLabel(AnimatableParams.find(track.paramId)),
            track.keys.size,
        )
    val selectedKey = track.keys.firstOrNull { it.id == selected }
    val keyLabel = stringResource(R.string.studio_a11y_key)
    val selectedState = selectedKey?.let { stringResource(R.string.studio_a11y_selected_at, keyLabel, clockLabel(it.atMs)) }
    val a11yActions =
        keyLaneActions(
            labels = laneActionLabels(),
            addLabel = stringResource(R.string.studio_a11y_add_key_at_playhead),
            track = track,
            selected = selectedKey,
            nudgeMs = scale.msOf(NUDGE_DP, density),
            snapContext = { currentSnapContext() },
            onSelect = onSelect,
            onAdd = onAdd,
            onMove = onMove,
        )

    fun keyAt(x: Float): Keyframe? = track.keys.minByOrNull { abs(scale.xOf(it.atMs) - x) }?.takeIf { abs(scale.xOf(it.atMs) - x) <= hitPx }

    Canvas(
        modifier
            .semantics {
                contentDescription = description
                selectedState?.let { stateDescription = it }
                customActions = a11yActions
            }.pointerInput(track, scale) {
                detectTapGestures { offset ->
                    val hit = keyAt(offset.x)
                    if (hit == null) onAdd(scale.msOf(offset.x)) else onSelect(hit.id)
                }
            }.pointerInput(track, scale) {
                detectDragGestures(
                    onDragStart = { offset ->
                        val hit = keyAt(offset.x) ?: return@detectDragGestures
                        onSelect(hit.id)
                        drag = KeyDrag(hit.id, hit.atMs)
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
                            SnapMode.Magnetic(
                                setOf(SnapTarget.PLAYHEAD, SnapTarget.MARKERS, SnapTarget.CLIP_EDGES),
                                scale.msOf(SNAP_DP, density),
                            ),
                            currentSnapContext(),
                        )
                    },
                )
            },
    ) {
        val midY = size.height / 2
        drawLine(line.copy(alpha = 0.4f), Offset(0f, midY), Offset(size.width, midY), strokeWidth = 1f)
        for (key in track.keys) {
            val active = drag?.takeIf { it.id == key.id }
            val atMs = if (active == null) key.atMs else (active.originMs + active.deltaMs).coerceAtLeast(0L)
            val x = scale.xOf(atMs)
            val r = 6f * density
            diamond.rewind()
            diamond.moveTo(x, midY - r)
            diamond.lineTo(x + r, midY)
            diamond.lineTo(x, midY + r)
            diamond.lineTo(x - r, midY)
            diamond.close()
            drawPath(diamond, fill.copy(alpha = if (track.enabled) 1f else 0.4f))
            if (key.id == selected) drawPath(diamond, outline, style = Stroke(2f * density))
        }
    }
}

/**
 * The lane's TalkBack actions: step the selection, add a key at the playhead, and nudge the
 * selected key. A nudge is exact (no snapping): its whole point is a small, precise move.
 */
private fun keyLaneActions(
    labels: LaneActionLabels,
    addLabel: String,
    track: KeyframeTrack,
    selected: Keyframe?,
    nudgeMs: Long,
    snapContext: () -> SnapContext,
    onSelect: (KeyframeId?) -> Unit,
    onAdd: (Long) -> Unit,
    onMove: (KeyframeId, Long, SnapMode, SnapContext) -> Unit,
): List<CustomAccessibilityAction> =
    buildList {
        track.keys.stepFrom(selected?.id, forward = true, idOf = Keyframe::id, timeOf = Keyframe::atMs)?.let { next ->
            action(labels.next) { onSelect(next.id) }
        }
        track.keys.stepFrom(selected?.id, forward = false, idOf = Keyframe::id, timeOf = Keyframe::atMs)?.let { previous ->
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

private const val HIT_DP = 12f
private val SNAP_DP = 10.dp
