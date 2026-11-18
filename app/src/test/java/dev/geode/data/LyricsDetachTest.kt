package dev.geode.data

import android.content.Context
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class LyricsDetachTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun detachRemovesTheAttachedLrcAndOnlyThatTracks() {
        val track = Uri.parse("content://media/external/audio/media/1")
        val other = Uri.parse("content://media/external/audio/media/2")
        listOf(track, other).forEach { uri ->
            LyricsLoader.attachedFile(context, uri).apply { parentFile?.mkdirs() }.writeText("[00:01.00]line")
        }

        assertTrue(LyricsLoader.detach(context, track))
        assertFalse(LyricsLoader.attachedFile(context, track).exists())
        assertTrue(LyricsLoader.attachedFile(context, other).exists())
        assertFalse(LyricsLoader.detach(context, track))
    }
}
