package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R

/**
 * How the track list is ordered. Exhaustive by construction: adding an order is a compile error
 * until every `when` over it handles the new case.
 */
enum class LibrarySort(
    @param:StringRes val labelRes: Int,
) {
    TITLE(R.string.library_sort_title),
    ARTIST(R.string.library_sort_artist),
    ALBUM(R.string.library_sort_album),
    DURATION(R.string.library_sort_duration),
    ADDED(R.string.library_sort_added),
}

/**
 * Ordering and searching for the library, kept free of Compose so it can be reasoned about — and
 * tested — on its own.
 *
 * Nothing here reads tempo, key or any analysis result. A track is browsable the moment its tags
 * are known, which is the whole point: the library never waits for anything.
 */
object LibraryBrowse {
    fun sort(
        tracks: List<DeviceTrack>,
        order: LibrarySort,
    ): List<DeviceTrack> =
        when (order) {
            LibrarySort.TITLE -> tracks.sortedBy { it.title.lowercase() }
            LibrarySort.ARTIST ->
                tracks.sortedWith(compareBy({ it.artist.lowercase() }, { it.title.lowercase() }))
            LibrarySort.ALBUM ->
                tracks.sortedWith(compareBy({ it.album.lowercase() }, { it.title.lowercase() }))
            LibrarySort.DURATION -> tracks.sortedBy { it.durationMs }
            LibrarySort.ADDED -> tracks.sortedByDescending { it.addedSec }
        }

    /**
     * Filters as you type across title, artist, album and filename.
     *
     * Every term must match something, so extra words narrow rather than widen. A blank query
     * returns the list untouched rather than nothing — an empty search box is not a search.
     */
    fun search(
        tracks: List<DeviceTrack>,
        query: String,
    ): List<DeviceTrack> {
        val terms = query.trim().split(WORD_SPLIT).filter { it.isNotBlank() }
        if (terms.isEmpty()) return tracks
        return tracks.filter { track ->
            val fields = listOf(track.title, track.artist, track.album, track.folder)
            terms.all { term -> matches(term, fields) }
        }
    }

    /**
     * True when [term] appears in any field, allowing for a typo.
     *
     * Substring first, because that is what most typing is. Only if nothing contains the term do
     * we pay for edit distance, and the budget scales with length: a three-letter word has no
     * spare edits before it turns into a different word entirely.
     */
    fun matches(
        term: String,
        fields: List<String>,
    ): Boolean {
        if (term.isBlank() || fields.any { it.contains(term, ignoreCase = true) }) return true
        val budget = typoBudget(term)
        return budget > 0 &&
            fields.any { field ->
                field.split(WORD_SPLIT).any { word ->
                    word.isNotBlank() && withinEditDistance(term, word, budget)
                }
            }
    }

    private fun typoBudget(term: String): Int =
        when {
            term.length <= 3 -> 0
            term.length <= 5 -> 1
            else -> 2
        }

    /**
     * Levenshtein distance, answered as a yes/no against [max] so it can stop early.
     *
     * Two rows rather than a full matrix, and the row minimum bails out as soon as every path is
     * already over budget — this runs per word per track on every keystroke.
     */
    internal fun withinEditDistance(
        a: String,
        b: String,
        max: Int,
    ): Boolean {
        if (a.length - b.length > max || b.length - a.length > max) return false
        var prev = IntArray(b.length + 1) { it }
        var cur = IntArray(b.length + 1)
        var rowMin = 0
        for (i in 1..a.length) {
            cur[0] = i
            rowMin = cur[0]
            for (j in 1..b.length) {
                val cost = if (a[i - 1].equals(b[j - 1], ignoreCase = true)) 0 else 1
                cur[j] = minOf(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost)
                if (cur[j] < rowMin) rowMin = cur[j]
            }
            if (rowMin > max) break
            val swap = prev
            prev = cur
            cur = swap
        }
        // A row whose cheapest path already exceeds the budget can only get dearer, so the break
        // above leaves `prev` on the last row worth reading and `rowMin` saying it was abandoned.
        return rowMin <= max && prev[b.length] <= max
    }

    /** `m:ss`, or blank when the duration is unknown — better nothing than a confident `0:00`. */
    fun formatDuration(durationMs: Long): String {
        if (durationMs <= 0L) return ""
        val totalSeconds = durationMs / 1000L
        return "%d:%02d".format(totalSeconds / 60L, totalSeconds % 60L)
    }

    private val WORD_SPLIT = Regex("[\\s\\-_./]+")
}
