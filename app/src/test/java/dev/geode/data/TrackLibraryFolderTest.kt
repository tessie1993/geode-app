package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Test

class TrackLibraryFolderTest {
    @Test
    fun `rescan adds folder to an older analyzed track without losing metadata`() {
        val uri = "content://provider/document/song"
        val folder = "content://provider/document/album"
        val existing =
            LibraryTrack(
                uri = uri,
                title = "Edited title",
                artist = "Edited artist",
                analyzed = true,
                bpm = 128f,
                fileName = "song.mp3",
                sizeBytes = 100L,
            )
        val scanned =
            LibraryTrack(
                uri = uri,
                title = "Tag title",
                fileName = "song.mp3",
                sizeBytes = 100L,
                folder = folder,
            )

        val merged = TrackLibrary.mergeAdds(listOf(existing), listOf(scanned))

        assertEquals(1, merged.size)
        assertEquals(existing.copy(folder = folder), merged.single())
        assertEquals(merged, TrackLibrary.parse(TrackLibrary.serialize(merged)))
    }

    @Test
    fun `same named tracks in distinct imported folders stay distinct`() {
        val first = LibraryTrack("content://provider/a", "Song", fileName = "song.mp3", sizeBytes = 100L, folder = "content://provider/one")
        val second =
            LibraryTrack("content://provider/b", "Song", fileName = "song.mp3", sizeBytes = 100L, folder = "content://provider/two")

        assertEquals(2, TrackLibrary.mergeAdds(emptyList(), listOf(first, second)).size)
    }
}
