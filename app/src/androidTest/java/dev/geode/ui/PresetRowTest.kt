package dev.geode.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performSemanticsAction
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.model.PresetEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** A Presets row: text only, a tap applies, a long press or its TalkBack action opens the sheet. */
@RunWith(AndroidJUnit4::class)
class PresetRowTest {
    @get:Rule
    val compose = createComposeRule()

    private val events = mutableListOf<String>()

    private fun show(
        applied: Boolean,
        inPlaylist: Boolean = false,
    ) {
        compose.setContent {
            MaterialTheme {
                PresetRow(
                    entry = PresetEntry(name = "Night Drive", sceneId = "test_style", builtIn = false),
                    applied = applied,
                    locked = false,
                    inPlaylist = inPlaylist,
                    onTap = { events += "tap" },
                    onMenu = { events += "menu" },
                )
            }
        }
    }

    private fun row() = compose.onNode(hasClickAction())

    @Test
    fun tapAppliesAndLongPressOpensTheActions() {
        show(applied = false)
        compose.onNodeWithText("Night Drive").assertExists()
        compose.onNodeWithText("Mine").assertExists()
        row().assertHeightIsAtLeast(48.dp).performClick()
        row().performSemanticsAction(SemanticsActions.OnLongClick)
        val custom = row().fetchSemanticsNode().config[SemanticsActions.CustomActions]
        assertEquals(listOf("Preset actions"), custom.map { it.label })
        compose.runOnUiThread { custom.single().action() }
        assertEquals(listOf("tap", "menu", "menu"), events)
        assertNull(row().fetchSemanticsNode().config.getOrNull(SemanticsProperties.StateDescription))
    }

    @Test
    fun theAppliedRowSaysSo() {
        show(applied = true, inPlaylist = true)
        val state = row().fetchSemanticsNode().config.getOrNull(SemanticsProperties.StateDescription)
        assertEquals("Applied, In the visual playlist", state)
    }
}
