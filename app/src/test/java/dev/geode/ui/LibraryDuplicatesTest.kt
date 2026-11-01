package dev.geode.ui

import dev.geode.model.DeviceTrack
import org.junit.Assert.assertEquals
import org.junit.Test

class LibraryDuplicatesTest {
    private fun track(
        uri: String,
        durationMs: Long,
    ) = DeviceTrack(uri = uri, title = "Song", artist = "Band", album = "", folder = uri, durationMs = durationMs)

    @Test
    fun oneSongSplitByLengthGivesGroupsWithDistinctKeys() {
        val tracks =
            listOf(
                track("content://a", 180_000),
                track("content://b", 180_500),
                track("content://c", 240_000),
                track("content://d", 240_900),
            )
        val groups = LibraryDuplicates.find(tracks)
        assertEquals(2, groups.size)
        assertEquals(groups[0].title to groups[0].artist, groups[1].title to groups[1].artist)
        val keys = groups.map { it.key } + groups.flatMap { group -> group.tracks.map { it.uri } }
        assertEquals(keys.size, keys.toSet().size)
    }
}
