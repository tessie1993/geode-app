package dev.geode.widget

import android.content.Context
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.exoplayer.ExoPlayer
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** With no widget placed the widget state is not written, yet the screen saver still reads the track playing now. */
@RunWith(RobolectricTestRunner::class)
class WidgetPublisherNowPlayingTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun theTrackIsKeptWithNoWidgetPlaced() {
        val player = ExoPlayer.Builder(context).build()
        try {
            val item =
                MediaItem
                    .Builder()
                    .setUri("content://media/external/audio/media/7")
                    .setMediaMetadata(
                        MediaMetadata
                            .Builder()
                            .setTitle("Song")
                            .setArtist("Artist")
                            .build(),
                    ).build()
            player.setMediaItem(item)
            val publisher = WidgetPublisher(context, player)
            publisher.publish()
            assertEquals("Song", WidgetPublisher.nowPlaying?.title)
            assertEquals("Artist", WidgetPublisher.nowPlaying?.artist)
            assertFalse(WidgetPublisher.nowPlaying?.playing ?: true)
        } finally {
            player.release()
        }
    }
}
