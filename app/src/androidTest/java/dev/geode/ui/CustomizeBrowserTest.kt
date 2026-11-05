package dev.geode.ui

import androidx.activity.ComponentActivity
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasScrollToNodeAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.render.scene.ParamKeys
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Customize's browser (issue #333) over the engine's own map (core/viz/CustomizeMap through
 * libgeode): a category opens on its Basics, sections drop open below them and several stay open
 * at once, › pages to the next category without a back entry, and back returns to the list where
 * it was scrolled. Each control is drawn as its name, so the test reads what the map put where.
 */
@RunWith(AndroidJUnit4::class)
class CustomizeBrowserTest {
    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    private lateinit var listState: LazyListState
    private lateinit var nav: CustomizeNav

    private fun setBrowser() {
        compose.setContent {
            MaterialTheme {
                nav = rememberLocalCustomizeNav()
                listState = rememberLazyListState()
                val open = rememberOpenSections()
                CustomizeBrowser(
                    categories = CustomizeCategory.entries,
                    sectionsOf = CustomizeMapTable::sectionsOf,
                    nav = nav,
                    listState = listState,
                    openSections = open.value,
                    onToggleSection = { s -> open.value = if (s.path in open.value) open.value - s.path else open.value + s.path },
                    sectionLocked = { false },
                    // Shorter than the ten rows, so the list scrolls on any emulator.
                    modifier = Modifier.fillMaxWidth().height(BROWSER_HEIGHT),
                ) { section -> section.controls.forEach { Text(it) } }
            }
        }
    }

    private fun back() {
        compose.runOnUiThread { compose.activity.onBackPressedDispatcher.onBackPressed() }
        compose.waitForIdle()
    }

    @Test
    fun openingLookShowsItsBasicsAndNotItsSections() {
        setBrowser()
        compose.onNodeWithText("Look").performClick()
        compose.onNodeWithText(ParamKeys.PALETTE).assertIsDisplayed()
        compose.onNodeWithText(ParamKeys.BRIGHTNESS).assertIsDisplayed()
        compose.onNodeWithText(ParamKeys.HUE_SHIFT).assertDoesNotExist()
    }

    @Test
    fun colourDetailRevealsHueShiftAndTwoSectionsStayOpen() {
        setBrowser()
        compose.onNodeWithText("Look").performClick()
        compose.onNodeWithText("Colour detail").performClick()
        compose.onNodeWithText(ParamKeys.HUE_SHIFT).assertIsDisplayed()
        compose.onNode(hasScrollToNodeAction()).performScrollToNode(hasText("Colour cycle"))
        compose.onNodeWithText("Colour cycle").performClick()
        compose.onNode(hasScrollToNodeAction()).performScrollToNode(hasText(ParamKeys.CYCLE_SPEED))
        compose.onNodeWithText(ParamKeys.CYCLE_SPEED).assertIsDisplayed()
        // The first section is still open.
        compose.onNode(hasScrollToNodeAction()).performScrollToNode(hasText(ParamKeys.HUE_SHIFT))
        compose.onNodeWithText(ParamKeys.HUE_SHIFT).assertIsDisplayed()
    }

    @Test
    fun nextStepsFromLookToShapeAndBackReturnsToTheList() {
        setBrowser()
        compose.onNodeWithText("Look").performClick()
        compose.onNodeWithContentDescription("Next: Shape").performClick()
        compose.onNodeWithText(ParamKeys.MORPH).assertIsDisplayed()
        assertEquals(CustomizeCategory.SHAPE.name, nav.openCategory)
        // Sideways, not forward: one back closes the category, whichever one it stepped to.
        back()
        assertEquals(null, nav.openCategory)
        compose.onNodeWithText("Quick knobs").assertIsDisplayed()
    }

    @Test
    fun backReturnsToTheListAtTheSameScrollPosition() {
        setBrowser()
        compose.onNode(hasScrollToNodeAction()).performScrollToNode(hasText("Expert"))
        compose.waitForIdle()
        val before = listState.firstVisibleItemIndex to listState.firstVisibleItemScrollOffset
        assertTrue("the list scrolled", before.first > 0 || before.second > 0)
        compose.onNodeWithText("Expert").performClick()
        compose.onNodeWithText("This style's engine").assertIsDisplayed()
        back()
        compose.onNodeWithText("Expert").assertIsDisplayed()
        assertEquals(before, listState.firstVisibleItemIndex to listState.firstVisibleItemScrollOffset)
    }
}

private val BROWSER_HEIGHT = 360.dp
