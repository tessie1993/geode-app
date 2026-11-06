package dev.geode.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import dev.geode.R
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The library's empty state (#340): music access granted, MediaStore answered with nothing. The
 * screen shows [LibraryEmpty] exactly when `LibraryUiState.showEmptyLibrary` holds (covered by
 * `LibraryViewModelTest`); this draws it and presses its buttons.
 */
@RunWith(AndroidJUnit4::class)
class LibraryEmptyTest {
    @get:Rule
    val compose = createComposeRule()

    private fun text(id: Int): String = InstrumentationRegistry.getInstrumentation().targetContext.getString(id)

    @Test
    fun emptyGrantedLibraryShowsRescanAndItsWaysOut() {
        var rescans = 0
        var folders = 0
        var mics = 0
        var apps = 0
        compose.setContent {
            MaterialTheme {
                LibraryEmpty(
                    onRescan = { rescans++ },
                    onChooseFolder = { folders++ },
                    onListenToApp = { apps++ },
                    onListenToMic = { mics++ },
                    minTrackSeconds = 30,
                )
            }
        }
        compose.onNodeWithText(text(R.string.library_empty_hint)).assertIsDisplayed()
        compose.onNodeWithText(text(R.string.library_empty_rescan)).assertIsDisplayed().performClick()
        assertEquals(1, rescans)

        compose.onNodeWithText(text(R.string.library_empty_choose_folder)).assertIsDisplayed().performClick()
        assertEquals(1, folders)

        compose.onNodeWithText(text(R.string.library_empty_listen)).assertIsDisplayed().performClick()
        compose.onNodeWithText(text(R.string.library_empty_listen_mic)).performClick()
        assertEquals(1, mics)
        assertEquals(0, apps)
    }

    @Test
    fun withoutAppCaptureListeningGoesStraightToTheMic() {
        var mics = 0
        compose.setContent {
            MaterialTheme {
                LibraryEmpty(onRescan = {}, onChooseFolder = {}, onListenToApp = null, onListenToMic = { mics++ })
            }
        }
        compose.onNodeWithText(text(R.string.library_empty_listen)).performClick()
        assertEquals(1, mics)
    }
}
