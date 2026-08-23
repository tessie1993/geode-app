package dev.geode.editor

import dev.geode.export.ClipEdit
import kotlin.math.abs

/*
 * Editor timeline — Product Spec §9.
 *
 * Every type here is immutable and every operation is a pure function returning a new Timeline,
 * because undo in this editor is "keep the previous value", not "replay an inverse operation".
 * Unchanged lanes and clips are reused by reference, so a copy costs a handful of list nodes.
 *
 * Ids for anything an operation creates (a split tail, a duplicate) are passed in by the caller.
 * That keeps the operations deterministic and testable, and lets the undo stack keep re-doing an
 * edit that lands on the same ids it did the first time.
 */

/** Snap radius used when the UI has not derived one from the current zoom level. */
const val DEFAULT_SNAP_TOLERANCE_MS: Long = 60L

/**
 * Shortest clip an operation will produce: roughly two frames at 60 fps. Below this a clip cannot
 * be grabbed with a fingertip, so trims, splits and overwrites refuse rather than leave slivers.
 */
const val MIN_CLIP_DURATION_MS: Long = 33L

@JvmInline
value class LaneId(val value: String)

@JvmInline
value class ClipId(val value: String)

/**
 * The lane families of the editor. A lane only accepts clips whose content belongs to it, which is
 * what makes "video dropped onto the text lane" unrepresentable rather than merely discouraged.
 */
sealed interface LaneKind {
    /** Generated visuals: a scene or preset rendered by the engine. */
    data object Visual : LaneKind

    /** Footage: video files and stills, which frame the same way and trim the same way. */
    data object Media : LaneKind

    data object Text : LaneKind

    data object Overlay : LaneKind

    data object Audio : LaneKind
}

enum class OverlayBlend {
    NORMAL,
    ADD,
    SCREEN,
    MULTIPLY,
}

/** What a clip actually plays. Each variant declares the one lane kind it can live on. */
sealed interface ClipContent {
    val laneKind: LaneKind

    data class Scene(
        val sceneId: String,
        val presetId: String? = null,
        val milkPath: String? = null,
    ) : ClipContent {
        override val laneKind: LaneKind = LaneKind.Visual
    }

    /**
     * A video file. [edit] carries the existing export-side look (grade, speed, rotation, ratio);
     * the in/out points live on the [Clip] instead, so trimming works the same on every lane.
     */
    data class Video(
        val uri: String,
        val edit: ClipEdit = ClipEdit(),
    ) : ClipContent {
        override val laneKind: LaneKind = LaneKind.Media
    }

    data class Still(
        val uri: String,
        val kenBurns: Float = 0f,
    ) : ClipContent {
        override val laneKind: LaneKind = LaneKind.Media
    }

    data class Text(
        val text: String,
        val styleId: String? = null,
    ) : ClipContent {
        override val laneKind: LaneKind = LaneKind.Text
    }

    data class Overlay(
        val uri: String,
        val blend: OverlayBlend = OverlayBlend.SCREEN,
        val opacity: Float = 1f,
    ) : ClipContent {
        override val laneKind: LaneKind = LaneKind.Overlay
    }

    data class Audio(
        val uri: String,
        val gainDb: Float = 0f,
    ) : ClipContent {
        override val laneKind: LaneKind = LaneKind.Audio
    }
}

/**
 * One block on a lane.
 *
 * [sourceInMs] is the offset into the underlying media; [sourceDurationMs] is how much material
 * that media has. Synthesised content (scenes, text) has no intrinsic length, so it stores 0 and
 * may be stretched freely — that is the one meaning of 0 here.
 */
