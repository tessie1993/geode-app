package dev.geode.editor

import dev.geode.analysis.AudioFeatures
import dev.geode.analysis.FeatureTimeline

/*
 * Auto-cut — Product Spec §9.
 *
 * Cuts and markers are proposed from the transients in the track: the hits the player actually
 * played. Nothing here estimates or follows a tempo, and nothing here writes to the project — a
 * detection run returns suggestions, and the user decides which of them become ordinary clips and
 * markers that can then be dragged, trimmed and deleted like any others.
 */

/** Far enough apart that consecutive cuts read as cuts rather than as a flicker. */
const val DEFAULT_MIN_CUT_SPACING_MS: Long = 250L

/** A cap so a noisy track cannot bury the timeline under thousands of suggestions. */
const val DEFAULT_MAX_CUTS: Int = 512

/** How much music the adaptive threshold looks at around each instant. */
const val DEFAULT_THRESHOLD_WINDOW_MS: Long = 1_000L

/** Which measured attack to cut on. Notably absent: beat and bpm — auto-cut does not follow a grid. */
enum class TransientSource {
    /** Broadband attack strength: the general "something hit" signal. */
    TRANSIENT,

    /** Raw spectral flux, before any pulse shaping. Busier, good for texture-heavy material. */
    SPECTRAL_FLUX,

    ONSET,

    KICK,

    SNARE,

    HAT,
}

/**
 * A detection function sampled along the track: how much new energy arrived at each time.
 *
 * Values are held as lists rather than as float arrays because this is an offline, undoable,
 * comparable document — not a per-frame render path where a preallocated buffer would earn its
 * keep.
 */
data class TransientEnvelope(
    val timesMs: List<Long>,
    val strength: List<Float>,
) {
    init {
        // Mismatched columns are a caller bug, not something a user can do — fail loudly.
        require(timesMs.size == strength.size) {
            "envelope times (${timesMs.size}) and strength (${strength.size}) must line up"
        }
    }

    val isEmpty: Boolean get() = timesMs.isEmpty()

    val durationMs: Long get() = timesMs.lastOrNull() ?: 0L

    companion object {
        /** Read an offline analysis into an envelope, taking one measured channel from each frame. */
        fun from(
            timeline: FeatureTimeline,
            source: TransientSource = TransientSource.TRANSIENT,
        ): TransientEnvelope =
            TransientEnvelope(
                timesMs = timeline.frames.map { it.timeMs },
                strength = timeline.frames.map { source.read(it.features) },
            )
    }
}

data class AutoCutSettings(
    /**
     * Which channel to build the envelope from — see [TransientEnvelope.from]. Detection itself
     * reads the envelope it is handed; this is kept here so the choice round-trips with the rest
     * of the settings and comes back attached to the suggestions it produced.
     */
    val source: TransientSource = TransientSource.TRANSIENT,
    /** 0 keeps only the hits that tower over the mix, 1 takes almost every rise. */
    val sensitivity: Float = 0.5f,
    val minSpacingMs: Long = DEFAULT_MIN_CUT_SPACING_MS,
    val fromMs: Long = 0L,
    val untilMs: Long = Long.MAX_VALUE,
    val maxCuts: Int = DEFAULT_MAX_CUTS,
    val thresholdWindowMs: Long = DEFAULT_THRESHOLD_WINDOW_MS,
)

/**
 * One suggested cut. [strength] is relative to the loudest hit in the analysed window, and
 * [confidence] is how far clear of the local threshold it sat — the two things a UI needs to show
 * suggestions at different weights before any of them are accepted.
 */
data class TransientHit(
    val atMs: Long,
    val strength: Float,
    val confidence: Float,
)

sealed interface AutoCutResult {
    data class Suggested(
        val hits: List<TransientHit>,
        val settings: AutoCutSettings,
    ) : AutoCutResult

    data class NoCuts(val reason: AutoCutMiss) : AutoCutResult
}

/** Why a run found nothing. Expected outcomes, all of them: the UI explains, it does not throw. */
sealed interface AutoCutMiss {
    data object EmptyEnvelope : AutoCutMiss

