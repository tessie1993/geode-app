package dev.geode.journey

import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import dev.geode.ui.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * A fresh install walks from the safety notice to the shell. The cheapest guard against a launch
 * crash or an onboarding step that can no longer be left.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class AppLaunchSmokeTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    /** Only the boot animation is skipped: the onboarding itself is what this test walks. */
    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded(skipOnboarding = false)).around(compose)

    @Test
    fun freshInstallReachesThePlayer() {
        compose.awaitNode(hasText("Before the visuals start"))
        compose.tap("I understand")

        compose.awaitNode(hasText("Let's find your music"))
        // "Not now" keeps the system permission dialog, a different process, out of the test.
        compose.tap("Not now")

        compose.awaitNode(hasText("Add a folder?"))
        compose.tap("Start listening")

        // Finishing setup offers Pro once. Closing it must land on the shell, not a dead end.
        compose.awaitNode(hasText("Make your music come to life"))
        compose.tap("Close")
        compose.awaitNoNode(hasText("Make your music come to life"))

        // The first-run tour always follows on a fresh install; Skip all is the shortest way to the shell.
        compose.awaitNode(hasText("Skip all"))
        compose.tap("Skip all")
        compose.awaitNoNode(hasText("Skip all"))

        compose.awaitEmptyPlayer()
    }
}
