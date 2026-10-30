package dev.geode.journey

import android.Manifest
import android.os.Build
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.rule.GrantPermissionRule
import dev.geode.ui.MainActivity
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/** Every tab of the shell opens, for a user past onboarding who let the app see their music. */
@LargeTest
@RunWith(AndroidJUnit4::class)
class ShellTabsTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    /**
     * Music access, granted up front as a user who said yes would have: the Library tab asks for
     * it otherwise, and the system dialog is another app's window that no Compose finder sees.
     */
    @get:Rule
    val music: GrantPermissionRule =
        GrantPermissionRule.grant(
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                Manifest.permission.READ_MEDIA_AUDIO
            } else {
                Manifest.permission.READ_EXTERNAL_STORAGE
            },
        )

    @Test
    fun everyTabOpens() {
        // Seeded past onboarding: the shell starts on the Player tab with an empty queue.
        compose.awaitEmptyPlayer()

        compose.tap("Library")
        compose.awaitNoNode(hasText("Pick something to play"))

        compose.tap("Visuals")
        compose.awaitNode(hasText("View live"))

        // Default intent is BOTH, which shows Studio (behind its Pro gate for a free user).
        compose.tap("Studio")
        compose.waitForIdle()

        compose.tap("Settings")
        compose.awaitNode(hasText("Export"))

        compose.tap("Player")
        compose.awaitEmptyPlayer()
    }
}
