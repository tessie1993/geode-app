package dev.geode.journey

import android.content.Context
import android.media.MediaExtractor
import android.media.MediaFormat
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.media3.common.Player
import androidx.media3.session.MediaBrowser
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import com.google.common.truth.Truth.assertThat
import dev.geode.data.TakeCapture
import dev.geode.data.TakeVideo
import dev.geode.ui.MainActivity
import org.junit.After
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith
import java.nio.ByteBuffer

/**
 * Record is a screen capture of the live visual (#456, ADR 0008): a 2 s take over a playing track
 * is an MP4 with the stage's frames in a video track and the heard sound in an audio track.
 *
 * It drives the capture the way TakeController does ([TakeCapture] begin and end on a worker
 * thread), with the real stage drawing on Visuals and the real playback chain feeding the record
 * tap, rather than through the hold-to-record gesture: Record is Pro, and the gesture's red ring and
 * the paywall belong to tests of their own. Everything the gesture adds on top is TakeController,
 * which `TakeSaveTargetTest` covers on the JVM.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class CaptureTakeJourneyTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    private var browser: MediaBrowser? = null
    private var video: TakeVideo? = null

    @After
    fun release() {
        browser?.stopAndRelease()
        video?.file?.delete()
    }

    @Test
    fun aTwoSecondTakeHoldsVideoFramesAndAnAudioTrack() {
        compose.awaitEmptyPlayer()
        compose.tap("Visuals")
        compose.awaitNode(hasContentDescription("Show or hide the options"))

        val b = connectBrowser().also { browser = it }
        val playing = b.latchPlaying()
        onMain {
            b.repeatMode = Player.REPEAT_MODE_ONE
            b.setMediaItem(TestTrack.mediaItem(TestTrack.write(seconds = 6.0)))
            b.prepare()
            b.play()
        }
        awaitMain("the tone to play") { playing.get() }

        val context = ApplicationProvider.getApplicationContext<Context>()
        val capture = TakeCapture(context)
        // The stage tells native its size from the first frame it draws; on the CI emulator a new
        // surface's first frames are slow, so begin() is retried until the stage has drawn.
        val deadline = System.currentTimeMillis() + JOURNEY_TIMEOUT_MS
        var started = capture.begin()
        while (!started && System.currentTimeMillis() < deadline) {
            Thread.sleep(BEGIN_RETRY_MS)
            started = capture.begin()
        }
        assertThat(started).isTrue()
        Thread.sleep(TAKE_MS)
        val ended = capture.end().also { video = it }

        assertThat(ended).isNotNull()
        val take = checkNotNull(ended)
        assertThat(take.durationMs).isAtLeast(TAKE_MS / 2)
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(take.file.path)
            val mimes = (0 until extractor.trackCount).map { extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME).orEmpty() }
            val videoTrack = mimes.indexOfFirst { it.startsWith("video/") }
            assertThat(videoTrack).isAtLeast(0)
            assertThat(mimes.any { it.startsWith("audio/") }).isTrue()
            extractor.selectTrack(videoTrack)
            val sample = ByteBuffer.allocate(SAMPLE_BUFFER_BYTES)
            var frames = 0
            while (extractor.readSampleData(sample, 0) >= 0) {
                frames++
                extractor.advance()
            }
            assertThat(frames).isGreaterThan(0)
        } finally {
            extractor.release()
        }
    }

    private companion object {
        const val TAKE_MS = 2_000L
        const val BEGIN_RETRY_MS = 250L

        /** Enough for one encoded frame at the emulator's screen size. */
        const val SAMPLE_BUFFER_BYTES = 4 * 1024 * 1024
    }
}
