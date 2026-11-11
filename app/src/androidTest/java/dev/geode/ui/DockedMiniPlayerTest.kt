package dev.geode.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertWidthIsAtLeast
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** The Visuals mini player docked to the options: one slim line, the title and three 48 dp buttons. */
@RunWith(AndroidJUnit4::class)
class DockedMiniPlayerTest {
    @get:Rule
    val compose = createComposeRule()

    @Test
    fun slimBarWithTitleAndBasicTransport() {
        val pressed = mutableListOf<String>()
        compose.setContent {
            MaterialTheme {
                DockedMiniPlayer(
                    title = "Night Drive",
                    isPlaying = true,
                    progress = { 0.5f },
                    onOpenLibrary = {},
                    onOpenPlayer = {},
                    onHide = {},
                    onPrevious = { pressed += "previous" },
                    onPlayPause = { pressed += "pause" },
                    onNext = { pressed += "next" },
                )
            }
        }
        compose.onRoot().assertHeightIsEqualTo(DOCKED_PLAYER_HEIGHT)
        compose.onNodeWithText("Night Drive").assertIsDisplayed()
        for (label in listOf("Previous", "Pause", "Next")) {
            compose
                .onNodeWithContentDescription(label)
                .assertIsDisplayed()
                .assertWidthIsAtLeast(48.dp)
                .assertHeightIsAtLeast(48.dp)
                .performClick()
        }
        assertEquals(listOf("previous", "pause", "next"), pressed)
    }
}
