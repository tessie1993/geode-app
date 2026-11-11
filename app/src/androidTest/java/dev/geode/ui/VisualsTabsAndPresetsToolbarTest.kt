package dev.geode.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import dev.geode.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * #865: every Visuals tab label is fully inside the side panel at its narrowest width.
 * #866: the Save look label wraps at 2x font scale instead of ending in an ellipsis, and is one
 * line at the default scale in a 360 dp phone's sheet.
 */
@RunWith(AndroidJUnit4::class)
class VisualsTabsAndPresetsToolbarTest {
    @get:Rule
    val compose = createComposeRule()

    @Test
    fun everyTabLabelFitsAtTheNarrowestPanel() {
        val labels = mutableListOf<String>()
        compose.setContent {
            val context = InstrumentationRegistry.getInstrumentation().targetContext
            VisualsTabs.forEach { labels += context.getString(it.titleRes) }
            MaterialTheme {
                // The panel's narrowest width, less the 48 dp close button beside the tabs.
                Row(Modifier.width(PANEL_MIN - 48.dp)) {
                    VisualsTabRow(selectedTab = 0, onSelect = {})
                }
            }
        }
        val rootRight =
            compose
                .onRoot()
                .fetchSemanticsNode()
                .boundsInRoot.right
        for (label in labels) {
            val bounds = compose.onNodeWithText(label).fetchSemanticsNode().boundsInRoot
            assertTrue("$label ends at ${bounds.right}, past the row's $rootRight", bounds.right <= rootRight)
        }
    }

    @Test
    fun saveLookIsNotTruncatedAtDoubleFontScale() {
        val layout = saveLookLayout(width = 320.dp, fontScale = 2f)
        assertFalse("label is cut off", layout.hasVisualOverflow)
    }

    /** A 360 dp window less the sheet's 12 dp inset on each side: the label used to take three lines. */
    @Test
    fun saveLookIsOneLineInA360DpPhonesSheet() {
        val layout = saveLookLayout(width = 336.dp, fontScale = 1f)
        assertEquals(1, layout.lineCount)
        assertFalse("label is cut off", layout.hasVisualOverflow)
    }

    private fun saveLookLayout(
        width: Dp,
        fontScale: Float,
    ): TextLayoutResult {
        var label = ""
        compose.setContent {
            val density = LocalDensity.current
            label = InstrumentationRegistry.getInstrumentation().targetContext.getString(R.string.presets_save_look)
            CompositionLocalProvider(LocalDensity provides Density(density.density, fontScale = fontScale)) {
                MaterialTheme {
                    Row(Modifier.width(width)) {
                        PresetsToolbar(
                            openFolder = null,
                            onSave = {},
                            onPaste = {},
                            onOpenFile = {},
                            onRenameFolder = {},
                            onTemplates = {},
                            onBackground = {},
                        )
                    }
                }
            }
        }
        val results = mutableListOf<TextLayoutResult>()
        compose
            .onNodeWithText(label)
            .fetchSemanticsNode()
            .config[SemanticsActions.GetTextLayoutResult]
            .action
            ?.invoke(results)
        assertFalse("label was not laid out", results.isEmpty())
        return results.first()
    }
}
