package dev.geode.playback

import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import dev.geode.model.QueueTrack
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

/**
 * The queue a tap builds: which list the tapped track plays from, and which slice of a long list
 * reaches the player. Both are pure, so they are pinned here without a player.
 */
class PlaybackQueueTest {
    private fun tracks(count: Int): List<QueueTrack> = List(count) { QueueTrack("uri-$it", "Title $it") }

    private fun device(uri: String) = DeviceTrack(uri, "Device title", "Device artist", "Album", "Folder", durationMs = 1_000L)

    private fun library(uri: String) = LibraryTrack(uri, "Library title", "Library artist")

    @Test
    fun aShortListPlaysWholeFromTheTappedTrack() {
        val list = tracks(PlaybackQueue.MAX_QUEUE)
        val window = PlaybackQueue.window(list, "uri-7")
        assertSame(list, window.tracks)
        assertEquals(7, window.startIndex)
    }

    @Test
    fun anyListIsCutTheSameWayAroundAnIndex() {
        val all = List(5_000) { it }
        val (slice, index) = PlaybackQueue.around(all, 4_999)
        assertEquals(PlaybackQueue.MAX_QUEUE, slice.size)
        assertEquals(4_999, slice[index])
        val short = List(10) { it }
        assertSame(short, PlaybackQueue.around(short, 3).first)
    }

    @Test
    fun aTrackOutsideTheListPlaysAlone() {
        val window = PlaybackQueue.window(tracks(5), "elsewhere")
        assertEquals(listOf(QueueTrack("elsewhere")), window.tracks)
        assertEquals(0, window.startIndex)
    }

    @Test
    fun aLongListIsCutToAWindowCentredOnTheTappedTrack() {
        val window = PlaybackQueue.window(tracks(3_000), "uri-1500")
        assertEquals(PlaybackQueue.MAX_QUEUE, window.tracks.size)
        assertEquals("uri-1000", window.tracks.first().uri)
        assertEquals("uri-2000", window.tracks.last().uri)
        assertEquals(500, window.startIndex)
        assertEquals("uri-1500", window.tracks[window.startIndex].uri)
    }

    @Test
    fun theWindowIsClampedAtTheStartOfTheList() {
        val window = PlaybackQueue.window(tracks(3_000), "uri-10")
        assertEquals(PlaybackQueue.MAX_QUEUE, window.tracks.size)
        assertEquals("uri-0", window.tracks.first().uri)
        assertEquals(10, window.startIndex)
    }

    @Test
    fun theWindowIsClampedAtTheEndOfTheList() {
        val window = PlaybackQueue.window(tracks(3_000), "uri-2999")
        assertEquals(PlaybackQueue.MAX_QUEUE, window.tracks.size)
        assertEquals("uri-1999", window.tracks.first().uri)
        assertEquals("uri-2999", window.tracks.last().uri)
        assertEquals(PlaybackQueue.MAX_QUEUE - 1, window.startIndex)
    }

    @Test
    fun oneTrackOverTheLimitStillKeepsTheTappedTrack() {
        val window = PlaybackQueue.window(tracks(PlaybackQueue.MAX_QUEUE + 1), "uri-${PlaybackQueue.MAX_QUEUE}")
        assertEquals(PlaybackQueue.MAX_QUEUE, window.tracks.size)
        assertEquals("uri-1", window.tracks.first().uri)
        assertEquals(PlaybackQueue.MAX_QUEUE - 1, window.startIndex)
    }

    @Test
    fun theBrowsedListWinsOverTheDeviceAndTheLibrary() {
        val browse = listOf(QueueTrack("a", "Browse title"), QueueTrack("b"))
        val context = PlaybackQueue.contextFor("a", browse, listOf(device("a")), listOf(library("a")))
        assertSame(browse, context)
    }

    @Test
    fun theDeviceListWinsOverTheLibraryWhenTheBrowsedListLacksTheTrack() {
        val context = PlaybackQueue.contextFor("a", listOf(QueueTrack("b")), listOf(device("a"), device("c")), listOf(library("a")))
        assertEquals(
            listOf(QueueTrack("a", "Device title", "Device artist"), QueueTrack("c", "Device title", "Device artist")),
            context,
        )
    }

    @Test
    fun theLibraryIsUsedWhenNeitherOtherListHasTheTrack() {
        val context = PlaybackQueue.contextFor("a", emptyList(), listOf(device("c")), listOf(library("z"), library("a")))
        assertEquals(
            listOf(QueueTrack("z", "Library title", "Library artist"), QueueTrack("a", "Library title", "Library artist")),
            context,
        )
    }

    @Test
    fun aTrackInNoListPlaysAlone() {
        val context = PlaybackQueue.contextFor("a", listOf(QueueTrack("b")), listOf(device("c")), listOf(library("d")))
        assertEquals(listOf(QueueTrack("a")), context)
    }
}
