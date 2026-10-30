package dev.geode.journey

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onFirst
import androidx.media3.common.Player
import androidx.media3.session.MediaBrowser
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import com.google.common.truth.Truth.assertThat
import dev.geode.ui.MainActivity
import org.junit.After
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * The Player tab's transport. A track is queued through PlaybackService rather than the
 * library, which keeps storage permission and MediaStore contents out of the test; the screen
 * drives the same process-wide player either way. QA numbers refer to
 * docs/qa/emulator-pass-2026-10-30.md.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class PlayerJourneyTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    private var browser: MediaBrowser? = null

    @After
    fun release() {
        browser?.stopAndRelease()
    }

    /** QA #5: ExoPlayer ignores play() in STATE_ENDED; the Play button must restart the track. */
    @Test
    fun playRestartsATrackThatHasEnded() {
        compose.awaitEmptyPlayer()
        val b = connectBrowser().also { browser = it }
        val item = TestTrack.mediaItem(TestTrack.write())
        onMain {
            b.repeatMode = Player.REPEAT_MODE_OFF
            b.setMediaItem(item)
            b.prepare()
            b.play()
        }
        awaitMain("the track to end", timeoutMs = 3 * JOURNEY_TIMEOUT_MS) {
            b.playbackState == Player.STATE_ENDED
        }

        // Latched from a listener: the restarted track plays for only three seconds.
        val restarted = b.latchPlaying()
        compose.tap("Play")

        awaitMain("Play to restart the ended track") { restarted.get() }
    }

    /**
     * QA #14: Repeat cycled off, all, one with the same icon in two of them and a description
     * that never changed, so TalkBack (and the user) couldn't tell the modes apart. The mode
     * must be in the content or state description.
     */
    @Test
    fun repeatButtonNamesItsMode() {
        compose.awaitEmptyPlayer()
        // The transport row, Repeat included, is only drawn once something is queued.
        val b = connectBrowser().also { browser = it }
        onMain {
            b.setMediaItem(TestTrack.mediaItem(TestTrack.write()))
            b.prepare()
        }
        val repeat = hasContentDescription("Repeat", substring = true) and hasClickAction()
        val spoken = mutableListOf<String>()
        repeat(3) {
            // tap() scrolls first: on a phone the transport row sits below the fold, and a click
            // on an off-screen node lands outside the window.
            compose.tap(repeat)
            compose.waitForIdle()
            val config =
                compose
                    .onAllNodes(repeat)
                    .onFirst()
                    .fetchSemanticsNode()
                    .config
            val descriptions = config.getOrElse(SemanticsProperties.ContentDescription) { emptyList() }
            val state = listOfNotNull(config.getOrNull(SemanticsProperties.StateDescription))
            spoken += (descriptions + state).joinToString(" ")
        }
        // OFF → ALL → ONE → OFF: three different modes, three different descriptions.
        assertThat(spoken.toSet()).hasSize(3)
    }
}