    /** Silence, or a pad with no attacks in it. Turning sensitivity up will not invent hits. */
    data object NoTransients : AutoCutMiss

    data class WindowTooShort(val spanMs: Long) : AutoCutMiss
}

object AutoCut {
    /**
     * Peak-pick the envelope against a rolling median.
     *
     * A fixed threshold cuts everything in a loud chorus and nothing in a quiet verse, so the bar
     * each hit has to clear is the median of the music around it, scaled by sensitivity. Peaks
     * closer together than the minimum spacing collapse to the loudest of them.
     */
    fun detect(
        envelope: TransientEnvelope,
        settings: AutoCutSettings = AutoCutSettings(),
    ): AutoCutResult {
        val window = envelope.indicesIn(settings.fromMs, settings.untilMs)
        val peak = window.maxOfOrNull { envelope.strength[it] } ?: 0f
        return when {
            envelope.isEmpty -> AutoCutResult.NoCuts(AutoCutMiss.EmptyEnvelope)
            window.isEmpty() -> AutoCutResult.NoCuts(AutoCutMiss.WindowTooShort(settings.untilMs - settings.fromMs))
            peak <= 0f -> AutoCutResult.NoCuts(AutoCutMiss.NoTransients)
            else -> pickPeaks(envelope, window, settings, peak)
        }
    }

    /** Turn accepted suggestions into ordinary markers, tagged with where they came from. */
    fun markersFrom(
        hits: List<TransientHit>,
        idFor: (TransientHit) -> MarkerId,
        colour: MarkerColour = MarkerColour.VIOLET,
        namePrefix: String = "Cut",
    ): List<Marker> =
        hits.mapIndexed { index, hit ->
            Marker(
                id = idFor(hit),
                atMs = hit.atMs,
                name = "$namePrefix ${index + 1}",
                colour = colour,
                origin = MarkerOrigin.Detected(confidence = hit.confidence, strength = hit.strength),
            )
        }

    /**
     * Turn accepted suggestions into clips that butt up against each other, the first starting on
     * the first hit and the last running to [untilMs]. The stretch before the first hit is left
     * empty: nothing was detected there, so there is no cut to justify.
     *
     * Auto-cut decides *when* the cuts fall; [content] decides what plays in each one, because
     * that is a taste decision and this is not the place for it.
     */
    fun clipsFrom(
        hits: List<TransientHit>,
        untilMs: Long,
        idFor: (Int) -> ClipId,
        content: (index: Int, startMs: Long, durationMs: Long) -> ClipContent,
    ): List<Clip> {
        val cuts = hits.map { it.atMs }.filter { it < untilMs }.sorted()
        return cuts.mapIndexedNotNull { index, startMs ->
            val durationMs = (cuts.getOrNull(index + 1) ?: untilMs) - startMs
            if (durationMs < MIN_CLIP_DURATION_MS) {
                null
            } else {
                Clip(
                    id = idFor(index),
                    content = content(index, startMs, durationMs),
                    startMs = startMs,
                    durationMs = durationMs,
                )
            }
        }
    }

    private fun pickPeaks(
        envelope: TransientEnvelope,
        window: IntRange,
        settings: AutoCutSettings,
        peak: Float,
    ): AutoCutResult {
        val normalized = FloatArray(envelope.strength.size) { envelope.strength[it] / peak }
        val hopMs = envelope.medianHopMs()
        val half = (settings.thresholdWindowMs / 2L / hopMs).toInt().coerceAtLeast(1)
        val look = (LOCAL_MAX_WINDOW_MS / hopMs).toInt().coerceAtLeast(1)
        val medians = rollingMedians(normalized, half)
        val sensitivity = settings.sensitivity.coerceIn(0f, 1f)
        val multiplier = lerp(QUIET_MULTIPLIER, LOUD_MULTIPLIER, sensitivity)
        val delta = lerp(QUIET_DELTA, LOUD_DELTA, sensitivity)

        val hits = ArrayList<TransientHit>()
        for (i in window) {
            val value = normalized[i]
            val bar = medians[i] * multiplier + delta
            if (value >= bar && isLocalMax(normalized, i, look)) {
                hits.offer(
                    TransientHit(
                        atMs = envelope.timesMs[i],
                        strength = value,
                        confidence = ((value - bar) / (1f - bar).coerceAtLeast(MIN_HEADROOM)).coerceIn(0f, 1f),
                    ),
                    settings.minSpacingMs,
                )
            }
        }

        val capped =
            if (hits.size <= settings.maxCuts) {
                hits.toList()
            } else {
                hits.sortedByDescending { it.strength }.take(settings.maxCuts).sortedBy { it.atMs }
            }
        return if (capped.isEmpty()) {
            AutoCutResult.NoCuts(AutoCutMiss.NoTransients)
        } else {
            AutoCutResult.Suggested(capped, settings)
        }
    }

