package dev.geode.journey

import android.Manifest
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.lifecycle.Lifecycle
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.rule.GrantPermissionRule
import dev.geode.ui.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/** Every tab of the shell opens, for a user past onboarding who let the app see their music. */
@LargeTest
@RunWith(AndroidJUnit4::class)
class ShellTabsTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    /**
     * Music access, granted up front as a user who said yes would have: the Library tab asks for
     * it otherwise, and the system dialog is another app's window that no Compose finder sees.
     */
    @get:Rule
    val music: GrantPermissionRule = GrantPermissionRule.grant(Manifest.permission.READ_MEDIA_AUDIO)

    @Test
    fun everyTabOpens() {
        // Seeded past onboarding: the shell starts on the Player tab with an empty queue.
        compose.awaitEmptyPlayer()

        compose.tap("Library")
        compose.awaitNoNode(hasText("Pick something to play"))

        compose.tap("Visuals")
        compose.awaitNode(hasContentDescription("Show or hide the options"))

        // Default intent is BOTH, which shows Studio (behind its Pro gate for a free user).
        compose.tap("Studio")
        compose.waitForIdle()

        // Settings is a list of groups; a group opens as its page, ‹ › step to the next one and ←
        // returns to the list. The list is lazy, so the wait names a group near its top: Export,
        // eighth of ten, sits below the fold on a phone-sized emulator and is never composed.
        compose.tap("Settings")
        compose.awaitNode(hasText("Behaviour & gestures"))
        compose.tap("Behaviour & gestures")
        compose.awaitNode(hasText("Open on"))
        compose.tap("Next group")
        // Visuals' Basics: the heading is a CrystalOverline, which draws its text in capitals.
        compose.awaitNode(hasText("Flashing and motion", ignoreCase = true))
        compose.tap("Back to Settings")
        compose.awaitNode(hasText("Behaviour & gestures"))

        compose.tap("Player")
        compose.awaitEmptyPlayer()
    }

    /**
     * Coming back to the app finds the tab bar on Visuals, however long it was away: the chrome's
     * sleep timer used to run out in the background, and with no track loaded the bar faded even
     * under the open options.
     */
    @Test
    fun theTabBarIsThereWhenTheAppComesBack() {
        compose.awaitEmptyPlayer()
        compose.tap("Visuals")
        compose.awaitNode(hasContentDescription("Show or hide the options"))

        compose.activityRule.scenario.moveToState(Lifecycle.State.CREATED)
        compose.activityRule.scenario.moveToState(Lifecycle.State.RESUMED)
        compose.awaitNode(hasText("Library") and hasClickAction())

        // Past the default 6 s fade: the options are up, so the bar stays.
        Thread.sleep(FADE_AND_MORE_MS)
        compose.awaitNode(hasText("Library") and hasClickAction())
        compose.tap("Library")
        compose.awaitNoNode(hasText("Pick something to play"))
    }

    private companion object {
        const val FADE_AND_MORE_MS = 8_000L
    }
}
