package dev.geode.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.performSemanticsAction
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * TalkBack's view of the app's slider (#996): a labelled slider is one stop that reads its row's
 * line, name with value and unit, and the line itself is not read a second time.
 */
@RunWith(AndroidJUnit4::class)
class CrystalSliderTest {
    @get:Rule
    val compose = createComposeRule()

    private var speed by mutableFloatStateOf(1f)

    private fun showSpeed() {
        compose.setContent {
            MaterialTheme {
                CrystalSlider(
                    label = "Speed  %.2fx".format(speed),
                    value = speed,
                    onValueChange = { speed = it },
                    valueRange = 0.5f..2f,
                    accessibilityStep = 0.05f,
                )
            }
        }
    }

    private fun stateIs(text: String) = SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, text)

    @Test
    fun labelledSliderReadsItsLineOnce() {
        showSpeed()
        compose.onNode(stateIs("Speed  1.00x")).assertExists()
        // The drawn line is cleared from semantics, so TalkBack does not stop on it as well.
        compose.onAllNodesWithText("Speed  1.00x", useUnmergedTree = true).assertCountEquals(0)
    }

    @Test
    fun accessibilityStepSetsTheSwipeIncrement() {
        showSpeed()
        val range = compose.onNode(stateIs("Speed  1.00x")).fetchSemanticsNode().config[SemanticsProperties.ProgressBarRangeInfo]
        // 0.5..2 in 0.05 steps: 30 intervals, 29 stops between the ends.
        assertEquals(29, range.steps)
        assertEquals(0.5f..2f, range.range)
    }

    @Test
    fun settingProgressUpdatesTheValueAndItsDescription() {
        showSpeed()
        compose.onNode(stateIs("Speed  1.00x")).performSemanticsAction(SemanticsActions.SetProgress) { it(1.5f) }
        compose.runOnIdle { assertEquals(1.5f, speed, 1e-4f) }
        compose.onNode(stateIs("Speed  1.50x")).assertExists()
    }

    @Test
    fun valueDescriptionWithoutLabelDrawsNoLine() {
        compose.setContent {
            MaterialTheme {
                CrystalSlider(value = 0.5f, onValueChange = {}, valueDescription = "Dim: 50%")
            }
        }
        compose.onNode(stateIs("Dim: 50%")).assertExists()
        compose.onAllNodesWithText("Dim: 50%", useUnmergedTree = true).assertCountEquals(0)
    }

    @Test
    fun unlabelledSliderKeepsMaterialsDefaults() {
        compose.setContent {
            MaterialTheme {
                CrystalSlider(value = 0.5f, onValueChange = {})
            }
        }
        val node = compose.onNode(SemanticsMatcher.keyIsDefined(SemanticsProperties.ProgressBarRangeInfo)).fetchSemanticsNode()
        assertNull(node.config.getOrNull(SemanticsProperties.StateDescription))
        assertEquals(0, node.config[SemanticsProperties.ProgressBarRangeInfo].steps)
    }
}
