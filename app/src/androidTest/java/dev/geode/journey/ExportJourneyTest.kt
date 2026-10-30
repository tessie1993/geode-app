package dev.geode.journey

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.performScrollTo
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import dev.geode.ui.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * QA #18 (docs/qa/emulator-pass-2026-10-30.md): on a phone the Export dialog's options pushed
 * its Start button below the dialog's edge, so a render could not be started. The dialog now
 * scrolls; this checks the button can be brought on screen. It does not start a render: with
 * nothing queued the button is disabled, and a render belongs in an export test of its own.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class ExportJourneyTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    @Test
    fun exportDialogStartButtonIsReachable() {
        compose.awaitNode(hasText("Nothing playing"))
        compose.tap("Settings")
        compose.tap("Export")
        compose.tap("Export video…")
        compose.tap("Video export…")

        // "Render 1080p MP4 30fps"; not "Render to chosen folder…", and not the fps chips.
        val start = hasTextMatching(Regex("""Render \d+p .+ \d+fps""")) and hasClickAction()
        compose.awaitNode(start)
        compose.onAllNodes(start).onFirst().performScrollTo().assertIsDisplayed()
        compose.onAllNodes(hasText("Close") and hasClickAction()).onFirst().performScrollTo().assertIsDisplayed()
    }
}
