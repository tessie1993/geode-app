package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.analysis.SearchMatcher
import dev.geode.model.DeviceTrack
import java.util.IdentityHashMap

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
 * One track with its search fields folded once ([SearchMatcher.fold]), so typing walks
 * precomputed text instead of re-folding the library on every keystroke. [fields] are in rank
 * order: title, artist, album, folder.
 */
class IndexedTrack(
    val track: DeviceTrack,
) {
    val fields: List<String> = listOf(track.title, track.artist, track.album, track.folder).map(SearchMatcher::fold)

    /** Each field's words, for the typo fallback; split here so a keystroke never runs the regex. */
    val words: List<List<String>> = fields.map { field -> field.split(LibraryBrowse.WORD_SPLIT).filter { it.isNotBlank() } }
}

/** The library ready to search: built off the main thread once per library change ([LibraryBrowse.index]). */
class LibraryIndex(
    val entries: List<IndexedTrack>,
) {
    val size: Int get() = entries.size
}

/**
 * What the library's track-shaped tabs draw for one query and sort: the tracks, and the album,
 * artist and folder groupings of the same tracks. Built together off the main thread so no tab
 * groups 15k tracks during composition.
 */
data class LibraryBrowseResult(
    val tracks: List<DeviceTrack> = emptyList(),
    val albums: Map<String, List<DeviceTrack>> = emptyMap(),
    val artists: Map<String, List<DeviceTrack>> = emptyMap(),
    val folders: Map<String, List<DeviceTrack>> = emptyMap(),
)

/**
 * Ordering and searching for the library, kept free of Compose so it can be reasoned about — and
 * tested — on its own.
 *
 * Nothing here reads tempo, key or any analysis result. A track is browsable the moment its tags
 * are known, which is the whole point: the library never waits for anything.
 */
object LibraryBrowse {
    /**
     * The favourites as tracks, in the order given (the store's newest first). A favourite the
     * device no longer lists (deleted, or under the minimum length) is left out, not shown dead.
     */
    fun favouriteTracks(
        favourites: Collection<String>,
        tracks: List<DeviceTrack>,
    ): List<DeviceTrack> {
        if (favourites.isEmpty()) return emptyList()
        val byUri = tracks.associateBy { it.uri }
        return favourites.mapNotNull { byUri[it] }
    }

    fun index(tracks: List<DeviceTrack>): LibraryIndex = LibraryIndex(tracks.map(::IndexedTrack))

    /**
     * The one pass the library screen runs per (debounced) query: filter, rank, sort, group.
     *
     * A blank query is the whole library in [order]. Otherwise every term must land somewhere
     * (extra words narrow, never widen), and the hits come title first, then artist, then album,
     * then folder ([SearchMatcher.tier]); within a tier [order] still holds.
     */
    fun browse(
        index: LibraryIndex,
        query: String,
        order: LibrarySort,
    ): LibraryBrowseResult {
        val tracks = searchIndexed(index, query, order)
        return LibraryBrowseResult(
            tracks = tracks,
            albums = tracks.groupBy { it.album },
            artists = tracks.groupBy { it.artist },
            folders = tracks.groupBy { it.folder },
        )
    }

    private fun searchIndexed(
        index: LibraryIndex,
        query: String,
        order: LibrarySort,
    ): List<DeviceTrack> {
        val terms = SearchMatcher.terms(query)
        if (terms.isEmpty()) return sortIndexed(index.entries, order).map { it.track }
        val hits = rank(index.entries, terms)
        val tierOf = IdentityHashMap<IndexedTrack, Int>(hits.size).apply { hits.forEach { (entry, tier) -> put(entry, tier) } }
        // Stable sort: tiers first, the user's order inside each.
        return sortIndexed(hits.map { it.first }, order)
            .sortedBy { tierOf[it] ?: 0 }
            .map { it.track }
    }

