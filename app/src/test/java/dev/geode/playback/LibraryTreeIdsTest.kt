package dev.geode.playback

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

/** A folder id under which a playable id `<parentId>|<uri>` still splits on its first `|`, whatever the name. */
class LibraryTreeIdsTest {
    @Test
    fun aNameWithTheSeparatorStillSplitsAtTheUri() {
        val parent = LibraryTree.folderId(LibraryTree.ALBUMS, "Live | Unplugged")
        assertFalse('|' in parent)
        val playable = "$parent|content://media/1"
        assertEquals("content://media/1", playable.substringAfter('|'))
        assertEquals("Live | Unplugged", LibraryTree.folderName(playable.substringBefore('|').substringAfter('/')))
    }

    @Test
    fun aNameWithoutSpecialCharactersKeepsItsId() {
        assertEquals("artists/OK Computer", LibraryTree.folderId(LibraryTree.ARTISTS, "OK Computer"))
        assertEquals("OK Computer", LibraryTree.folderName("OK Computer"))
    }

    @Test
    fun percentSignsRoundTrip() {
        for (name in listOf("100% Hits", "a%7Cb", "%25|%", "|")) {
            assertEquals(name, LibraryTree.folderName(LibraryTree.folderId(LibraryTree.PLAYLISTS, name).substringAfter('/')))
        }
    }
}