data class Clip(
    val id: ClipId,
    val content: ClipContent,
    val startMs: Long,
    val durationMs: Long,
    val sourceInMs: Long = 0L,
    val sourceDurationMs: Long = 0L,
    val label: String = "",
    val enabled: Boolean = true,
) {
    val endMs: Long get() = startMs + durationMs

    val hasBoundedSource: Boolean get() = sourceDurationMs > 0L

    fun spans(atMs: Long): Boolean = atMs >= startMs && atMs < endMs

    fun overlaps(other: Clip): Boolean = startMs < other.endMs && other.startMs < endMs

    fun shiftedBy(deltaMs: Long): Clip = copy(startMs = (startMs + deltaMs).coerceAtLeast(0L))

    /** Where in the source the playhead is sitting, for scrubbing a trimmed media clip. */
    fun sourceAt(timelineMs: Long): Long = sourceInMs + (timelineMs - startMs).coerceIn(0L, durationMs)

    /**
     * Move the head to [newStartMs], carrying the source in-point with it so the picture does not
     * slip. Null when the remainder would be too short to keep.
     */
    fun trimmedToStart(newStartMs: Long): Clip? {
        val floor = if (hasBoundedSource) (startMs - sourceInMs).coerceAtLeast(0L) else 0L
        val start = newStartMs.coerceIn(floor, endMs)
        val duration = endMs - start
        return if (duration < MIN_CLIP_DURATION_MS) {
            null
        } else {
            copy(
                startMs = start,
                durationMs = duration,
                sourceInMs = (sourceInMs + (start - startMs)).coerceAtLeast(0L),
            )
        }
    }

    /** Move the tail to [newEndMs], never past the end of the available source material. */
    fun trimmedToEnd(newEndMs: Long): Clip? {
        val ceiling =
            if (hasBoundedSource) {
                (startMs + (sourceDurationMs - sourceInMs)).coerceAtLeast(startMs)
            } else {
                Long.MAX_VALUE
            }
        val duration = newEndMs.coerceIn(startMs, ceiling) - startMs
        return if (duration < MIN_CLIP_DURATION_MS) null else copy(durationMs = duration)
    }

    /** Split at [atMs], the tail taking [tailId]. Null when [atMs] is not usably inside the clip. */
    fun splitAt(
        atMs: Long,
        tailId: ClipId,
    ): ClipSplit? {
        val headMs = atMs - startMs
        val tailMs = endMs - atMs
        if (headMs < MIN_CLIP_DURATION_MS || tailMs < MIN_CLIP_DURATION_MS) return null
        return ClipSplit(
            head = copy(durationMs = headMs),
            tail =
                copy(
                    id = tailId,
                    startMs = atMs,
                    durationMs = tailMs,
                    sourceInMs = sourceInMs + headMs,
                ),
        )
    }

    /**
     * What is left of this clip once the span [fromMs, untilMs) is taken by something else.
     * Null means the span swallowed it whole.
     */
    fun carvedOut(
        fromMs: Long,
        untilMs: Long,
    ): Clip? =
        when {
            endMs <= fromMs || startMs >= untilMs -> this
            startMs >= fromMs && endMs <= untilMs -> null
            startMs < fromMs -> trimmedToEnd(fromMs)
            else -> trimmedToStart(untilMs)
        }
}

data class ClipSplit(
    val head: Clip,
    val tail: Clip,
)

data class Lane(
    val id: LaneId,
    val kind: LaneKind,
    val name: String = "",
    val clips: List<Clip> = emptyList(),
    val muted: Boolean = false,
    val locked: Boolean = false,
) {
    fun clipAt(atMs: Long): Clip? = clips.firstOrNull { it.spans(atMs) }

    fun clip(clipId: ClipId): Clip? = clips.firstOrNull { it.id == clipId }

    /** Clips are kept in start order; every operation rebuilds a lane through here. */
    fun withClips(next: List<Clip>): Lane = copy(clips = next.sortedBy { it.startMs })

    fun edgesMs(): List<Long> = clips.flatMap { listOf(it.startMs, it.endMs) }

    /** Shift everything that starts at or after the cut. Straddling clips are left alone — see [Timeline.ripple]. */
    fun rippled(shift: RippleShift): Lane = withClips(clips.map { if (it.startMs >= shift.fromMs) it.shiftedBy(shift.deltaMs) else it })
}

/**
 * How far time moved and from where, so a ripple edit can be applied to markers and keyframes as
 * well as clips. [deltaMs] is negative when time was removed.
 *
 * [scope] and [movedClips] record how wide the ripple was, which is what lets the markers and the
 * clip-scoped keyframe tracks work out whether it was theirs to follow.
 */
data class RippleShift(
    val fromMs: Long,
    val deltaMs: Long,
    val scope: RippleScope = RippleScope.ALL_LANES,
    val movedClips: Set<ClipId> = emptySet(),
)

/** Whether a ripple pulls one lane tight or the whole programme. */
enum class RippleScope {
    LANE,
    ALL_LANES,
}

enum class ClipEdge {
    START,
    END,
}

