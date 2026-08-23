package dev.geode.editor

import kotlin.math.abs

/*
 * Markers — Product Spec §9.
 *
 * A marker is the user's own mark on the track: the drop, the bar they want the text to hit, the
 * moment the vocal comes in. They are placed by hand or tapped in live while the music plays, and
 * everything else in the editor (clip snapping, keyframe snapping, auto-cut suggestions) is
 * expressed against them rather than against any tempo grid.
 */

/**
 * How late a tap lands by default: human reaction to a hit sits around 100 ms and Android's audio
 * output adds a little more, so a tap is nudged back by this much to land where the user felt it.
 * The raw tap is kept on the marker so the whole session can be re-nudged later.
 */
const val DEFAULT_TAP_LATENCY_MS: Long = 120L

/** One physical hit should make one marker; a second trigger inside this window is a bounce. */
const val DEFAULT_TAP_DEBOUNCE_MS: Long = 90L

@JvmInline
value class MarkerId(val value: String)

/** A small fixed palette so markers read as a set of colours rather than as arbitrary paint. */
enum class MarkerColour(val argb: Int) {
    RED(0xFFE5484D.toInt()),
    AMBER(0xFFFFB224.toInt()),
    LIME(0xFF99D52A.toInt()),
    CYAN(0xFF00C2D7.toInt()),
    BLUE(0xFF3E63DD.toInt()),
    VIOLET(0xFF8E4EC6.toInt()),
    WHITE(0xFFF1F1F1.toInt()),
}

/** Where a marker came from, which is what lets the editor treat the three kinds differently. */
sealed interface MarkerOrigin {
    /** Placed deliberately, usually with the transport parked. */
    data object Manual : MarkerOrigin

    /**
     * Tapped in live. [rawAtMs] is the uncompensated playhead position at the moment of the tap;
     * keeping it means changing the latency setting can re-place the whole take instead of only
     * affecting taps made afterwards.
     */
    data class TappedIn(
        val rawAtMs: Long,
        val latencyCompensationMs: Long,
    ) : MarkerOrigin

    /** Produced by auto-cut from a transient. Always a suggestion the user accepted. */
    data class Detected(
        val confidence: Float,
        val strength: Float,
    ) : MarkerOrigin
}

data class Marker(
    val id: MarkerId,
    val atMs: Long,
    val name: String = "",
    val colour: MarkerColour = MarkerColour.CYAN,
    val note: String = "",
    val origin: MarkerOrigin = MarkerOrigin.Manual,
)

/**
 * The marker document. Kept in time order by every operation here — nothing outside this file
 * should build one with [copy], because the ordering is what makes lookup and snapping cheap.
 */
data class MarkerSet(
    val markers: List<Marker> = emptyList(),
) {
    val timesMs: List<Long> get() = markers.map { it.atMs }

    val isEmpty: Boolean get() = markers.isEmpty()

    fun marker(id: MarkerId): Marker? = markers.firstOrNull { it.id == id }

    fun add(marker: Marker): MarkerSet = ordered(markers.filterNot { it.id == marker.id } + marker)

    fun addAll(incoming: List<Marker>): MarkerSet {
        val incomingIds = incoming.map { it.id }.toSet()
        return ordered(markers.filterNot { it.id in incomingIds } + incoming)
    }

    fun remove(id: MarkerId): MarkerSet = ordered(markers.filterNot { it.id == id })

    fun update(
        id: MarkerId,
        transform: (Marker) -> Marker,
    ): MarkerSet = ordered(markers.map { if (it.id == id) transform(it) else it })

    fun rename(
        id: MarkerId,
        name: String,
    ): MarkerSet = update(id) { it.copy(name = name) }

    fun recolour(
        id: MarkerId,
        colour: MarkerColour,
    ): MarkerSet = update(id) { it.copy(colour = colour) }

    fun annotate(
        id: MarkerId,
        note: String,
    ): MarkerSet = update(id) { it.copy(note = note) }

    /**
     * Drag a marker. Markers snap like anything else, but a tapped-in marker that is moved by hand
     * stops being a tap: its position is now the user's decision, not the latency setting's.
     */
    fun moveTo(
        id: MarkerId,
        atMs: Long,
        snap: SnapMode = SnapMode.Free,
        context: SnapContext = SnapContext(),
    ): MarkerSet =
        update(id) {
            it.copy(
                atMs = snap.snap(atMs, context).coerceAtLeast(0L),
                origin = if (it.origin is MarkerOrigin.TappedIn) MarkerOrigin.Manual else it.origin,
            )
        }

    fun nearest(
        atMs: Long,
        withinMs: Long = Long.MAX_VALUE,
    ): Marker? = markers.minByOrNull { abs(it.atMs - atMs) }?.takeIf { abs(it.atMs - atMs) <= withinMs }

    fun inRange(
        fromMs: Long,
        untilMs: Long,
    ): List<Marker> = markers.filter { it.atMs >= fromMs && it.atMs < untilMs }

    /** Follow a ripple edit so the marks stay on the music they were put against. */
    fun rippled(shift: RippleShift): MarkerSet =
        ordered(
            markers.map {
                if (it.atMs >= shift.fromMs) it.copy(atMs = (it.atMs + shift.deltaMs).coerceAtLeast(0L)) else it
            },
        )

    /** Re-place every tapped-in marker with a new latency figure, using the raw taps kept on them. */
    fun recalibrateTaps(latencyCompensationMs: Long): MarkerSet = ordered(markers.map { it.withLatency(latencyCompensationMs) })

    private fun ordered(next: List<Marker>): MarkerSet = copy(markers = next.sortedBy { it.atMs })
}

