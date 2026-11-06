package dev.geode.playback

import androidx.media3.common.MediaItem
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * #382: the item Media3 builds for a legacy `playFromSearch` (Android Auto voice, Assistant) has no URI, an
 * empty media id and the words in `requestMetadata.searchQuery`. Before the fix nothing read the query and the
 * item reached ExoPlayer as is; these pin which items the session now treats as a search.
 */
class VoiceSearchTest {
    private fun fromSearch(query: String?): MediaItem =
        MediaItem
            .Builder()
            .setRequestMetadata(
                MediaItem.RequestMetadata
                    .Builder()
                    .setSearchQuery(query)
                    .build(),
            ).build()

    @Test
    fun aPlayFromSearchItemIsASearch() {
        assertEquals("Radiohead", VoiceSearch.of(fromSearch("Radiohead"))?.query)
    }

    @Test
    fun anEmptyQueryPlaysTheLibrary() {
        assertEquals("", VoiceSearch.of(fromSearch(""))?.query)
        assertEquals("", VoiceSearch.of(fromSearch(null))?.query)
    }

    @Test
    fun aBrowseTreeIdIsNotASearch() {
        assertNull(VoiceSearch.of(MediaItem.Builder().setMediaId("albums/OK Computer|content://media/1").build()))
    }
}
