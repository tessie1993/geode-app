package dev.geode.journey

import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isToggleable
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import dev.geode.ui.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * The Visuals tab: the Customize tab's switches work, and Presets saves the current look.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class VisualsJourneyTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    private fun openVisuals() {
        compose.awaitEmptyPlayer()
        compose.tap("Visuals")
        compose.awaitNode(hasContentDescription("Show or hide the options"))
    }

    /**
     * Kaleidoscope is one of Shape's Basics, open at the top of the category (issue #333). Not
     * through Customize's search: with a query, the results page never let Compose go idle on the
     * CI emulator, and every later step waited for it until the job timed out.
     */
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

    /**
     * Presets has one save path: "Save current look" opens a dialog, and the saved look shows as
     * a card, under Mine too.
     */
    @Test
    fun savingTheCurrentLookAddsAPresetCard() {
        openVisuals()
        compose.tap("Presets")
        compose.tap("Save current look")
        compose.awaitNode(hasSetTextAction())
        compose.onNode(hasSetTextAction()).performTextInput("Journey look")
        compose.tap("Save")
        compose.awaitNode(hasText("Journey look"))
        compose.tap("Mine")
        compose.awaitNode(hasText("Journey look"))
    }
}
