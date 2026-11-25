package dev.geode.playback

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * A controller's media id resolves only to a row the folder it names lists: the uri inside the id is
 * never opened on its own, so a browsing app cannot make the player fetch or read a uri of its choosing.
 */
@RunWith(RobolectricTestRunner::class)
class LibraryTreePlayableTest {
    private val tree =
        LibraryTree(ApplicationProvider.getApplicationContext<Context>()) {
            listOf(LibraryTree.Row(LISTED, "Title", "Artist", "Album"))
        }

    @Test
    fun aListedTrackResolvesToItsOwnUri() {
        val item = tree.playable("${LibraryTree.TRACKS}|$LISTED")
        assertEquals(LISTED, item?.localConfiguration?.uri?.toString())
        assertEquals("Title", item?.mediaMetadata?.title?.toString())
        assertEquals("$ALBUM|$LISTED", tree.item("$ALBUM|$LISTED")?.mediaId)
    }

    @Test
    fun aSearchResultResolvesAmongTheLibrarysTracks() {
        val found = tree.playable("$SEARCH|$LISTED")
        assertEquals(LISTED, found?.localConfiguration?.uri?.toString())
        for (uri in UNLISTED) assertNull(uri, tree.playable("$SEARCH|$uri"))
    }

    @Test
    fun aUriTheLibraryDoesNotListResolvesToNothing() {
        for (uri in UNLISTED) {
            assertNull(uri, tree.playable("${LibraryTree.TRACKS}|$uri"))
            assertNull(uri, tree.item("$ALBUM|$uri"))
        }
    }

    private companion object {
        const val LISTED = "content://media/external/audio/media/1"
        val ALBUM = LibraryTree.folderId(LibraryTree.ALBUMS, "Album")
        const val SEARCH = "${LibraryTree.SEARCH}/title"
        val UNLISTED =
            listOf(
                "https://example.invalid/a.mp3",
                "file:///data/user/0/dev.geode/shared_prefs/geode-prefs.xml",
                "content://dev.geode.presets/presets/a.json",
                "content://media/external/audio/media/2",
            )
    }
}
