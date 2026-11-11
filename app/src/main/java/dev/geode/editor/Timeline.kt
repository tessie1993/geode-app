package dev.geode.editor

import dev.geode.export.ClipEdit
import kotlin.math.abs

/*
 * Editor timeline — Product Spec §9.
 *
 * Every type here is immutable. Unchanged lanes and clips are reused by reference, so a copy
 * costs a handful of list nodes.
 */

/** Snap radius used when the UI has not derived one from the current zoom level. */
const val DEFAULT_SNAP_TOLERANCE_MS: Long = 60L

/**
 * Shortest clip an operation will produce: roughly two frames at 60 fps. Below this a clip cannot
 * be grabbed with a fingertip, so trims refuse rather than leave slivers.
 */
const val MIN_CLIP_DURATION_MS: Long = 33L

@JvmInline
value class LaneId(
    val value: String,
)

@JvmInline
value class ClipId(
    val value: String,
)

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

/**
 * A visual clip that cuts between styles on the beat: the next of [sceneIds] every [everyBeats]
 * beats of the audio lane, wrapping round. The beat comes from the sound engine's rhythm while the
 * clip renders, not from markers, so it follows the song the clip is placed over.
 */
data class BeatCut(
    val sceneIds: List<String>,
    val everyBeats: Int = DEFAULT_BEATS_PER_CUT,
) {
    companion object {
        const val DEFAULT_BEATS_PER_CUT: Int = 4
    }
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

    /**
     * A generated visual. The three visual clip kinds of a Studio project are all this one type, so
     * every lane, strip and render path that knows a scene clip keeps working when a new kind lands:
     *
     * - a **look**: [sceneId] with its [presetId] or [milkPath], animated by the keyframe sheet;
     * - a **take**: [takeId] names a recorded performance in the project folder (`takes/<id>.json`).
     *   Its events replace the look as the clip plays: the take's time is the clip's source time
     *   ([Clip.sourceInMs] onwards), which is track time from where the recording started, so a take
     *   that sits under its audio stays in step with the song. [sceneId] is the take's first style,
     *   the fallback a renderer that cannot read takes draws;
     * - a **beat-cut sequence**: [beatCut] steps through its styles on the beat, starting at [sceneId].
     */
    data class Scene(
        val sceneId: String,
        val presetId: String? = null,
        val milkPath: String? = null,
        val takeId: String? = null,
        val beatCut: BeatCut? = null,
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
    val transition: ClipTransition? = null,
) {
    val endMs: Long get() = startMs + durationMs

    val hasBoundedSource: Boolean get() = sourceDurationMs > 0L

    fun overlaps(other: Clip): Boolean = startMs < other.endMs && other.startMs < endMs

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
}

data class Lane(
    val id: LaneId,
    val kind: LaneKind,
    val name: String = "",
    val clips: List<Clip> = emptyList(),
    val muted: Boolean = false,
    val locked: Boolean = false,
) {
    fun clip(clipId: ClipId): Clip? = clips.firstOrNull { it.id == clipId }

    /** Clips are kept in start order; every operation rebuilds a lane through here. */
    fun withClips(next: List<Clip>): Lane = copy(clips = next.sortedBy { it.startMs })
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

data class Timeline(
    val lanes: List<Lane> = emptyList(),
    val durationMs: Long = 0L,
) {
    fun lane(laneId: LaneId): Lane? = lanes.firstOrNull { it.id == laneId }

    fun laneOf(clipId: ClipId): Lane? = lanes.firstOrNull { lane -> lane.clips.any { it.id == clipId } }

    fun clip(clipId: ClipId): Clip? = laneOf(clipId)?.clip(clipId)
}

/** Where the editor's three documents meet. Only this type knows they have to move together. */
data class EditorProject(
    val timeline: Timeline = Timeline(),
    val markers: MarkerSet = MarkerSet(),
    val keyframes: KeyframeSheet = KeyframeSheet(),
)

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
