package dev.geode.journey

import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import com.google.common.truth.Truth.assertThat
import dev.geode.BuildConfig
import dev.geode.ui.MainActivity
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * The Pro gate as a free user meets it: a locked style opens the paywall, closing it changes
 * nothing, and buying Pro (against FakeBillingRepository, which debug builds bill through)
 * unlocks the style so the next tap goes straight through.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class PaywallJourneyTest {
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(Onboarded()).around(compose)

    private val stylesTitle = hasText("Every visual style, unlocked")

    @Before
    fun needsFakeBilling() {
        // -Pmuzviz.fakeBilling=false points debug at the real Play Store; nothing to fake then.
        assumeTrue(BuildConfig.USE_FAKE_BILLING)
    }

    /** Silk styles are Pro: only the shader styles are in FreeTier. */
    private fun tapLockedStyle() {
        compose.tap("Styles")
        compose.tap("Silk")
        compose.tap("Halvorsen Web")
    }

    private fun openVisualsStyles() {
        compose.awaitNode(hasText("Nothing playing"))
        compose.tap("Visuals")
        compose.awaitNode(hasText("View live"))
    }

    @Test
    fun lockedStyleOpensThePaywallAndCloseDismissesIt() {
        openVisualsStyles()
        tapLockedStyle()
        compose.awaitNode(stylesTitle)
        compose.tap("Close")
        compose.awaitNoNode(stylesTitle)
    }

    @Test
    fun buyingProUnlocksTheStyle() {
        openVisualsStyles()
        tapLockedStyle()
        compose.awaitNode(stylesTitle)
        compose.awaitNode(hasText("Debug billing (fake)"))

        // "Unlock Pro · <price>": the fake answers the price query after ~900 ms, and until
        // then the button reads "Getting the price…".
        compose.tap(hasText("Unlock Pro", substring = true))
        compose.tap("Start creating")
        compose.awaitNoNode(stylesTitle)

        compose.tap("Halvorsen Web")
        compose.waitForIdle()
        assertThat(compose.onAllNodes(stylesTitle).fetchSemanticsNodes()).isEmpty()
    }
}
