package dev.geode.ui.studio

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.ClipEdge
import dev.geode.editor.ClipId
import dev.geode.editor.EditResult
import dev.geode.editor.Lane
import dev.geode.editor.OverlapPolicy
import dev.geode.editor.SnapContext
import dev.geode.editor.SnapMode
import dev.geode.editor.SnapTarget
import dev.geode.editor.Timeline
import dev.geode.editor.snap
import dev.geode.ui.opaline.creative.CreativeColors

/** What a drag on a clip is doing: sliding it or pulling one of its edges. */
private enum class DragMode {
    MOVE,
    TRIM_START,
    TRIM_END,
}

private data class ClipDrag(
    val clipId: ClipId,
    val mode: DragMode,
    val originStartMs: Long,
    val originEndMs: Long,
    var deltaMs: Long = 0L,
)

/**
 * One lane's clips. A tap selects, a double tap splits at the tap, a drag on the body moves and a
 * drag inside the edge zone trims; the drag is previewed locally and committed as a single edit.
 */
@Composable
fun ClipStrip(
    timeline: Timeline,
    lane: Lane,
    scale: TimelineScale,
    selected: ClipId?,
    snapContext: (ClipId) -> SnapContext,
    onSelect: (ClipId?) -> Unit,
    onSplit: (ClipId, Long) -> Unit,
    onResult: (EditResult) -> Unit,
    modifier: Modifier = Modifier,
) {
    val density = LocalDensity.current.density
    val edgeZonePx = EDGE_ZONE_DP * density
    var drag by remember(lane.id) { mutableStateOf<ClipDrag?>(null) }
    val colors = ClipColors.glass()

    fun clipAt(x: Float): Clip? = lane.clips.firstOrNull { x >= scale.xOf(it.startMs) && x < scale.xOf(it.endMs) }

    fun snapModeFor(): SnapMode =
        SnapMode.Magnetic(setOf(SnapTarget.PLAYHEAD, SnapTarget.MARKERS, SnapTarget.CLIP_EDGES), scale.msOf(SNAP_DP, density))

    Canvas(
        modifier
            .pointerInput(lane, scale) {
                detectTapGestures(
                    onTap = { offset -> onSelect(clipAt(offset.x)?.id) },
                    onDoubleTap = { offset -> clipAt(offset.x)?.let { onSplit(it.id, scale.msOf(offset.x)) } },
                )
            }.pointerInput(lane, scale, timeline) {
                detectDragGestures(
                    onDragStart = { offset ->
                        val clip = clipAt(offset.x) ?: return@detectDragGestures
                        if (lane.locked) return@detectDragGestures
                        val mode =
                            when {
                                offset.x - scale.xOf(clip.startMs) <= edgeZonePx -> DragMode.TRIM_START
                                scale.xOf(clip.endMs) - offset.x <= edgeZonePx -> DragMode.TRIM_END
                                else -> DragMode.MOVE
                            }
                        onSelect(clip.id)
                        drag = ClipDrag(clip.id, mode, clip.startMs, clip.endMs)
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
                        val snap = snapModeFor()
                        val context = snapContext(active.clipId)
                        onResult(
                            when (active.mode) {
                                DragMode.MOVE ->
                                    timeline.moveClip(
                                        active.clipId,
                                        snap.snap((active.originStartMs + active.deltaMs).coerceAtLeast(0L), context),
                                        policy = OverlapPolicy.REJECT,
                                    )
                                DragMode.TRIM_START ->
                                    timeline.trimClip(
                                        active.clipId,
                                        ClipEdge.START,
                                        snap.snap(active.originStartMs + active.deltaMs, context),
                                    )
                                DragMode.TRIM_END ->
                                    timeline.trimClip(active.clipId, ClipEdge.END, snap.snap(active.originEndMs + active.deltaMs, context))
                            },
                        )
                    },
                )
            },
    ) {
        for (clip in lane.clips) {
            val active = drag?.takeIf { it.clipId == clip.id }
            val (startMs, endMs) = previewSpan(clip, active)
            drawClip(clip, scale.xOf(startMs), scale.widthOf(endMs - startMs), clip.id == selected, lane.muted, colors, density)
        }
    }
}

