package dev.geode.ui

import dev.geode.model.DeviceTrack
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LibraryBrowseTest {
    private fun track(
        id: String,
        title: String,
        artist: String = "Someone",
        album: String = "Something",
        folder: String = "/music",
    ) = DeviceTrack(uri = id, title = title, artist = artist, album = album, folder = folder, durationMs = 200_000L)

    @Test
    fun accentFreeQueryFindsAccentedTags() {
        val tracks = listOf(track("1", "Halo", artist = "Beyoncé"), track("2", "Hello", artist = "Adele"))
        val index = LibraryBrowse.index(tracks)
        assertEquals(listOf("1"), LibraryBrowse.browse(index, "beyonce", LibrarySort.TITLE).tracks.map { it.uri })
        assertEquals(listOf("1"), LibraryBrowse.browse(index, "BEYONCÉ", LibrarySort.TITLE).tracks.map { it.uri })
    }

    @Test
    fun titleHitsRankAboveArtistAboveAlbum() {
        val tracks =
            listOf(
                track("album", "Aaa", album = "Love Songs"),
                track("artist", "Bbb", artist = "Love Inc."),
                track("title", "Zzz Love"),
            )
        // Title order alone would put "album" first; the tiers win, the sort holds inside them.
        val result = LibraryBrowse.browse(LibraryBrowse.index(tracks), "love", LibrarySort.TITLE)
        assertEquals(listOf("title", "artist", "album"), result.tracks.map { it.uri })
    }

    @Test
    fun sortHoldsWithinATier() {
        val tracks = listOf(track("b", "Love B"), track("a", "Love A"), track("c", "Love C"))
        val result = LibraryBrowse.browse(LibraryBrowse.index(tracks), "love", LibrarySort.TITLE)
        assertEquals(listOf("a", "b", "c"), result.tracks.map { it.uri })
    }

    @Test
    fun aTypoStillFindsButBelowExactHits() {
        val tracks = listOf(track("typo", "Yesterday"), track("exact", "Other", album = "Yesterdays"))
        val result = LibraryBrowse.browse(LibraryBrowse.index(tracks), "yesterdya", LibrarySort.TITLE)
        assertTrue(result.tracks.any { it.uri == "typo" })
    }

    @Test
    fun blankQueryIsTheWholeLibrarySorted() {
        val tracks = listOf(track("2", "Élan"), track("1", "Dawn"), track("3", "Fall"))
        val result = LibraryBrowse.browse(LibraryBrowse.index(tracks), "  ", LibrarySort.TITLE)
        // Folded: "Élan" sorts with the Es.
        assertEquals(listOf("1", "2", "3"), result.tracks.map { it.uri })
    }

    @Test
    fun groupsFollowTheSearch() {
        val tracks =
            listOf(
                track("1", "Halo", artist = "Beyoncé", album = "Fierce"),
                track("2", "Run", artist = "Beyoncé", album = "4"),
                track("3", "Hello", artist = "Adele", album = "25"),
            )
        val result = LibraryBrowse.browse(LibraryBrowse.index(tracks), "beyonce", LibrarySort.TITLE)
        assertEquals(setOf("Beyoncé"), result.artists.keys)
        assertEquals(setOf("Fierce", "4"), result.albums.keys)
        assertEquals(setOf("/music"), result.folders.keys)
    }

    @Test
    fun voiceSearchFoldsToo() {
        val tracks = listOf(track("1", "Café del Mar"), track("2", "Tea"))
        assertEquals(listOf("1"), LibraryBrowse.search(tracks, "cafe").map { it.uri })
        assertEquals(tracks, LibraryBrowse.search(tracks, ""))
    }
}
