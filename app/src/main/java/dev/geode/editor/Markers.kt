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

@JvmInline
value class MarkerId(
    val value: String,
)

/** A small fixed palette so markers read as a set of colours rather than as arbitrary paint. */
enum class MarkerColour(
    val argb: Int,
) {
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

    private fun ordered(next: List<Marker>): MarkerSet = copy(markers = next.sortedBy { it.atMs })
}
