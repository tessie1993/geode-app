package dev.geode.journey

import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.performTextInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.platform.app.InstrumentationRegistry
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

    /**
     * Silk styles are Pro: ten styles are free (FreeTier). A tap loads a style; a Pro one
     * plays on the visual while the paywall offers it. Silk is picked in the Styles family dropdown.
     */
    private fun tapLockedStyle() {
        compose.tap("Styles")
        compose.tap("Style family")
        compose.tap("Silk")
        compose.tap("Halvorsen Web")
    }

    private fun openVisualsStyles() {
        compose.awaitEmptyPlayer()
        compose.tap("Visuals")
        compose.awaitNode(hasContentDescription("Show or hide the options"))
    }

    @Test
    fun lockedStyleOpensThePaywallAndCloseDismissesIt() {
        openVisualsStyles()
        tapLockedStyle()
        compose.awaitNode(stylesTitle)
        compose.tap("Close")
        compose.awaitNoNode(stylesTitle)
    }

    /**
     * The fake store sells nothing until the tester code is entered, and the code is not in the repo:
     * CI passes it as the `testerCode` instrumentation argument (a secret). Without it this skips.
     */
    @Test
    fun buyingProUnlocksTheStyle() {
        val code = InstrumentationRegistry.getArguments().getString("testerCode")
        assumeTrue("needs -e testerCode", !code.isNullOrBlank())
        openVisualsStyles()
        tapLockedStyle()
        compose.awaitNode(stylesTitle)
        compose.awaitNode(hasText("Debug billing (fake)"))

        // The code grants Pro at once, as a purchase would.
        compose.onNode(hasText("Tester code")).performTextInput(code!!)
        compose.tap("Unlock")
        compose.tap("Start creating")
        compose.awaitNoNode(stylesTitle)

        // Buying from the paywall the tap opened chooses that style: no second trip to the list.
        compose.awaitNode(hasText("Halvorsen Web"))
        assertThat(compose.onAllNodes(stylesTitle).fetchSemanticsNodes()).isEmpty()
    }
}
