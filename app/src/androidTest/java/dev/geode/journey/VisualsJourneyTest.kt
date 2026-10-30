package dev.geode.journey

import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isToggleable
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.espresso.Espresso
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import dev.geode.ui.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * The Visuals tab: the live view opens from it and gives the user back to it, and the
 * Customize tab's switches work. QA numbers refer to docs/qa/emulator-pass-2026-10-30.md.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class VisualsJourneyTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    private fun openVisuals() {
        compose.awaitNode(hasText("Nothing playing"))
        compose.tap("Visuals")
        compose.awaitNode(hasText("View live"))
    }

    private fun openLiveView() {
        openVisuals()
        compose.tap("View live")
        compose.awaitNode(hasContentDescription("Collapse"))
    }

    /** QA #11: Collapse must hand the user back to the tab they came from. */
    @Test
    fun collapseReturnsToVisuals() {
        openLiveView()
        compose.tap("Collapse")
        compose.awaitNoNode(hasContentDescription("Collapse"))
        compose.awaitNode(hasText("View live"))
    }

    /** QA #11, the system back route: the live view's dismiss must win over the shell's reset to Player. */
    @Test
    fun backFromLiveViewReturnsToVisuals() {
        openLiveView()
        Espresso.pressBack()
        compose.awaitNoNode(hasContentDescription("Collapse"))
        compose.awaitNode(hasText("View live"))
    }

    private fun openCustomizeShape() {
        openVisuals()
        compose.tap("Customize")
        compose.tap("Shape")
    }

    /** The whole CheckRow is the toggleable node (role Checkbox), its label merged into it. */
    private val kaleidoscopeBox = isToggleable() and hasText("Kaleidoscope")

    @Test
    fun kaleidoscopeCheckboxTogglesAndRevealsFolds() {
        openCustomizeShape()
        compose.awaitNode(kaleidoscopeBox)
        val box = compose.onAllNodes(kaleidoscopeBox).onFirst()
        box.performScrollTo()
        box.assertIsOff()
        box.performClick()
        box.assertIsOn()
        compose.awaitNode(hasText("Folds"))
    }

    /**
     * QA #13: the label used to be a separate, unclickable Text beside the Checkbox, so tapping
     * the word (or an accessibility click on it) did nothing. The row is toggleable now.
     */
    @Test
    fun tappingTheKaleidoscopeLabelTogglesIt() {
        openCustomizeShape()
        compose.awaitNode(hasText("Kaleidoscope"))
        compose.tap("Kaleidoscope")
        compose.awaitNode(hasText("Folds"))
    }
}
