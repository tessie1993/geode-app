package dev.geode.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.R
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * #865: every Visuals tab label is fully inside the side panel at its narrowest width.
 * #866: the Save current look label wraps at 2x font scale instead of ending in an ellipsis.
 */
@RunWith(AndroidJUnit4::class)
class VisualsTabsAndPresetsToolbarTest {
    @get:Rule
    val compose = createComposeRule()

    @Test
    fun everyTabLabelFitsAtTheNarrowestPanel() {
        val labels = mutableListOf<String>()
        compose.setContent {
            val context = LocalContext.current
            VisualsTabs.forEach { labels += context.getString(it.titleRes) }
            MaterialTheme {
                // The panel's narrowest width, less the 48 dp close button beside the tabs.
                Row(Modifier.width(PANEL_MIN - 48.dp)) {
                    VisualsTabRow(selectedTab = 0, onSelect = {})
                }
            }
        }
        val rootRight = compose.onRoot().fetchSemanticsNode().boundsInRoot.right
        for (label in labels) {
            val bounds = compose.onNodeWithText(label).fetchSemanticsNode().boundsInRoot
            assertTrue("$label ends at ${bounds.right}, past the row's $rootRight", bounds.right <= rootRight)
        }
    }

    @Test
    fun saveCurrentLookIsNotTruncatedAtDoubleFontScale() {
        var label = ""
        compose.setContent {
            val density = LocalDensity.current
            label = LocalContext.current.getString(R.string.presets_save_current_look)
            CompositionLocalProvider(LocalDensity provides Density(density.density, fontScale = 2f)) {
                MaterialTheme {
                    Row(Modifier.width(320.dp)) {
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
        compose.onNodeWithText(label).fetchSemanticsNode().config[SemanticsActions.GetTextLayoutResult]
            .action?.invoke(results)
        assertFalse("label was not laid out", results.isEmpty())
        assertFalse("label is cut off", results.first().hasVisualOverflow)
    }
}