/** What to do when an incoming clip lands on top of one that is already there. */
enum class OverlapPolicy {
    REJECT,
    OVERWRITE,
    RIPPLE,
}

sealed interface EditResult {
    data class Applied(
        val timeline: Timeline,
        val affected: List<ClipId> = emptyList(),
        val ripple: RippleShift? = null,
    ) : EditResult

    data class Rejected(val error: EditError) : EditResult
}

/** Expected refusals. These are values, not exceptions: the UI shows them, it does not crash on them. */
sealed interface EditError {
    data class LaneNotFound(val laneId: LaneId) : EditError

    data class ClipNotFound(val clipId: ClipId) : EditError

    data class LaneLocked(val laneId: LaneId) : EditError

    data class WrongLaneKind(
        val laneId: LaneId,
        val laneKind: LaneKind,
        val contentKind: LaneKind,
    ) : EditError

    data class Overlaps(val clipId: ClipId) : EditError

    /** The edit would have to cut an existing clip in two, and only the caller can name the new half. */
    data class NeedsSplit(val clipId: ClipId) : EditError

    data object TooShort : EditError

    data object OutsideClip : EditError
}

data class Timeline(
    val lanes: List<Lane> = emptyList(),
    val durationMs: Long = 0L,
) {
    fun lane(laneId: LaneId): Lane? = lanes.firstOrNull { it.id == laneId }

    fun laneOf(clipId: ClipId): Lane? = lanes.firstOrNull { lane -> lane.clips.any { it.id == clipId } }

    fun clip(clipId: ClipId): Clip? = laneOf(clipId)?.clip(clipId)

    /** Every clip boundary in the programme, for edge snapping. [exclude] drops the clip being dragged. */
    fun edgesMs(exclude: ClipId? = null): List<Long> =
        lanes
            .asSequence()
            .flatMap { it.clips.asSequence() }
            .filter { it.id != exclude }
            .flatMap { sequenceOf(it.startMs, it.endMs) }
            .distinct()
            .sorted()
            .toList()

    fun addClip(
        laneId: LaneId,
        clip: Clip,
        policy: OverlapPolicy = OverlapPolicy.REJECT,
    ): EditResult {
        val lane = lane(laneId)
        return when {
            lane == null -> EditResult.Rejected(EditError.LaneNotFound(laneId))
            lane.locked -> EditResult.Rejected(EditError.LaneLocked(laneId))
            lane.kind != clip.content.laneKind ->
                EditResult.Rejected(EditError.WrongLaneKind(laneId, lane.kind, clip.content.laneKind))
            clip.durationMs < MIN_CLIP_DURATION_MS -> EditResult.Rejected(EditError.TooShort)
            else -> lane.place(clip, policy).asEditResult(listOf(clip.id))
        }
    }

    /** Move a clip along its lane, or to another lane of the same kind. */
    fun moveClip(
        clipId: ClipId,
        toStartMs: Long,
        toLaneId: LaneId? = null,
        policy: OverlapPolicy = OverlapPolicy.REJECT,
    ): EditResult {
        val from = laneOf(clipId)
        val clip = from?.clip(clipId)
        val target = if (toLaneId == null) from else lane(toLaneId)
        val moved = clip?.copy(startMs = toStartMs.coerceAtLeast(0L))
        return when {
            from == null || clip == null || moved == null ->
                EditResult.Rejected(EditError.ClipNotFound(clipId))
            target == null -> EditResult.Rejected(EditError.LaneNotFound(toLaneId ?: from.id))
            from.locked -> EditResult.Rejected(EditError.LaneLocked(from.id))
            target.locked -> EditResult.Rejected(EditError.LaneLocked(target.id))
            target.kind != clip.content.laneKind ->
                EditResult.Rejected(EditError.WrongLaneKind(target.id, target.kind, clip.content.laneKind))
            target.id == from.id -> target.place(moved, policy).asEditResult(listOf(clipId))
            else -> moveAcross(from, target, moved, policy)
        }
    }

    /**
     * Drag one edge of a clip. A trim stops dead at the neighbouring clip rather than refusing,
     * which is how a trim feels right under a finger; it refuses only if nothing usable is left.
     */
    fun trimClip(
        clipId: ClipId,
        edge: ClipEdge,
        toMs: Long,
    ): EditResult {
        val lane = laneOf(clipId)
        val clip = lane?.clip(clipId)
        return when {
            lane == null || clip == null -> EditResult.Rejected(EditError.ClipNotFound(clipId))
            lane.locked -> EditResult.Rejected(EditError.LaneLocked(lane.id))
            else -> lane.trim(clip, edge, toMs).asEditResult(listOf(clipId))
        }
    }

    fun splitClip(
        clipId: ClipId,
        atMs: Long,
        tailId: ClipId,
    ): EditResult {
        val lane = laneOf(clipId)
        val clip = lane?.clip(clipId)
        val halves = clip?.splitAt(atMs, tailId)
        return when {
            lane == null || clip == null -> EditResult.Rejected(EditError.ClipNotFound(clipId))
            lane.locked -> EditResult.Rejected(EditError.LaneLocked(lane.id))
            halves == null -> EditResult.Rejected(EditError.OutsideClip)
            else ->
                EditResult.Applied(
                    timeline = replaceLane(lane.withClips(lane.clips - clip + halves.head + halves.tail)),
                    affected = listOf(clipId, tailId),
                )
        }
    }

    /** Copy a clip, landing it after the original unless [atMs] says otherwise. */
    fun duplicateClip(
        clipId: ClipId,
        newId: ClipId,
        atMs: Long? = null,
        policy: OverlapPolicy = OverlapPolicy.RIPPLE,
    ): EditResult {
        val lane = laneOf(clipId)
        val clip = lane?.clip(clipId)
        val duplicate = clip?.copy(id = newId, startMs = (atMs ?: clip.endMs).coerceAtLeast(0L))
        return when {
            lane == null || duplicate == null -> EditResult.Rejected(EditError.ClipNotFound(clipId))
            lane.locked -> EditResult.Rejected(EditError.LaneLocked(lane.id))
            else -> lane.place(duplicate, policy).asEditResult(listOf(newId))
        }
    }

    /** Lift a clip out and leave the hole where it was. */
    fun deleteClip(clipId: ClipId): EditResult {
        val lane = laneOf(clipId)
        val clip = lane?.clip(clipId)
        return when {
            lane == null || clip == null -> EditResult.Rejected(EditError.ClipNotFound(clipId))
            lane.locked -> EditResult.Rejected(EditError.LaneLocked(lane.id))
            else ->
                EditResult.Applied(
                    timeline = replaceLane(lane.withClips(lane.clips - clip)),
                    affected = listOf(clipId),
                )
        }
    }

    /**
     * Delete a clip and close the gap behind it. The returned [EditResult.Applied.ripple] must be
     * handed to the markers and keyframe tracks too — [EditorProject.apply] does that for you.
     */
    fun rippleDeleteClip(
        clipId: ClipId,
        scope: RippleScope = RippleScope.LANE,
    ): EditResult {
        val lane = laneOf(clipId)
        val clip = lane?.clip(clipId)
        return when {
            lane == null || clip == null -> EditResult.Rejected(EditError.ClipNotFound(clipId))
            lane.locked -> EditResult.Rejected(EditError.LaneLocked(lane.id))
            else -> {
                val emptied = replaceLane(lane.withClips(lane.clips - clip))
                val scoped =
                    when (scope) {
                        RippleScope.LANE -> setOf(lane.id)
                        RippleScope.ALL_LANES -> lanes.map { it.id }.toSet()
                    }
                val shift =
                    RippleShift(
                        fromMs = clip.endMs,
                        deltaMs = -clip.durationMs,
                        scope = scope,
                        movedClips = emptied.clipIdsAfter(clip.endMs, scoped),
                    )
                EditResult.Applied(
                    timeline = emptied.ripple(shift, scoped),
                    affected = listOf(clipId),
                    ripple = shift,
                )
            }
        }
    }

    /**
     * Slide everything that starts at or after [RippleShift.fromMs].
     *
     * A clip straddling the cut is left where it is: there is no lossless way to shorten it, and
     * silently trimming someone's clip is worse than leaving one visible thing to fix by hand.
     * Locked lanes never move.
     */
    fun ripple(
        shift: RippleShift,
        laneIds: Set<LaneId> = lanes.map { it.id }.toSet(),
    ): Timeline = copy(lanes = lanes.map { if (it.id in laneIds && !it.locked) it.rippled(shift) else it })

    private fun replaceLane(lane: Lane): Timeline = copy(lanes = lanes.map { if (it.id == lane.id) lane else it })

    /** The clips a ripple from [fromMs] would carry, so their keyframes can be carried with them. */
    private fun clipIdsAfter(
        fromMs: Long,
        laneIds: Set<LaneId>,
    ): Set<ClipId> =
        lanes
            .filter { it.id in laneIds && !it.locked }
            .flatMap { lane -> lane.clips.filter { it.startMs >= fromMs } }
            .mapTo(mutableSetOf()) { it.id }

    private fun moveAcross(
        from: Lane,
        target: Lane,
        clip: Clip,
        policy: OverlapPolicy,
    ): EditResult {
        val emptied = from.withClips(from.clips.filterNot { it.id == clip.id })
        return when (val outcome = target.place(clip, policy)) {
            is PlaceOutcome.Blocked -> EditResult.Rejected(outcome.error)
            is PlaceOutcome.Placed ->
                EditResult.Applied(
                    timeline = replaceLane(emptied).replaceLane(outcome.lane),
                    affected = listOf(clip.id),
                )
        }
    }

    private fun PlaceOutcome.asEditResult(affected: List<ClipId>): EditResult =
        when (this) {
            is PlaceOutcome.Placed -> EditResult.Applied(replaceLane(lane), affected)
            is PlaceOutcome.Blocked -> EditResult.Rejected(error)
        }
}

