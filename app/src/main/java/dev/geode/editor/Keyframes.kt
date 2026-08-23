package dev.geode.editor

import dev.geode.data.PerformanceTake
import kotlin.math.abs

/*
 * Keyframes and the curve editor — Product Spec §9.
 *
 * Any parameter can be animated: transform, opacity, colour, scene settings, FX amounts. A track
 * is a list of keys in time order plus the curve leaving each key, and evaluation is a pure
 * function of (track, time) so the same code drives the timeline preview and the export.
 *
 * Keyframes snap to markers or clip edges through the editor's one snapping engine (SnapMode);
 * there is no beat grid to snap to on purpose.
 */

@JvmInline
value class ParamId(val value: String)

@JvmInline
value class KeyframeId(val value: String)

/** The shapes a parameter value can take. The kind is fixed per track — see [KeyframeTrack.kind]. */
enum class ParamKind {
    SCALAR,
    VECTOR2,
    COLOUR,
    TOGGLE,
    CHOICE,
}

sealed interface ParamValue {
    val kind: ParamKind

    /** Opacity, FX amount, rotation — anything that is one number. */
    data class Scalar(val value: Float) : ParamValue {
        override val kind: ParamKind = ParamKind.SCALAR
    }

    /** Position, scale, any two-axis transform channel. */
    data class Vector2(
        val x: Float,
        val y: Float,
    ) : ParamValue {
        override val kind: ParamKind = ParamKind.VECTOR2
    }

    /**
     * Components are kept in whatever space the renderer already works in, and blended straight.
     * Converting to a perceptual space here would double-convert against the engine's own colour
     * handling, which is a worse error than a slightly dark midpoint.
     */
    data class Colour(
        val r: Float,
        val g: Float,
        val b: Float,
        val a: Float = 1f,
    ) : ParamValue {
        override val kind: ParamKind = ParamKind.COLOUR
    }

    data class Toggle(val on: Boolean) : ParamValue {
        override val kind: ParamKind = ParamKind.TOGGLE
    }

    /** A choice out of a fixed list: palette index, blend mode, symmetry count. */
    data class Choice(val index: Int) : ParamValue {
        override val kind: ParamKind = ParamKind.CHOICE
    }
}

/**
 * A cubic bezier with its end points pinned at (0,0) and (1,1) — the curve editor's handles.
 *
 * [eval] solves for the curve parameter at the requested x and then reads y there. The control
 * points bend time as well as value, so sampling y at x directly (a lerp) is wrong for every
 * curve that is not a straight line.
 */
data class BezierCurve(
    val c1x: Float,
    val c1y: Float,
    val c2x: Float,
    val c2y: Float,
) {
    // Polynomial form of the bezier, computed once: B(s) = ((a*s + b)*s + c)*s.
    // The x handles are clamped to [0,1] because a handle outside that range makes the curve
    // non-monotonic in time, which is not a thing a time warp can be.
    private val xC: Float = 3f * c1x.coerceIn(0f, 1f)
    private val xB: Float = 3f * (c2x.coerceIn(0f, 1f) - c1x.coerceIn(0f, 1f)) - xC
    private val xA: Float = 1f - xC - xB
    private val yC: Float = 3f * c1y
    private val yB: Float = 3f * (c2y - c1y) - yC
    private val yA: Float = 1f - yC - yB

    fun eval(x: Float): Float = axisY(solveForX(x.coerceIn(0f, 1f)))

    private fun axisX(s: Float): Float = ((xA * s + xB) * s + xC) * s

    private fun axisY(s: Float): Float = ((yA * s + yB) * s + yC) * s

    private fun slopeX(s: Float): Float = (3f * xA * s + 2f * xB) * s + xC

    private fun solveForX(x: Float): Float {
        var s = x
        repeat(NEWTON_STEPS) {
            val slope = slopeX(s)
            // A near-flat slope would send Newton off into the weeds; bisection picks it up below.
            if (slope > SLOPE_EPSILON) s = (s - (axisX(s) - x) / slope).coerceIn(0f, 1f)
        }
        return if (abs(axisX(s) - x) < SOLVE_EPSILON) s else bisectForX(x)
    }

    private fun bisectForX(x: Float): Float {
        var low = 0f
        var high = 1f
        var s = x
        repeat(BISECT_STEPS) {
            s = (low + high) * 0.5f
            if (axisX(s) < x) low = s else high = s
        }
        return s
    }

    private companion object {
        const val NEWTON_STEPS = 8
        const val BISECT_STEPS = 24
        const val SOLVE_EPSILON = 1e-5f
        const val SLOPE_EPSILON = 1e-6f
    }
}

