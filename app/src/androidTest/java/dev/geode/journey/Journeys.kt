package dev.geode.journey

import android.content.Context
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.core.content.edit
import androidx.test.core.app.ApplicationProvider
import dev.geode.data.GeodePrefsFiles
import dev.geode.ui.ThemeStore
import org.junit.rules.ExternalResource

/**
 * Shared plumbing for the end-to-end journeys in this package.
 *
 * Orchestrator runs with clearPackageData, so every test starts as a fresh install: boot
 * animation, safety notice, first-run setup and the tutorial would all stand between the test
 * and the screen it means to exercise. [Onboarded] writes the same prefs those screens write
 * when the user gets past them, before the activity is launched, so the shell comes up on the
 * Player tab. The keys are ThemeStore's own constants, so a rename breaks the build here rather
 * than silently putting every journey back behind onboarding.
 */
class Onboarded(
    private val skipOnboarding: Boolean = true,
) : ExternalResource() {
    override fun before() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        context.getSharedPreferences(GeodePrefsFiles.GENERAL, Context.MODE_PRIVATE).edit(commit = true) {
            // BootAnimationStore's key, private there. The intro only delays the test.
            putBoolean("boot_anim", false)
            if (skipOnboarding) {
                putBoolean(ThemeStore.KEY_SAFETY_ACKNOWLEDGED, true)
                putBoolean(ThemeStore.KEY_SETUP_DONE, true)
                putBoolean(ThemeStore.KEY_TUTORIAL_SEEN, true)
            }
        }
    }
}

/** Generous: the first frame waits on the native core loading and the GL surface. */
const val JOURNEY_TIMEOUT_MS = 10_000L

/** Waits until some node matches, merged tree, any window (dialogs included). */
fun ComposeTestRule.awaitNode(
    matcher: SemanticsMatcher,
    timeoutMs: Long = JOURNEY_TIMEOUT_MS,
) = waitUntil(timeoutMs) { onAllNodes(matcher).fetchSemanticsNodes().isNotEmpty() }

/** Waits until no node matches, e.g. a dialog that should have closed. */
fun ComposeTestRule.awaitNoNode(
    matcher: SemanticsMatcher,
    timeoutMs: Long = JOURNEY_TIMEOUT_MS,
) = waitUntil(timeoutMs) { onAllNodes(matcher).fetchSemanticsNodes().isEmpty() }

/**
 * Taps the first clickable thing labelled [label], the way a user would look for it: by its
 * visible text, falling back to its content description. Labels repeat across the shell
 * ("Visuals" is both a nav item and a header), and only the clickable one is wanted. Scrolls it
 * into view first when it sits in a scrolling column.
 */
fun ComposeTestRule.tap(label: String) = tap(hasText(label) or hasContentDescription(label))

/** [tap] for a label only a matcher can pin down, such as one with a price in it. */
fun ComposeTestRule.tap(what: SemanticsMatcher) {
    val matcher = what and hasClickAction()
    awaitNode(matcher)
    val node = onAllNodes(matcher).onFirst()
    // performScrollTo throws when no ancestor scrolls; most of the shell doesn't.
    runCatching { node.performScrollTo() }
    node.performClick()
}

/** Matches a node whose text matches [regex] in full. */
fun hasTextMatching(regex: Regex): SemanticsMatcher =
    SemanticsMatcher("text matches $regex") { node ->
        val texts = node.config.getOrNull(SemanticsProperties.Text).orEmpty()
        texts.any { regex.matches(it.text) }
    }
