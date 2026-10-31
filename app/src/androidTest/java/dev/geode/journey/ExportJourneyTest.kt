package dev.geode.journey

import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onFirst
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import dev.geode.ui.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * QA #18 (docs/qa/emulator-pass-2026-10-30.md): on a phone the old Export dialog's options pushed
 * its Start button below the dialog's edge, so a render could not be started. Studio › Render is
 * now one page whose options scroll above a pinned RenderBar; this checks the bar's Render button
 * is on screen. It does not start a render: with nothing playing the button is disabled, and a
 * render belongs in an export test of its own.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class ExportJourneyTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    @Test
    fun renderButtonIsReachable() {
        compose.awaitEmptyPlayer()
        compose.tap("Studio")
        compose.tap("Render")

        // The bar's button, not the "Render" tab above the page (a Tab has Role.Tab).
        val button = hasText("Render") and SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button)
        compose.awaitNode(button)
        compose.onAllNodes(button).onFirst().assertIsDisplayed()
    }
}
