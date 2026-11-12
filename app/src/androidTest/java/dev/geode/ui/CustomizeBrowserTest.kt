package dev.geode.ui

import androidx.activity.ComponentActivity
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasScrollToNodeAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Customize's browser (owner, 2026-11-12) over the engine's own map (core/viz/CustomizeMap through
 * libgeode): the categories are tabs, a tab is a list of groups that all start collapsed, an opened
 * group stays open (several at once, and when its tab is left and come back to), and Collapse all
 * closes the tab's groups and nobody else's. Each control is drawn as its name, so the test reads
 * what the map put where. The state is hoisted here as CustomizePanel hoists it into
 * CustomizeGroupsStore.
 */
@RunWith(AndroidJUnit4::class)
class CustomizeBrowserTest {
    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    private var tab by mutableStateOf(CustomizeCategory.QUICK_KNOBS)
    private var open by mutableStateOf(emptySet<String>())

    private fun setBrowser() {
        compose.setContent {
            MaterialTheme {
                CustomizeBrowser(
                    categories = CustomizeCategory.entries,
                    selected = tab,
                    onSelect = { tab = it },
                    sectionsOf = CustomizeMapTable::sectionsOf,
                    openGroups = open,
                    onToggleGroup = { s -> open = if (s.path in open) open - s.path else open + s.path },
                    onCollapseAll = { c -> open = open.filterTo(mutableSetOf()) { !it.startsWith("${c.name}/") } },
                    sectionLocked = { false },
                    // Short, so a tab of open groups scrolls on any emulator.
                    modifier = Modifier.fillMaxWidth().height(BROWSER_HEIGHT),
                ) { section -> section.controls.forEach { Text(it) } }
            }
        }
    }

    private fun openTab(category: CustomizeCategory) {
        compose.onNode(isTab and hasText(category.title)).performClick()
        compose.waitForIdle()
    }

    private fun scrollTo(text: String) {
        compose.onNode(hasScrollToNodeAction() and !isTabRow).performScrollToNode(hasText(text))
    }

    @Test
    fun aTabOpensWithEveryGroupCollapsedBasicsIncluded() {
        setBrowser()
        openTab(CustomizeCategory.LOOK)
        assertEquals(CustomizeCategory.LOOK, tab)
        compose.onNodeWithText("Basics").assertIsDisplayed()
        compose.onNodeWithText("Colour detail").assertIsDisplayed()
        compose.onNodeWithText("Palette").assertDoesNotExist()
        compose.onNodeWithText("Hue shift").assertDoesNotExist()
    }

    @Test
    fun groupsOpenTogetherAndStayOpenAcrossTabs() {
        setBrowser()
        openTab(CustomizeCategory.LOOK)
        compose.onNodeWithText("Basics").performClick()
        compose.onNodeWithText("Palette").assertIsDisplayed()
        scrollTo("Colour detail")
        compose.onNodeWithText("Colour detail").performClick()
        scrollTo("Hue shift")
        compose.onNodeWithText("Hue shift").assertIsDisplayed()
        // Another tab and back: a tab is not a back step, and what was open is open again.
        openTab(CustomizeCategory.SHAPE)
        compose.onNodeWithText("Morph").assertDoesNotExist()
        openTab(CustomizeCategory.LOOK)
        scrollTo("Palette")
        compose.onNodeWithText("Palette").assertIsDisplayed()
        assertEquals(setOf("LOOK/basics", "LOOK/colour_detail"), open)
    }

    @Test
    fun collapseAllClosesThisTabOnly() {
        setBrowser()
        openTab(CustomizeCategory.SHAPE)
        compose.onNodeWithText("Basics").performClick()
        openTab(CustomizeCategory.LOOK)
        compose.onNodeWithText("Basics").performClick()
        compose.onNodeWithText("Palette").assertIsDisplayed()
        // The label is the app's own string, as TalkBack reads it, not a copy of its English.
        val collapse = compose.activity.getString(R.string.customize_collapse_all_in, CustomizeCategory.LOOK.title)
        compose.onNodeWithContentDescription(collapse).performClick()
        compose.onNodeWithText("Palette").assertDoesNotExist()
        compose.onNodeWithContentDescription(collapse).assertIsNotEnabled()
        assertEquals(setOf("SHAPE/basics"), open)
    }

    @Test
    fun engineHasNoBasicsAndOpensOnItsGroups() {
        setBrowser()
        compose.onNode(isTabRow).performScrollToNode(hasText(CustomizeCategory.ENGINE.title))
        openTab(CustomizeCategory.ENGINE)
        compose.onNodeWithText("Fluid: flow").assertIsDisplayed()
        compose.onNodeWithText("Basics").assertDoesNotExist()
        assertTrue(open.isEmpty())
    }

    private companion object {
        val isTab = SemanticsMatcher.keyIsDefined(SemanticsProperties.Selected)

        // The tab row is the horizontal scroller; the group list is the other one.
        val isTabRow = SemanticsMatcher.keyIsDefined(SemanticsProperties.HorizontalScrollAxisRange)
    }
}

private val BROWSER_HEIGHT = 360.dp
