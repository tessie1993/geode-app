package dev.geode.playback

import androidx.media3.common.MediaItem
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Only this app's own controller queues a uri of its choosing; any other caller gets the tree's row or nothing. */
@RunWith(RobolectricTestRunner::class)
class RequestedItemTest {
    private val row = item(ID, LISTED)
    private val rowOf: (String) -> MediaItem? = { if (it == ID) row else null }

    @Test
    fun thisAppKeepsTheUriItSent() {
        val sent = item("geode:opened:1", OWN)
        assertEquals(OWN, requestedItem(sent, fromThisApp = true, rowOf).localConfiguration?.uri?.toString())
    }

    @Test
    fun anotherAppsUriIsReplacedByTheRowItsIdNames() {
        val sent = item(ID, FOREIGN)
        assertEquals(LISTED, requestedItem(sent, fromThisApp = false, rowOf).localConfiguration?.uri?.toString())
    }

    @Test
    fun anotherAppsUriWithAnUnknownIdIsLeftWithNoUri() {
        val sent = item("tracks|$FOREIGN", FOREIGN)
        val got = requestedItem(sent, fromThisApp = false, rowOf)
        assertNull(got.localConfiguration)
        assertEquals("tracks|$FOREIGN", got.mediaId)
    }

    @Test
    fun anIdOnlyRequestResolvesThroughTheTree() {
        val sent = item(ID, null)
        assertEquals(LISTED, requestedItem(sent, fromThisApp = true, rowOf).localConfiguration?.uri?.toString())
    }

    private fun item(
        id: String,
        uri: String?,
    ): MediaItem =
        MediaItem
            .Builder()
            .setMediaId(id)
            .setUri(uri)
            .build()

    private companion object {
        const val LISTED = "content://media/external/audio/media/1"
        const val ID = "tracks|$LISTED"
        const val OWN = "content://com.example.files/song.mp3"
        const val FOREIGN = "https://example.invalid/a.mp3"
    }
}