    private const val LOCAL_MAX_WINDOW_MS = 30L
    private const val QUIET_MULTIPLIER = 2.4f
    private const val LOUD_MULTIPLIER = 1.05f
    private const val QUIET_DELTA = 0.18f
    private const val LOUD_DELTA = 0.01f
    private const val MIN_HEADROOM = 1e-3f
}

private fun TransientSource.read(features: AudioFeatures): Float =
    when (this) {
        TransientSource.TRANSIENT -> features.transient
        TransientSource.SPECTRAL_FLUX -> features.flux
        TransientSource.ONSET -> features.onset
        TransientSource.KICK -> features.kick
        TransientSource.SNARE -> features.snare
        TransientSource.HAT -> features.hat
    }

private fun TransientEnvelope.indicesIn(
    fromMs: Long,
    untilMs: Long,
): IntRange {
    val first = timesMs.indexOfFirst { it >= fromMs }
    val last = timesMs.indexOfLast { it < untilMs }
    return if (first < 0 || last < first) IntRange.EMPTY else first..last
}

/** Frame spacing, taken as a median so a single ragged gap cannot skew the window sizes. */
private fun TransientEnvelope.medianHopMs(): Long {
    val gaps = timesMs.zipWithNext { a, b -> b - a }.filter { it > 0L }.sorted()
    return if (gaps.isEmpty()) 1L else gaps[gaps.size / 2].coerceAtLeast(1L)
}

/**
 * Median of the values within [half] samples of each index. One scratch buffer is reused for the
 * sorted window: this runs over a whole track, and there is no reason to allocate an array per
 * sample to do it.
 */
private fun rollingMedians(
    values: FloatArray,
    half: Int,
): FloatArray {
    val out = FloatArray(values.size)
    val buffer = FloatArray(half * 2 + 1)
    for (i in values.indices) {
        val from = (i - half).coerceAtLeast(0)
        val to = (i + half).coerceAtMost(values.lastIndex)
        val count = to - from + 1
        values.copyInto(buffer, 0, from, to + 1)
        buffer.sort(0, count)
        out[i] = buffer[count / 2]
    }
    return out
}

private fun isLocalMax(
    values: FloatArray,
    index: Int,
    look: Int,
): Boolean {
    val from = (index - look).coerceAtLeast(0)
    val to = (index + look).coerceAtMost(values.lastIndex)
    var highest = true
    for (i in from..to) {
        if (values[i] > values[index]) highest = false
    }
    return highest
}

/**
 * Add a hit, keeping the run at least [minSpacingMs] apart. Inside the spacing window the louder
 * hit wins, but only when dropping the quieter one does not pull the new hit too close to the one
 * before it — otherwise a run of rising hits would walk straight through the spacing rule.
 */
private fun MutableList<TransientHit>.offer(
    candidate: TransientHit,
    minSpacingMs: Long,
) {
    val last = lastOrNull()
    val previous = getOrNull(lastIndex - 1)
    val clearOfLast = last == null || candidate.atMs - last.atMs >= minSpacingMs
    val louderThanLast = last != null && candidate.strength > last.strength
    val clearOfPrevious = previous == null || candidate.atMs - previous.atMs >= minSpacingMs
    when {
        clearOfLast -> add(candidate)
        louderThanLast && clearOfPrevious -> set(lastIndex, candidate)
    }
}
