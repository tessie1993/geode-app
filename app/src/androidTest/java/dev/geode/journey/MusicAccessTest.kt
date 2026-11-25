package dev.geode.journey

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.core.content.ContextCompat
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.onElement
import androidx.test.uiautomator.onElementOrNull
import com.google.common.truth.Truth.assertThat
import dev.geode.R
import dev.geode.ui.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * Library's music-access request answered in the real system dialog, the way a user meets it.
 * The dialog is the permission controller's window, out of Compose's reach, so UI Automator
 * presses its buttons (developer.android.com/training/testing/other-components/ui-automator).
 * Its ready-made PermissionDialog is not used: its deny looks only for the first-ask button, and
 * from Android 11 the second ask's "Don't allow" is a different one (deny and don't ask again).
 * Orchestrator's clearPackageData revokes the grant before each test.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class MusicAccessTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation())

    private fun text(id: Int) = context.getString(id)

    private fun granted() =
        ContextCompat.checkSelfPermission(context, Manifest.permission.READ_MEDIA_AUDIO) ==
            PackageManager.PERMISSION_GRANTED

    private fun openLibrary() {
        compose.awaitEmptyPlayer()
        compose.tap("Library")
        compose.awaitNode(hasText(text(R.string.library_permission_rationale)))
    }

    /** Presses the system dialog's button and waits for the dialog to go, so the next tap is the app's. */
    private fun answer(allow: Boolean) {
        val buttons = if (allow) setOf(ALLOW) else setOf(DENY, DENY_DONT_ASK_AGAIN)
        device.onElement(JOURNEY_TIMEOUT_MS) { viewIdResourceName in buttons }.click()
        compose.waitUntil(JOURNEY_TIMEOUT_MS) {
            device.onElementOrNull(timeoutMs = 0) { viewIdResourceName?.startsWith(CONTROLLER_IDS) == true } == null
        }
    }

    @Test
    fun allowingShowsTheLibrary() {
        openLibrary()
        compose.tap(text(R.string.library_permission_allow))
        answer(allow = true)

        compose.awaitNoNode(hasText(text(R.string.library_permission_rationale)))
        assertThat(granted()).isTrue()
    }

    /**
     * Android stops showing the dialog after a second refusal. The screen must then say so and
     * offer app Settings, the only way left to turn access on, instead of a button that does nothing.
     */
    @Test
    fun refusingTwiceOffersAppSettings() {
        openLibrary()
        compose.tap(text(R.string.library_permission_allow))
        answer(allow = false)
        compose.awaitNode(hasText(text(R.string.library_permission_rationale)))

        compose.tap(text(R.string.library_permission_allow))
        answer(allow = false)

        compose.awaitNode(hasText(text(R.string.library_permission_denied_forever)))
        compose.awaitNode(hasText(text(R.string.library_permission_open_settings)))
        assertThat(granted()).isFalse()
    }

    private companion object {
        // The permission controller's view ids keep the AOSP namespace in Google's build too.
        const val CONTROLLER_IDS = "com.android.permissioncontroller:id/"
        const val ALLOW = CONTROLLER_IDS + "permission_allow_button"
        const val DENY = CONTROLLER_IDS + "permission_deny_button"
        const val DENY_DONT_ASK_AGAIN = CONTROLLER_IDS + "permission_deny_and_dont_ask_again_button"
    }
}