/** The named presets of the curve editor, each one an ordinary bezier the user can then drag. */
enum class EaseShape(val curve: BezierCurve) {
    IN(BezierCurve(0.42f, 0f, 1f, 1f)),
    OUT(BezierCurve(0f, 0f, 0.58f, 1f)),
    IN_OUT(BezierCurve(0.42f, 0f, 0.58f, 1f)),
    SMOOTH(BezierCurve(0.4f, 0f, 0.2f, 1f)),
}

/** How the value leaves a key on its way to the next one. */
sealed interface Interpolation {
    /** Stay put and jump at the next key: the right behaviour for scene swaps and switches. */
    data object Hold : Interpolation

    data object Linear : Interpolation

    data class Ease(val shape: EaseShape) : Interpolation

    data class Custom(val curve: BezierCurve) : Interpolation
}

/** Eased position between two keys, given the raw 0..1 [progress] between their times. */
fun Interpolation.fractionAt(progress: Float): Float =
    when (this) {
        Interpolation.Hold -> 0f
        Interpolation.Linear -> progress.coerceIn(0f, 1f)
        is Interpolation.Ease -> shape.curve.eval(progress)
        is Interpolation.Custom -> curve.eval(progress)
    }

data class Keyframe(
    val id: KeyframeId,
    val atMs: Long,
    val value: ParamValue,
    val interpolation: Interpolation = Interpolation.Linear,
)

sealed interface KeyframeResult {
    data class Applied(val track: KeyframeTrack) : KeyframeResult

    data class Rejected(val error: KeyframeError) : KeyframeResult
}

sealed interface KeyframeError {
    /** A track animates one shape of value for its whole life; mixing shapes has no meaning. */
    data class KindMismatch(
        val expected: ParamKind,
        val actual: ParamKind,
    ) : KeyframeError

    data class KeyNotFound(val id: KeyframeId) : KeyframeError
}

/**
 * The animation of one parameter.
 *
 * [clipId] scopes the track to a clip (it moves and dies with that clip); a null [clipId] is a
 * programme-level track that runs for the whole piece. Keys are kept in time order and unique by
 * time — two keys at the same instant would make evaluation depend on list order.
 */
data class KeyframeTrack(
    val paramId: ParamId,
    val keys: List<Keyframe> = emptyList(),
    val clipId: ClipId? = null,
    val enabled: Boolean = true,
) {
    /** Null until the first key decides what this track animates. */
    val kind: ParamKind? get() = keys.firstOrNull()?.value?.kind

    val isEmpty: Boolean get() = keys.isEmpty()

    fun key(id: KeyframeId): Keyframe? = keys.firstOrNull { it.id == id }

    /**
     * Value at [timeMs]. Before the first key and after the last one the track holds that key's
     * value: a parameter is never undefined while its track exists.
     */
    fun valueAt(timeMs: Long): ParamValue? {
        val index = keys.indexAtOrBefore(timeMs)
        return when {
            keys.isEmpty() -> null
            index < 0 -> keys.first().value
            index == keys.lastIndex -> keys[index].value
            else -> blendBetween(keys[index], keys[index + 1], timeMs)
        }
    }

    /** Add a key, or replace the one already sitting on that instant. */
    fun withKey(key: Keyframe): KeyframeResult {
        val current = kind
        return if (current != null && current != key.value.kind) {
            KeyframeResult.Rejected(KeyframeError.KindMismatch(current, key.value.kind))
        } else {
            KeyframeResult.Applied(withKeys(keys.filterNot { it.id == key.id || it.atMs == key.atMs } + key))
        }
    }

    /** Drag a key along the timeline, snapping to markers or clip edges if the caller asked for it. */
    fun moveKey(
        id: KeyframeId,
        toMs: Long,
        snap: SnapMode = SnapMode.Free,
        context: SnapContext = SnapContext(),
    ): KeyframeResult {
        val key = key(id)
        val atMs = snap.snap(toMs, context).coerceAtLeast(0L)
        return if (key == null) {
            KeyframeResult.Rejected(KeyframeError.KeyNotFound(id))
        } else {
            val others = keys.filterNot { it.id == id || it.atMs == atMs }
            KeyframeResult.Applied(withKeys(others + key.copy(atMs = atMs)))
        }
    }

    fun withInterpolation(
        id: KeyframeId,
        interpolation: Interpolation,
    ): KeyframeResult =
        if (key(id) == null) {
            KeyframeResult.Rejected(KeyframeError.KeyNotFound(id))
        } else {
            val recurved = keys.map { if (it.id == id) it.copy(interpolation = interpolation) else it }
            KeyframeResult.Applied(withKeys(recurved))
        }

    /** Removing a key that is not there is a no-op, not a failure — the user got what they asked for. */
    fun removeKey(id: KeyframeId): KeyframeTrack = withKeys(keys.filterNot { it.id == id })

    fun shiftedBy(deltaMs: Long): KeyframeTrack = withKeys(keys.map { it.copy(atMs = (it.atMs + deltaMs).coerceAtLeast(0L)) })

    /** Follow a ripple edit: keys after the cut move with the picture they were written against. */
    fun rippled(shift: RippleShift): KeyframeTrack =
        withKeys(
            keys.map {
                if (it.atMs >= shift.fromMs) it.copy(atMs = (it.atMs + shift.deltaMs).coerceAtLeast(0L)) else it
            },
        )

    private fun withKeys(next: List<Keyframe>): KeyframeTrack = copy(keys = next.sortedBy { it.atMs })
}

