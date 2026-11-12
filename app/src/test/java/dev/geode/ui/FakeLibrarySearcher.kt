package dev.geode.ui

import dev.geode.model.DeviceTrack
import java.text.Normalizer
import java.util.Locale

/**
 * A stand-in for the native index, so JVM tests never load libgeode: a plain accent-folded
 * substring match in library order. The real match and ranking are pinned on the host by
 * `core/tests/library_search_test.cpp`; tests here only check what Kotlin does with the rows.
 */
class FakeLibrarySearcher(
    private val tracks: List<DeviceTrack>,
) : LibrarySearcher {
    var searches = 0
        private set

    override fun order(sort: LibrarySort): IntArray {
        val rows = tracks.indices
        val sorted =
            when (sort) {
                LibrarySort.TITLE -> rows.sortedBy { fold(tracks[it].title) }
                LibrarySort.ARTIST -> rows.sortedBy { fold(tracks[it].artist) }
                LibrarySort.ALBUM -> rows.sortedBy { fold(tracks[it].album) }
                LibrarySort.DURATION -> rows.sortedBy { tracks[it].durationMs }
                LibrarySort.ADDED -> rows.sortedByDescending { tracks[it].addedSec }
            }
        return sorted.toIntArray()
    }

    override fun search(query: String): IntArray {
        searches++
        val term = fold(query.trim())
        val hits = tracks.indices.filter { hit(it, term) { t -> listOf(t.title, t.artist, t.album) } }
        val albums = tracks.indices.filter { hit(it, term) { t -> listOf(t.album) } }.distinctBy { tracks[it].album }
        val artists = tracks.indices.filter { hit(it, term) { t -> listOf(t.artist) } }.distinctBy { tracks[it].artist }
        return intArrayOf(hits.size, albums.size, artists.size) + hits + albums + artists
    }

    private fun hit(
        row: Int,
        term: String,
        fields: (DeviceTrack) -> List<String>,
    ): Boolean = fields(tracks[row]).any { fold(it).contains(term) }

    /** Lower case with the accents dropped, enough for the tests' tags ("Beyoncé", "Élan"). */
    private fun fold(text: String): String =
        Normalizer.normalize(text, Normalizer.Form.NFD).replace(MARKS, "").lowercase(Locale.ROOT)

    private companion object {
        val MARKS = Regex("\\p{Mn}+")
    }
}