data class TapInSettings(
    val latencyCompensationMs: Long = DEFAULT_TAP_LATENCY_MS,
    val debounceMs: Long = DEFAULT_TAP_DEBOUNCE_MS,
    val colour: MarkerColour = MarkerColour.AMBER,
    val namePrefix: String = "Hit",
)

/**
 * A live tap-in pass: the track plays, the user hits the button in time with the music, and each
 * hit lands a marker. The pass is held apart from the [MarkerSet] until it is committed, so a
 * botched run costs nothing and the latency can still be re-tuned against the raw taps first.
 */
data class TapInSession(
    val settings: TapInSettings = TapInSettings(),
    val taps: List<Marker> = emptyList(),
) {
    val count: Int get() = taps.size

    /** Register a tap made while the transport sat at [playheadMs]. */
    fun tap(
        id: MarkerId,
        playheadMs: Long,
    ): TapResult {
        // Measured on the raw taps and unsigned, so seeking backwards mid-pass and tapping again is
        // judged by how close in the music the two hits are, not by which way the transport moved.
        val sinceLast = taps.lastOrNull()?.let { last -> abs(playheadMs - rawOf(last)) } ?: Long.MAX_VALUE
        return if (sinceLast < settings.debounceMs) {
            TapResult.Debounced(this, sinceLast)
        } else {
            val marker =
                Marker(
                    id = id,
                    atMs = (playheadMs - settings.latencyCompensationMs).coerceAtLeast(0L),
                    name = "${settings.namePrefix} ${taps.size + 1}",
                    colour = settings.colour,
                    origin = MarkerOrigin.TappedIn(playheadMs, settings.latencyCompensationMs),
                )
            TapResult.Placed(copy(taps = taps + marker), marker)
        }
    }

    /** Drop the last tap — the obvious undo for a mistimed hit without leaving the pass. */
    fun undoLast(): TapInSession = if (taps.isEmpty()) this else copy(taps = taps.dropLast(1))

    fun clear(): TapInSession = copy(taps = emptyList())

    /** Re-place the whole pass with a different latency figure before committing it. */
    fun withLatency(latencyCompensationMs: Long): TapInSession =
        copy(
            settings = settings.copy(latencyCompensationMs = latencyCompensationMs),
            taps = taps.map { it.withLatency(latencyCompensationMs) },
        )

    /**
     * Merge the pass into the document. [mergeWithinMs] above zero drops taps that landed on top
     * of a marker that already existed, which is what a second pass over the same section wants.
     */
    fun commitTo(
        markers: MarkerSet,
        mergeWithinMs: Long = 0L,
    ): MarkerSet =
        markers.addAll(
            if (mergeWithinMs <= 0L) taps else taps.filter { markers.nearest(it.atMs, mergeWithinMs) == null },
        )

    private fun rawOf(marker: Marker): Long =
        when (val origin = marker.origin) {
            is MarkerOrigin.TappedIn -> origin.rawAtMs
            MarkerOrigin.Manual, is MarkerOrigin.Detected -> marker.atMs
        }
}

sealed interface TapResult {
    data class Placed(
        val session: TapInSession,
        val marker: Marker,
    ) : TapResult

    /** The tap fell inside the debounce window: one physical hit, one marker. */
    data class Debounced(
        val session: TapInSession,
        val sinceLastMs: Long,
    ) : TapResult
}

private fun Marker.withLatency(latencyCompensationMs: Long): Marker =
    when (val current = origin) {
        is MarkerOrigin.TappedIn ->
            copy(
                atMs = (current.rawAtMs - latencyCompensationMs).coerceAtLeast(0L),
                origin = current.copy(latencyCompensationMs = latencyCompensationMs),
            )
        MarkerOrigin.Manual, is MarkerOrigin.Detected -> this
    }