/** Every animated parameter in the project, programme-level and clip-scoped alike. */
data class KeyframeSheet(
    val tracks: List<KeyframeTrack> = emptyList(),
) {
    fun track(
        paramId: ParamId,
        clipId: ClipId? = null,
    ): KeyframeTrack? = tracks.firstOrNull { it.paramId == paramId && it.clipId == clipId }

    fun tracksFor(clipId: ClipId?): List<KeyframeTrack> = tracks.filter { it.clipId == clipId }

    /** A track is identified by its parameter and its scope; adding one replaces that pair. */
    fun withTrack(track: KeyframeTrack): KeyframeSheet =
        copy(tracks = tracks.filterNot { it.paramId == track.paramId && it.clipId == track.clipId } + track)

    fun withTracks(incoming: List<KeyframeTrack>): KeyframeSheet = incoming.fold(this) { sheet, track -> sheet.withTrack(track) }

    fun removeTrack(
        paramId: ParamId,
        clipId: ClipId? = null,
    ): KeyframeSheet = copy(tracks = tracks.filterNot { it.paramId == paramId && it.clipId == clipId })

    fun removeTracksFor(clipId: ClipId): KeyframeSheet = copy(tracks = tracks.filterNot { it.clipId == clipId })

    /**
     * Everything animated at [timeMs]. Programme-level tracks are read first so a clip-scoped
     * track wins for the clip it belongs to — the narrower scope is the more specific intent.
     */
    fun valuesAt(
        timeMs: Long,
        clipId: ClipId? = null,
    ): Map<ParamId, ParamValue> {
        val out = LinkedHashMap<ParamId, ParamValue>()
        val programme = tracks.filter { it.enabled && it.clipId == null }
        val scoped = if (clipId == null) emptyList() else tracks.filter { it.enabled && it.clipId == clipId }
        for (track in programme + scoped) {
            track.valueAt(timeMs)?.let { out[track.paramId] = it }
        }
        return out
    }

    /**
     * Follow a ripple edit. A clip-scoped track moves when its own clip moved; a programme-level
     * track only moves when the whole programme did, because a gap closed on one lane says nothing
     * about the animation running over the piece as a whole.
     */
    fun rippled(shift: RippleShift): KeyframeSheet =
        copy(
            tracks =
                tracks.map { track ->
                    val clipId = track.clipId
                    val follows =
                        if (clipId == null) shift.scope == RippleScope.ALL_LANES else clipId in shift.movedClips
                    if (follows) track.rippled(shift) else track
                },
        )

    /** Keys belonging to a clip travel with it when the clip is moved. */
    fun shiftClip(
        clipId: ClipId,
        deltaMs: Long,
    ): KeyframeSheet = copy(tracks = tracks.map { if (it.clipId == clipId) it.shiftedBy(deltaMs) else it })
}

/** One instant of a recorded performance, already reduced to the parameters the editor exposes. */
data class PerformanceSample(
    val atMs: Long,
    val values: Map<ParamId, ParamValue>,
)

/**
 * Dropping a recorded take onto the timeline.
 *
 * A [PerformanceTake] is a stream of parameter deltas; the editor wants ordinary keyframe tracks
 * that can be dragged, re-curved and deleted like any other. Nothing here is special-cased on the
 * way out: once imported, a take is indistinguishable from keys placed by hand.
 */