/** Where the editor's three documents meet. Only this type knows they have to move together. */
data class EditorProject(
    val timeline: Timeline = Timeline(),
    val markers: MarkerSet = MarkerSet(),
    val keyframes: KeyframeSheet = KeyframeSheet(),
) {
    /**
     * Fold an edit back in.
     *
     * A ripple has to carry the keyframes that were written against the clips it moved, or the
     * animation drifts off the picture. Markers are different: they are marks against the music,
     * and the music only moves when the whole programme does — closing a gap on one visual lane
     * has to leave them exactly where the user put them. Keeping both rules here means no caller
     * can get either one wrong.
     */
    fun apply(result: EditResult): EditorProject =
        when (result) {
            is EditResult.Rejected -> this
            is EditResult.Applied -> {
                val shift = result.ripple
                copy(
                    timeline = result.timeline,
                    markers =
                        if (shift != null && shift.scope == RippleScope.ALL_LANES) markers.rippled(shift) else markers,
                    keyframes = shift?.let(keyframes::rippled) ?: keyframes,
                )
            }
        }

    fun snapContext(
        playheadMs: Long,
        excludeClip: ClipId? = null,
    ): SnapContext =
        SnapContext(
            playheadMs = playheadMs,
            markerMs = markers.timesMs,
            clipEdgeMs = timeline.edgesMs(excludeClip),
        )
}