private fun previewSpan(
    clip: Clip,
    drag: ClipDrag?,
): Pair<Long, Long> =
    when (drag?.mode) {
        null -> clip.startMs to clip.endMs
        DragMode.MOVE -> (drag.originStartMs + drag.deltaMs).coerceAtLeast(0L).let { it to it + clip.durationMs }
        DragMode.TRIM_START -> (drag.originStartMs + drag.deltaMs).coerceIn(0L, drag.originEndMs - 1) to drag.originEndMs
        DragMode.TRIM_END -> drag.originStartMs to (drag.originEndMs + drag.deltaMs).coerceAtLeast(drag.originStartMs + 1)
    }

/** A glass pill: a pastel-tinted translucent fill, a top specular sliver and a lighter rim. */
private fun DrawScope.drawClip(
    clip: Clip,
    x: Float,
    width: Float,
    selected: Boolean,
    muted: Boolean,
    colors: ClipColors,
    density: Float,
) {
    val tint = colors.fillFor(clip.content)
    val dim = if (clip.enabled && !muted) 1f else 0.4f
    val top = 4f * density
    val size = Size(width.coerceAtLeast(2f), this.size.height - top * 2)
    val corner = CornerRadius(size.height / 2f)
    val origin = Offset(x, top)
    drawRoundRect(CreativeColors.glassFill.copy(alpha = CreativeColors.glassFill.alpha * dim), origin, size, corner)
    drawRoundRect(tint.copy(alpha = 0.30f * dim), origin, size, corner)
    if (size.height > 8f) {
        drawRoundRect(
            brush = Brush.verticalGradient(listOf(Color.White.copy(alpha = 0.30f * dim), Color.Transparent)),
            topLeft = origin,
            size = Size(size.width, size.height * 0.5f),
            cornerRadius = corner,
        )
    }
    drawRoundRect(colors.outline.copy(alpha = 0.55f * dim), origin, size, corner, style = Stroke(1f * density))
    if (clip.transition != null) {
        drawRoundRect(colors.outline.copy(alpha = 0.6f), origin, Size(TRANSITION_BADGE_DP * density, size.height), corner)
    }
    if (selected) drawRoundRect(CreativeColors.textPrimary, origin, size, corner, style = Stroke(2f * density))
    val label = clip.label.ifBlank { defaultLabel(clip.content) }
    if (width > 24f * density) {
        val paint =
            android.graphics.Paint().apply {
                color = colors.label.copy(alpha = colors.label.alpha * dim).toArgb()
                textSize = 11f * density
                isAntiAlias = true
            }
        drawContext.canvas.nativeCanvas.save()
        drawContext.canvas.nativeCanvas.clipRect(x, top, x + width, top + size.height)
        drawContext.canvas.nativeCanvas.drawText(label, x + 8f * density, top + size.height / 2 + 4f * density, paint)
        drawContext.canvas.nativeCanvas.restore()
    }
}

private fun defaultLabel(content: ClipContent): String =
    when (content) {
        is ClipContent.Scene -> content.sceneId
        is ClipContent.Video -> content.uri.substringAfterLast('/').substringAfterLast(':')
        is ClipContent.Still -> content.uri.substringAfterLast('/').substringAfterLast(':')
        is ClipContent.Text -> content.text
        is ClipContent.Overlay -> content.uri.substringAfterLast('/').substringAfterLast(':')
        is ClipContent.Audio -> content.uri.substringAfterLast('/').substringAfterLast(':')
    }

private class ClipColors(
    val scene: Color,
    val media: Color,
    val textFill: Color,
    val overlay: Color,
    val audio: Color,
    val outline: Color,
    val label: Color,
) {
    fun fillFor(content: ClipContent): Color =
        when (content) {
            is ClipContent.Scene -> scene
            is ClipContent.Video, is ClipContent.Still -> media
            is ClipContent.Text -> textFill
            is ClipContent.Overlay -> overlay
            is ClipContent.Audio -> audio
        }

    companion object {
        /** One pastel from [CreativeColors] per lane kind, so every clip family reads distinctly. */
        fun glass(): ClipColors =
            ClipColors(
                scene = CreativeColors.mint,
                media = CreativeColors.sky,
                textFill = CreativeColors.lavender,
                overlay = CreativeColors.peach,
                audio = CreativeColors.pink,
                outline = CreativeColors.glassRim,
                label = CreativeColors.textPrimary,
            )
    }
}

private const val EDGE_ZONE_DP = 14f
private const val TRANSITION_BADGE_DP = 5f
private val SNAP_DP = 10.dp
