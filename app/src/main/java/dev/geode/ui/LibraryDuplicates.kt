package dev.geode.ui

import kotlin.math.abs

data class DuplicateGroup(
    val title: String,
    val artist: String,
    val tracks: List<DeviceTrack>,
)

/** Tracks that look like the same recording: same title and artist once normalised, and lengths within two seconds. */
object LibraryDuplicates {
    private const val DURATION_SLACK_MS = 2_000L

    fun find(tracks: List<DeviceTrack>): List<DuplicateGroup> =
        tracks
            .groupBy { normalise(it.title) to normalise(it.artist) }
            .values
            .flatMap { same -> splitByDuration(same.sortedBy { it.durationMs }) }
            .filter { it.size > 1 }
            .map { group -> DuplicateGroup(group.first().title, group.first().artist, group.sortedBy { it.folder }) }
            .sortedBy { it.title.lowercase() }

    private fun splitByDuration(sorted: List<DeviceTrack>): List<List<DeviceTrack>> {
        val groups = ArrayList<MutableList<DeviceTrack>>()
        for (track in sorted) {
            val open = groups.lastOrNull()
            if (open != null && abs(open.last().durationMs - track.durationMs) <= DURATION_SLACK_MS) open += track else groups += mutableListOf(track)
        }
        return groups
    }

    internal fun normalise(text: String): String =
        text
            .lowercase()
            .replace(NOISE, " ")
            .replace(SPACES, " ")
            .trim()

    private val NOISE = Regex("""[\[(].*?[\])]|[^\p{L}\p{N}\s]""")
    private val SPACES = Regex("""\s+""")
}
