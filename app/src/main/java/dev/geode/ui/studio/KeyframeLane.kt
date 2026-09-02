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
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
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
 * holding the track's value there, and a drag slides a key with snapping.
 */
@Composable
fun KeyframeLane(
    track: KeyframeTrack,
    scale: TimelineScale,
    selected: KeyframeId?,
    snapContext: SnapContext,
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

    fun keyAt(x: Float): Keyframe? = track.keys.minByOrNull { abs(scale.xOf(it.atMs) - x) }?.takeIf { abs(scale.xOf(it.atMs) - x) <= hitPx }

    Canvas(
        modifier
            .pointerInput(track, scale) {
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
                            snapContext,
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
            val diamond =
                Path().apply {
                    moveTo(x, midY - r)
                    lineTo(x + r, midY)
                    lineTo(x, midY + r)
                    lineTo(x - r, midY)
                    close()
                }
            drawPath(diamond, fill.copy(alpha = if (track.enabled) 1f else 0.4f))
            if (key.id == selected) drawPath(diamond, outline, style = Stroke(2f * density))
        }
    }
}

private const val HIT_DP = 12f
private val SNAP_DP = 10.dp
