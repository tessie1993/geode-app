package dev.geode.ui

import dev.geode.model.DeviceTrack
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
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

    private fun index(tracks: List<DeviceTrack>) = LibraryBrowse.index(tracks) { FakeLibrarySearcher(it) }

    @Test
    fun blankQueryIsTheWholeLibraryInTheOrderAskedGroupedForTheTabs() {
        val tracks =
            listOf(
                track("2", "Élan", artist = "B", album = "Y", folder = "/b"),
                track("1", "Dawn", artist = "A", album = "X"),
                track("3", "Fall", artist = "A", album = "Y"),
            )
        val result = LibraryBrowse.browse(index(tracks), "  ", LibrarySort.TITLE)
        assertEquals(listOf("1", "2", "3"), result.tracks.map { it.uri })
        assertEquals(setOf("X", "Y"), result.albums.keys)
        assertEquals(listOf("2", "3"), result.albums.getValue("Y").map { it.uri })
        assertEquals(setOf("A", "B"), result.artists.keys)
        assertEquals(setOf("/music", "/b"), result.folders.keys)
        assertTrue(result.albumHits.isEmpty() && result.artistHits.isEmpty())
    }

    @Test
    fun aSearchKeepsTheIndexsOrderWhateverTheSort() {
        val tracks = listOf(track("b", "Love B"), track("a", "Love A"), track("x", "Other"))
        val result = LibraryBrowse.browse(index(tracks), "love", LibrarySort.DURATION)
        assertEquals(listOf("b", "a"), result.tracks.map { it.uri })
        assertTrue(result.albums.isEmpty() && result.artists.isEmpty() && result.folders.isEmpty())
    }

    @Test
    fun aSearchNamesItsArtistsAndAlbumsWithEveryTrackOfThem() {
        val tracks =
            listOf(
                track("1", "Run", artist = "Beyoncé", album = "Fierce"),
                track("2", "Halo", artist = "Beyoncé", album = "Fierce"),
                track("3", "Hello", artist = "Adele", album = "25"),
            )
        val byArtist = LibraryBrowse.browse(index(tracks), "beyonce", LibrarySort.TITLE)
        assertEquals(listOf("1", "2"), byArtist.tracks.map { it.uri })
        assertEquals(listOf("Beyoncé"), byArtist.artistHits.map { it.name })
        // The whole artist, in title order, not only the hits.
        assertEquals(listOf("2", "1"), byArtist.artistHits.single().tracks.map { it.uri })
        assertTrue(byArtist.albumHits.isEmpty())

        val byAlbum = LibraryBrowse.browse(index(tracks), "fierce", LibrarySort.TITLE)
        assertEquals(listOf("Fierce"), byAlbum.albumHits.map { it.name })
        assertEquals(listOf("2", "1"), byAlbum.albumHits.single().tracks.map { it.uri })
    }

    @Test
    fun nothingFoundIsNoTrackAlbumOrArtist() {
        val tracks = listOf(track("1", "Halo"))
        assertTrue(LibraryBrowse.browse(index(tracks), "zzz", LibrarySort.TITLE).nothingFound)
        assertFalse(LibraryBrowse.browse(index(tracks), "halo", LibrarySort.TITLE).nothingFound)
    }

    @Test
    fun theIndexIsNotBuiltUntilAsked() {
        var built = 0
        val index =
            LibraryBrowse.index(listOf(track("1", "Halo"))) {
                built++
                FakeLibrarySearcher(it)
            }
        assertEquals(0, built)
        LibraryBrowse.browse(index, "", LibrarySort.TITLE)
        LibraryBrowse.browse(index, "halo", LibrarySort.TITLE)
        assertEquals(1, built)
    }

    @Test
    fun favouritesAreTheLibrarysTracksNewestFirstWithoutTheOnesGone() {
        val tracks = listOf(track("a", "A"), track("b", "B"), track("c", "C"))
        // The store's order (newest first) is kept; "gone" was deleted from the device.
        val favourites = LibraryBrowse.favouriteTracks(listOf("c", "gone", "a"), tracks)
        assertEquals(listOf("c", "a"), favourites.map { it.uri })
        assertTrue(LibraryBrowse.favouriteTracks(emptySet(), tracks).isEmpty())
    }
}
