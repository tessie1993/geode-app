package dev.geode.journey

import androidx.media3.common.Player
import androidx.media3.session.MediaBrowser
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import com.google.common.truth.Truth.assertThat
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.TimeUnit

/**
 * PlaybackService seen from outside, through the same MediaBrowser API Android Auto and media
 * buttons use: it accepts a controller, browses its library tree, and plays a track to the end.
 * No activity is launched; the service builds the process-wide PlaybackEngine on its own.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class PlaybackServiceTest {
    private lateinit var browser: MediaBrowser

    @Before
    fun connect() {
        browser = connectBrowser()
    }

    @After
    fun release() {
        browser.stopAndRelease()
    }

    @Test
    fun libraryRootListsTheBrowseCategories() {
        val root = onMain { browser.getLibraryRoot(null) }.get(JOURNEY_TIMEOUT_MS, TimeUnit.MILLISECONDS)
        assertThat(root.value?.mediaId).isEqualTo("root")

        val children =
            onMain { browser.getChildren("root", 0, Int.MAX_VALUE, null) }
                .get(JOURNEY_TIMEOUT_MS, TimeUnit.MILLISECONDS)
        assertThat(children.value.orEmpty().map { it.mediaId })
            .containsAtLeast("tracks", "albums", "artists", "playlists")
    }

    @Test
    fun playsATrackToTheEnd() {
        val item = TestTrack.mediaItem(TestTrack.write())
        val played = browser.latchPlaying()
        onMain {
            browser.repeatMode = Player.REPEAT_MODE_OFF
            browser.setMediaItem(item)
            browser.prepare()
            browser.play()
        }
        awaitMain("the track to end", timeoutMs = 3 * JOURNEY_TIMEOUT_MS) {
            browser.playbackState == Player.STATE_ENDED
        }
        assertThat(played.get()).isTrue()
        assertThat(onMain { browser.isPlaying }).isFalse()
    }
}
