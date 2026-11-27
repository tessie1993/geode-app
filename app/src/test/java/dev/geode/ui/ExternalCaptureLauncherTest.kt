package dev.geode.ui

import android.content.Context
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ApplicationProvider
import dev.geode.R
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * The shared "listen to other apps" start, with the system's dialogs faked: the app's own disclosure
 * comes before any of them, and Cancel leaves everything as it was.
 */
@RunWith(RobolectricTestRunner::class)
class ExternalCaptureLauncherTest {
    @get:Rule
    val compose = createComposeRule()

    private val context: Context get() = ApplicationProvider.getApplicationContext()

    private fun text(id: Int): String = context.getString(id)

    private val mic = mutableStateOf(PermissionStatus.NOT_ASKED)
    private var micDialogs = 0
    private var settingsOpened = 0
    private var consents = 0
    private var refusalsNoted = 0
    private var grant: () -> Unit = {}
    private var refuse: () -> Unit = {}

    @Before
    fun freshProcess() {
        CaptureDisclosure.accepted = false
    }

    private fun showTile() {
        compose.setContent {
            MaterialTheme {
                val capture =
                    rememberExternalCaptureLauncher(
                        askConsent = { consents++ },
                        onMicRefused = { refusalsNoted++ },
                        micRequest = { onGranted, onRefused ->
                            grant = onGranted
                            refuse = onRefused
                            remember { PermissionRequestState(mic, launch = { micDialogs++ }, settings = { settingsOpened++ }) }
                        },
                    )
                TextButton(onClick = capture::start) { Text(text(R.string.source_other_apps)) }
            }
        }
    }

    private fun tapOtherApps() = compose.onNodeWithText(text(R.string.source_other_apps)).performClick()

    private fun assertLaunched(
        micDialogs: Int,
        consents: Int,
    ) {
        compose.waitForIdle()
        assertEquals("mic dialogs", micDialogs, this.micDialogs)
        assertEquals("capture consents", consents, this.consents)
    }

    @Test
    fun withoutTheMicTheDisclosureComesFirstAndNothingLaunchesUntilContinue() {
        showTile()
        tapOtherApps()
        compose.onNodeWithText(text(R.string.ext_disclosure_title)).assertIsDisplayed()
        compose.onNodeWithText(text(R.string.ext_explainer)).assertIsDisplayed()
        assertLaunched(micDialogs = 0, consents = 0)

        compose.onNodeWithText(text(R.string.ext_disclosure_continue)).performClick()
        compose.onNodeWithText(text(R.string.ext_disclosure_title)).assertDoesNotExist()
        assertLaunched(micDialogs = 1, consents = 0)

        compose.runOnIdle {
            mic.value = PermissionStatus.GRANTED
            grant()
        }
        assertLaunched(micDialogs = 1, consents = 1)
    }

    @Test
    fun cancelLaunchesNothing() {
        showTile()
        tapOtherApps()
        compose.onNodeWithText(text(R.string.action_cancel)).performClick()
        compose.onNodeWithText(text(R.string.ext_disclosure_title)).assertDoesNotExist()
        assertLaunched(micDialogs = 0, consents = 0)
        assertEquals(0, settingsOpened)
    }

    @Test
    fun aRefusalIsExplainedAndAFinalOneRoutesToSettings() {
        showTile()
        tapOtherApps()
        compose.onNodeWithText(text(R.string.ext_disclosure_continue)).performClick()
        compose.runOnIdle {
            mic.value = PermissionStatus.DENIED
            refuse()
        }
        compose.onNodeWithText(text(R.string.ext_disclosure_mic_denied)).assertIsDisplayed()
        assertEquals(1, refusalsNoted)

        compose.onNodeWithText(text(R.string.ext_disclosure_continue)).performClick()
        assertLaunched(micDialogs = 2, consents = 0)
        compose.runOnIdle {
            mic.value = PermissionStatus.PERMANENTLY_DENIED
            refuse()
        }
        compose.onNodeWithText(text(R.string.ext_disclosure_mic_denied_forever)).assertIsDisplayed()
        compose.onNodeWithText(text(R.string.permission_open_settings)).performClick()
        compose.waitForIdle()
        assertEquals(1, settingsOpened)
        assertLaunched(micDialogs = 2, consents = 0)
    }

    @Test
    fun onceAcceptedWithTheMicGrantedATapGoesStraightToConsent() {
        mic.value = PermissionStatus.GRANTED
        showTile()
        tapOtherApps()
        compose.onNodeWithText(text(R.string.ext_disclosure_continue)).performClick()
        assertLaunched(micDialogs = 0, consents = 1)

        tapOtherApps()
        compose.onNodeWithText(text(R.string.ext_disclosure_title)).assertDoesNotExist()
        assertLaunched(micDialogs = 0, consents = 2)
    }
}
