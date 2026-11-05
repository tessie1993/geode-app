package dev.geode.ui.tour

import android.content.Context
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.R
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * #330 on the device: a tap on › says "you can swipe too" once, and never again once read.
 *
 * The › here is a stand-in that calls [GestureTips.nudge] exactly as `VisualsStage`'s arrow does,
 * drawn over the real [GestureTipHost] (the one `TutorialHost` draws) and the real preferences
 * file. The whole Visuals stage is not driven: its arrows are enabled only while the stage chrome
 * is awake, which fades on a timer, and it brings up the GL surface, whose first frames stall the
 * emulator's main thread for seconds (see `journey/Journeys.kt`). The counting itself is pinned on
 * the JVM by `GestureTipsTest`.
 */
@RunWith(AndroidJUnit4::class)
class GestureTipHostTest {
    @get:Rule
    val compose = createComposeRule()

    private val context: Context = ApplicationProvider.getApplicationContext()
    private val next = context.getString(R.string.visuals_next_style)
    private val tip = context.getString(R.string.tour_tip_swipe_styles)
    private val gotIt = context.getString(R.string.tour_tip_got_it)

    @Before
    fun freshTips() {
        GestureTips.detach()
        // With nothing attached, reset clears the file on disk, so no earlier run's tick leaks in.
        GestureTips.reset(context)
        runBlocking { GestureTips.attach(context) }
        compose.setContent {
            MaterialTheme {
                Box(Modifier.fillMaxSize()) {
                    TextButton(
                        onClick = { GestureTips.nudge(GestureTip.SWIPE_STYLES) },
                        modifier = Modifier.align(Alignment.BottomCenter),
                    ) { Text(next) }
                    GestureTipHost()
                }
            }
        }
    }

    @After
    fun forget() = GestureTips.detach()

    @Test
    fun tappingNextShowsTheSwipeTipOnce() {
        compose.onNodeWithText(tip).assertDoesNotExist()
        compose.onNodeWithText(next).performClick()
        compose.onNodeWithText(tip).assertIsDisplayed()
        compose.onNodeWithText(gotIt).performClick()
        compose.onNodeWithText(tip).assertDoesNotExist()
        repeat(2) {
            compose.onNodeWithText(next).performClick()
            compose.onNodeWithText(tip).assertDoesNotExist()
        }
    }

    /**
     * A tip left alone goes after its few seconds and counts as read. The test clock drives the
     * host's delay (developer.android.com/develop/ui/compose/testing/synchronization).
     */
    @Test
    fun aTipThatTimesOutIsNotShownAgain() {
        compose.onNodeWithText(next).performClick()
        compose.onNodeWithText(tip).assertIsDisplayed()
        compose.mainClock.advanceTimeBy(TIP_TIMEOUT_MS)
        compose.onNodeWithText(tip).assertDoesNotExist()
        compose.onNodeWithText(next).performClick()
        compose.onNodeWithText(tip).assertDoesNotExist()
    }

    private companion object {
        /** `GestureTips`' 7 s, plus a few frames for the dismissal to recompose. */
        const val TIP_TIMEOUT_MS = 7_100L
    }
}
