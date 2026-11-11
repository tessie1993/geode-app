package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.model.DeviceTrack
import java.lang.ref.Cleaner
import java.lang.ref.Reference

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
 * The library's order and search as the native index answers them (`core/library/Search`), in row
 * numbers of the tracks it was built from. Tests hand in their own.
 */
interface LibrarySearcher {
    fun order(sort: LibrarySort): IntArray

    /** Three counts (tracks, albums, artists), then those rows, best first; an album or artist is its first row. */
    fun search(query: String): IntArray
}

/** The native index over [tracks]; freed once this holder is collected. */
private class NativeLibrarySearcher(
    tracks: List<DeviceTrack>,
) : LibrarySearcher {
    private val handle: Long

    init {
        val cells = arrayOfNulls<ByteArray>(tracks.size * 3)
        tracks.forEachIndexed { i, track ->
            cells[i * 3] = track.title.encodeToByteArray()
            cells[i * 3 + 1] = track.artist.encodeToByteArray()
            cells[i * 3 + 2] = track.album.encodeToByteArray()
        }
        val durations = LongArray(tracks.size) { tracks[it].durationMs }
        val added = LongArray(tracks.size) { tracks[it].addedSec }
        handle = GeodeNative.libraryIndexCreate(cells, durations, added)
        val owned = handle
        cleaner.register(this) { GeodeNative.libraryIndexDestroy(owned) }
    }

    override fun order(sort: LibrarySort): IntArray = held { GeodeNative.libraryIndexOrder(handle, nativeSort(sort)) }

    override fun search(query: String): IntArray = held { GeodeNative.libraryIndexSearch(handle, query.encodeToByteArray()) }

    /** Runs a native call with this object held past it, so the collector cannot free the index under the call. */
    private inline fun <T> held(call: () -> T): T =
        try {
            call()
        } finally {
            Reference.reachabilityFence(this)
        }

    /** `geode_library_index_order`'s sort ids. */
    private fun nativeSort(sort: LibrarySort): Int =
        when (sort) {
            LibrarySort.TITLE -> 0
            LibrarySort.ARTIST -> 1
            LibrarySort.ALBUM -> 2
            LibrarySort.DURATION -> 3
            LibrarySort.ADDED -> 4
        }

    private companion object {
        val cleaner: Cleaner = Cleaner.create()
    }
}

/**
 * The library ready to list and search: its tracks and the index over them, built off the main
 * thread once per library change ([LibraryBrowse.index]). The index is made on first use, so a
 * library nobody searches or sorts costs nothing.
 */
class LibraryIndex(
    val tracks: List<DeviceTrack>,
    searcherOf: (List<DeviceTrack>) -> LibrarySearcher,
) {
    val searcher: LibrarySearcher by lazy { searcherOf(tracks) }

    val size: Int get() = tracks.size
}

/** An album or artist a search found: its name as tagged, and every track of it in title order. */
data class LibraryGroupHit(
    val name: String,
    val tracks: List<DeviceTrack>,
)

/**
 * What the library draws for one query and sort, built together off the main thread so nothing
 * groups 15k tracks during composition.
 *
 * Browsing (a blank query): [tracks] in the user's order and the album, artist and folder
 * groupings the tabs draw. Searching: [tracks] are the hits, best first, with the artists and
 * albums the query names in [artistHits] and [albumHits]; the groupings are empty, since the tabs
 * step aside for the results.
 */
data class LibraryBrowseResult(
    val tracks: List<DeviceTrack> = emptyList(),
    val albums: Map<String, List<DeviceTrack>> = emptyMap(),
    val artists: Map<String, List<DeviceTrack>> = emptyMap(),
    val folders: Map<String, List<DeviceTrack>> = emptyMap(),
    val artistHits: List<LibraryGroupHit> = emptyList(),
    val albumHits: List<LibraryGroupHit> = emptyList(),
) {
    /** A search that found no track, album or artist. */
    val nothingFound: Boolean get() = tracks.isEmpty() && artistHits.isEmpty() && albumHits.isEmpty()
}

/**
 * Listing and searching the library, kept free of Compose so it can be reasoned about, and
 * tested, on its own. It decides nothing: the order, the match and the ranking are native
 * (`core/library/Search`, pinned by `core/tests/library_search_test.cpp`); this turns the rows
 * the index answers back into tracks and groups them.
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

    fun index(
        tracks: List<DeviceTrack>,
        searcherOf: (List<DeviceTrack>) -> LibrarySearcher = ::NativeLibrarySearcher,
    ): LibraryIndex = LibraryIndex(tracks, searcherOf)

    /**
     * The one pass the library screen runs per (debounced) query. A blank query is the whole
     * library in [order], grouped for the tabs; anything else is the search's hits, best first,
     * whatever the order: a result list sorted by duration would bury the song that was asked for.
     */
    fun browse(
        index: LibraryIndex,
        query: String,
        order: LibrarySort,
    ): LibraryBrowseResult {
        if (query.isBlank()) {
            val tracks = index.searcher.order(order).map { index.tracks[it] }
            return LibraryBrowseResult(
                tracks = tracks,
                albums = tracks.groupBy { it.album },
                artists = tracks.groupBy { it.artist },
                folders = tracks.groupBy { it.folder },
            )
        }
        val packed = index.searcher.search(query)
        if (packed.size < COUNTS) return LibraryBrowseResult()
        val (trackCount, albumCount, artistCount) = packed
        val rows = packed.drop(COUNTS).map { index.tracks[it] }
        val byTitle by lazy { index.searcher.order(LibrarySort.TITLE).map { index.tracks[it] } }
        return LibraryBrowseResult(
            tracks = rows.take(trackCount),
            albumHits =
                rows.subList(trackCount, trackCount + albumCount).map { first ->
                    LibraryGroupHit(first.album, byTitle.filter { it.album == first.album })
                },
            artistHits =
                rows.subList(trackCount + albumCount, trackCount + albumCount + artistCount).map { first ->
                    LibraryGroupHit(first.artist, byTitle.filter { it.artist == first.artist })
                },
        )
    }

    /**
     * The tracks a spoken query plays, best first, from a list that has no index yet. A blank
     * query returns the list untouched rather than nothing: an empty search box is not a search.
     */
    fun search(
        tracks: List<DeviceTrack>,
        query: String,
    ): List<DeviceTrack> = if (query.isBlank()) tracks else browse(index(tracks), query, LibrarySort.TITLE).tracks

    /** `m:ss`, or blank when the duration is unknown — better nothing than a confident `0:00`. */
    fun formatDuration(durationMs: Long): String {
        if (durationMs <= 0L) return ""
        val totalSeconds = durationMs / 1000L
        return "%d:%02d".format(totalSeconds / 60L, totalSeconds % 60L)
    }

    /** The three counts a packed search result starts with. */
    private const val COUNTS = 3
}