/**
 * What the editor is allowed to snap to.
 *
 * There is deliberately no beat or tempo entry: clips and keyframes are placed against the
 * waveform and against the marks the user made themselves. Declaration order is the tie-break
 * order when two candidates are equally close — the playhead is the most likely target, a clip
 * edge the least.
 */
enum class SnapTarget {
    PLAYHEAD,
    MARKERS,
    CLIP_EDGES,
}

sealed interface SnapMode {
    /** No magnetism at all: the value lands exactly where it was dropped. */
    data object Free : SnapMode

    data class Magnetic(
        val targets: Set<SnapTarget>,
        val toleranceMs: Long = DEFAULT_SNAP_TOLERANCE_MS,
    ) : SnapMode
}

data class SnapContext(
    val playheadMs: Long = 0L,
    val markerMs: List<Long> = emptyList(),
    val clipEdgeMs: List<Long> = emptyList(),
)

/** [target] is null when nothing was near enough and the time passed straight through. */
data class SnapResult(
    val timeMs: Long,
    val target: SnapTarget?,
)

fun SnapMode.resolve(
    timeMs: Long,
    context: SnapContext,
): SnapResult =
    when (this) {
        SnapMode.Free -> SnapResult(timeMs, null)
        is SnapMode.Magnetic -> magnetise(timeMs, context, targets, toleranceMs)
    }

/** Convenience for the common "snap or don't" call where the caller only wants the time back. */
fun SnapMode.snap(
    timeMs: Long,
    context: SnapContext,
): Long = resolve(timeMs, context).timeMs