object PerformanceKeyframes {
    /**
     * Sample a stored take into tracks.
     *
     * [read] turns a take's state into the parameters the editor exposes — the take stores whole
     * scene state, and only the caller knows which of it is worth animating. Sampling walks
     * forwards at the recorder's own resolution: finer steps cannot recover detail the recorder
     * never wrote, and [PerformanceTake.Timeline.stateAt] is built for a forward scan.
     */
    fun fromTake(
        take: PerformanceTake.Timeline,
        read: (PerformanceTake.State) -> Map<ParamId, ParamValue>,
        idFor: (ParamId, Long) -> KeyframeId,
        sampleEveryMs: Long = PerformanceTake.MIN_KEYFRAME_GAP_MS,
        offsetMs: Long = take.trackOffsetMs,
        interpolation: Interpolation = Interpolation.Linear,
    ): List<KeyframeTrack> {
        val step = sampleEveryMs.coerceAtLeast(1L)
        val lastMs = take.lastEventMs()
        val samples = ArrayList<PerformanceSample>()
        var atMs = 0L
        while (atMs <= lastMs) {
            take.stateAt(atMs)?.let { samples += PerformanceSample((atMs + offsetMs).coerceAtLeast(0L), read(it)) }
            atMs += step
        }
        return fromSamples(samples, idFor, interpolation)
    }

    /**
     * Reduce sampled values to keys. Repeated values collapse to the two keys that bracket the
     * plateau, so a parameter the performer held still stays still instead of drifting across it.
     */
    fun fromSamples(
        samples: List<PerformanceSample>,
        idFor: (ParamId, Long) -> KeyframeId,
        interpolation: Interpolation = Interpolation.Linear,
    ): List<KeyframeTrack> {
        val paramIds = samples.flatMapTo(LinkedHashSet()) { it.values.keys }
        return paramIds
            .map { paramId ->
                KeyframeTrack(paramId = paramId, keys = reduceToKeys(paramId, samples, idFor, interpolation))
            }.filterNot { it.isEmpty }
    }

    private fun reduceToKeys(
        paramId: ParamId,
        samples: List<PerformanceSample>,
        idFor: (ParamId, Long) -> KeyframeId,
        interpolation: Interpolation,
    ): List<Keyframe> {
        val keys = ArrayList<Keyframe>()
        var held: ParamValue? = null
        var plateauEndMs: Long? = null
        for (sample in samples) {
            val value = sample.values[paramId] ?: continue
            if (value == held) {
                plateauEndMs = sample.atMs
            } else {
                val previous = held
                val plateau = plateauEndMs
                if (previous != null && plateau != null) {
                    keys += Keyframe(idFor(paramId, plateau), plateau, previous, interpolation)
                }
                keys += Keyframe(idFor(paramId, sample.atMs), sample.atMs, value, interpolation)
                held = value
                plateauEndMs = null
            }
        }
        val tail = held
        val tailAtMs = plateauEndMs
        if (tail != null && tailAtMs != null) {
            keys += Keyframe(idFor(paramId, tailAtMs), tailAtMs, tail, interpolation)
        }
        return keys
    }
}

internal fun lerp(
    from: Float,
    to: Float,
    fraction: Float,
): Float = from + (to - from) * fraction

private fun blendBetween(
    from: Keyframe,
    to: Keyframe,
    timeMs: Long,
): ParamValue {
    val span = (to.atMs - from.atMs).coerceAtLeast(1L)
    val progress = ((timeMs - from.atMs).toFloat() / span).coerceIn(0f, 1f)
    return blend(from.value, to.value, from.interpolation.fractionAt(progress))
}

private fun blend(
    from: ParamValue,
    to: ParamValue,
    fraction: Float,
): ParamValue =
    when (from) {
        is ParamValue.Scalar ->
            if (to is ParamValue.Scalar) ParamValue.Scalar(lerp(from.value, to.value, fraction)) else from
        is ParamValue.Vector2 ->
            if (to is ParamValue.Vector2) {
                ParamValue.Vector2(lerp(from.x, to.x, fraction), lerp(from.y, to.y, fraction))
            } else {
                from
            }
        is ParamValue.Colour ->
            if (to is ParamValue.Colour) {
                ParamValue.Colour(
                    r = lerp(from.r, to.r, fraction),
                    g = lerp(from.g, to.g, fraction),
                    b = lerp(from.b, to.b, fraction),
                    a = lerp(from.a, to.a, fraction),
                )
            } else {
                from
            }
        // Discrete values have no midpoint: they hold until their own next key.
        is ParamValue.Toggle -> from
        is ParamValue.Choice -> from
    }

/** Index of the last key at or before [timeMs], or -1. Binary search: tracks get long. */
private fun List<Keyframe>.indexAtOrBefore(timeMs: Long): Int {
    var low = 0
    var high = size - 1
    var found = -1
    while (low <= high) {
        val mid = (low + high) ushr 1
        if (this[mid].atMs <= timeMs) {
            found = mid
            low = mid + 1
        } else {
            high = mid - 1
        }
    }
    return found
}