    /** Every entry that matches [terms], with its tier, in the entries' own order. */
    private fun rank(
        entries: List<IndexedTrack>,
        terms: List<String>,
    ): List<Pair<IndexedTrack, Int>> {
        val hits = ArrayList<Pair<IndexedTrack, Int>>()
        for (entry in entries) {
            val exact = SearchMatcher.tier(terms, entry.fields)
            val tier = if (exact != SearchMatcher.NO_MATCH) exact else typoTier(terms, entry)
            if (tier != SearchMatcher.NO_MATCH) hits += entry to tier
        }
        return hits
    }

    /**
     * The tier allowing for typos, ranked below every exact hit: a typo match is a guess, and a
     * guess should not push down what the user actually spelled.
     */
    private fun typoTier(
        terms: List<String>,
        entry: IndexedTrack,
    ): Int {
        val tier =
            SearchMatcher.tier(terms, entry.fields) { term, i ->
                if (entry.fields[i].contains(term)) {
                    true
                } else {
                    val budget = typoBudget(term)
                    budget > 0 && entry.words[i].any { withinEditDistance(term, it, budget) }
                }
            }
        return if (tier == SearchMatcher.NO_MATCH) tier else tier + TYPO_TIER_OFFSET
    }

    private fun sortIndexed(
        entries: List<IndexedTrack>,
        order: LibrarySort,
    ): List<IndexedTrack> =
        when (order) {
            // The folded text sorts "Élan" with the Es, where a reader looks for it.
            LibrarySort.TITLE -> entries.sortedBy { it.fields[0] }
            LibrarySort.ARTIST -> entries.sortedWith(compareBy({ it.fields[1] }, { it.fields[0] }))
            LibrarySort.ALBUM -> entries.sortedWith(compareBy({ it.fields[2] }, { it.fields[0] }))
            LibrarySort.DURATION -> entries.sortedBy { it.track.durationMs }
            LibrarySort.ADDED -> entries.sortedByDescending { it.track.addedSec }
        }

    fun sort(
        tracks: List<DeviceTrack>,
        order: LibrarySort,
    ): List<DeviceTrack> = sortIndexed(tracks.map(::IndexedTrack), order).map { it.track }

    /**
     * Searches a list that has no index yet (voice search): folds it on the spot. The library
     * screen goes through [index] and [browse] instead.
     *
     * A blank query returns the list untouched rather than nothing — an empty search box is not
     * a search.
     */
    fun search(
        tracks: List<DeviceTrack>,
        query: String,
    ): List<DeviceTrack> {
        val terms = SearchMatcher.terms(query)
        if (terms.isEmpty()) return tracks
        return rank(tracks.map(::IndexedTrack), terms).sortedBy { it.second }.map { it.first.track }
    }

    /**
     * True when [term] appears in any field, allowing for a typo. Both sides are folded, so case
     * and accents never count as typos.
     *
     * Substring first, because that is what most typing is. Only if nothing contains the term do
     * we pay for edit distance, and the budget scales with length: a three-letter word has no
     * spare edits before it turns into a different word entirely.
     */
    fun matches(
        term: String,
        fields: List<String>,
    ): Boolean {
        val folded = SearchMatcher.fold(term)
        val foldedFields = fields.map(SearchMatcher::fold)
        if (folded.isBlank() || foldedFields.any { it.contains(folded) }) return true
        val budget = typoBudget(folded)
        return budget > 0 &&
            foldedFields.any { field ->
                field.split(WORD_SPLIT).any { word ->
                    word.isNotBlank() && withinEditDistance(folded, word, budget)
                }
            }
    }

    private fun typoBudget(term: String): Int =
        when {
            term.length <= 3 -> 0
            term.length <= 5 -> 1
            else -> 2
        }

    /** How far below the exact tiers a typo hit sorts: past folder, the last exact tier. */
    private const val TYPO_TIER_OFFSET = 4

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

    internal val WORD_SPLIT = Regex("[\\s\\-_./]+")
}