private fun magnetise(
    timeMs: Long,
    context: SnapContext,
    targets: Set<SnapTarget>,
    toleranceMs: Long,
): SnapResult {
    var bestTime = timeMs
    var bestTarget: SnapTarget? = null
    var bestDistance = Long.MAX_VALUE
    for (target in SnapTarget.entries) {
        val candidate = if (target in targets) context.candidates(target).nearestTo(timeMs) else null
        val distance = if (candidate == null) Long.MAX_VALUE else abs(candidate - timeMs)
        if (candidate != null && distance <= toleranceMs && distance < bestDistance) {
            bestTime = candidate
            bestTarget = target
            bestDistance = distance
        }
    }
    return SnapResult(bestTime, bestTarget)
}

private fun SnapContext.candidates(target: SnapTarget): List<Long> =
    when (target) {
        SnapTarget.PLAYHEAD -> listOf(playheadMs)
        SnapTarget.MARKERS -> markerMs
        SnapTarget.CLIP_EDGES -> clipEdgeMs
    }

private fun List<Long>.nearestTo(timeMs: Long): Long? = minByOrNull { abs(it - timeMs) }

private sealed interface PlaceOutcome {
    data class Placed(val lane: Lane) : PlaceOutcome

    data class Blocked(val error: EditError) : PlaceOutcome
}

private fun Lane.place(
    clip: Clip,
    policy: OverlapPolicy,
): PlaceOutcome {
    val others = clips.filterNot { it.id == clip.id }
    val collisions = others.filter { it.overlaps(clip) }
    return if (collisions.isEmpty()) {
        PlaceOutcome.Placed(withClips(others + clip))
    } else {
        when (policy) {
            OverlapPolicy.REJECT -> PlaceOutcome.Blocked(EditError.Overlaps(collisions.first().id))
            OverlapPolicy.RIPPLE -> rippleIn(others, clip)
            OverlapPolicy.OVERWRITE -> overwriteWith(others, clip, collisions)
        }
    }
}

private fun Lane.rippleIn(
    others: List<Clip>,
    clip: Clip,
): PlaceOutcome {
    val straddling = others.firstOrNull { it.startMs < clip.startMs && it.endMs > clip.startMs }
    return if (straddling != null) {
        PlaceOutcome.Blocked(EditError.NeedsSplit(straddling.id))
    } else {
        val shifted = others.map { if (it.startMs >= clip.startMs) it.shiftedBy(clip.durationMs) else it }
        PlaceOutcome.Placed(withClips(shifted + clip))
    }
}

private fun Lane.overwriteWith(
    others: List<Clip>,
    clip: Clip,
    collisions: List<Clip>,
): PlaceOutcome {
    // A clip that completely contains the incoming one would have to become two clips, and a new
    // half needs an id this function has no business inventing. Hand it back to the caller.
    val enclosing = collisions.firstOrNull { it.startMs < clip.startMs && it.endMs > clip.endMs }
    return if (enclosing != null) {
        PlaceOutcome.Blocked(EditError.NeedsSplit(enclosing.id))
    } else {
        PlaceOutcome.Placed(withClips(others.mapNotNull { it.carvedOut(clip.startMs, clip.endMs) } + clip))
    }
}

private fun Lane.trim(
    clip: Clip,
    edge: ClipEdge,
    toMs: Long,
): PlaceOutcome {
    val trimmed =
        when (edge) {
            ClipEdge.START -> clip.trimmedToStart(toMs.coerceAtLeast(neighbourBefore(clip)?.endMs ?: 0L))
            ClipEdge.END -> clip.trimmedToEnd(toMs.coerceAtMost(neighbourAfter(clip)?.startMs ?: Long.MAX_VALUE))
        }
    return if (trimmed == null) {
        PlaceOutcome.Blocked(EditError.TooShort)
    } else {
        PlaceOutcome.Placed(withClips(clips.map { if (it.id == clip.id) trimmed else it }))
    }
}

private fun Lane.neighbourBefore(clip: Clip): Clip? =
    clips.filter { it.id != clip.id && it.startMs < clip.startMs }.maxByOrNull { it.endMs }

private fun Lane.neighbourAfter(clip: Clip): Clip? =
    clips.filter { it.id != clip.id && it.startMs >= clip.endMs }.minByOrNull { it.startMs }
