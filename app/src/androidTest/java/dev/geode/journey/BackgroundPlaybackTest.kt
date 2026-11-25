package dev.geode.journey

import android.app.Notification
import android.app.NotificationManager
import android.content.Context
import androidx.compose.ui.test.junit4.createEmptyComposeRule
import androidx.media3.common.Player
import androidx.media3.session.MediaBrowser
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.platform.app.InstrumentationRegistry
import com.google.common.truth.Truth.assertThat
import dev.geode.ui.MainActivity
import org.junit.After
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * Music keeps playing when the user leaves: Play pressed in the app, then the activity finished
 * (Back out of the app, or the system destroying it). PlaybackService must already be a foreground
 * service with its media notification, and the player must still be moving once the activity is
 * gone. The notification needs no POST_NOTIFICATIONS grant: media session notifications are
 * exempt (developer.android.com/develop/ui/compose/notifications/notification-permission#exemptions).
 *
 * The browser only queues the track and is released before Play, so no bound controller keeps the
 * service alive in the test's place; the second browser connects after the activity is gone.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class BackgroundPlaybackTest {
    private val compose = createEmptyComposeRule()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    private val context = ApplicationProvider.getApplicationContext<Context>()
    private var browser: MediaBrowser? = null

    @After
    fun release() {
        browser?.stopAndRelease()
    }

    @Test
    fun playbackOutlivesTheActivity() {
        val scenario = ActivityScenario.launch(MainActivity::class.java)
        compose.awaitEmptyPlayer()

        // Repeat one: the track must still be playing after the activity has gone, however slowly
        // the emulator gets there.
        val item = TestTrack.mediaItem(TestTrack.write())
        val queue = connectBrowser()
        onMain {
            queue.repeatMode = Player.REPEAT_MODE_ONE
            queue.setMediaItem(item)
            queue.prepare()
        }
        awaitMain("the track to load") { queue.playbackState == Player.STATE_READY }
        onMain { queue.release() }

        compose.tap("Play")
        awaitMain("PlaybackService's foreground media notification") { foregroundMediaNotification() }

        scenario.close()
        // Before any new browser binds: binding would re-create a stopped service and could post
        // the notification again, hiding a teardown that dropped it.
        InstrumentationRegistry.getInstrumentation().waitForIdleSync()
        assertThat(onMain { foregroundMediaNotification() }).isTrue()

        val b = connectBrowser().also { browser = it }
        val atClose = onMain { b.currentPosition }
        awaitMain("playback to keep moving after the activity was destroyed") {
            // Not "past": with repeat one the position wraps, and a later poll can land below it.
            b.isPlaying && b.currentPosition != atClose
        }
        assertThat(onMain { foregroundMediaNotification() }).isTrue()
    }

    /** The app's own posted notifications: a media-session one from a foreground service. */
    private fun foregroundMediaNotification(): Boolean =
        context.getSystemService(NotificationManager::class.java).activeNotifications.any {
            val n = it.notification
            n.flags and Notification.FLAG_FOREGROUND_SERVICE != 0 &&
                n.extras.containsKey(Notification.EXTRA_MEDIA_SESSION)
        }
}
