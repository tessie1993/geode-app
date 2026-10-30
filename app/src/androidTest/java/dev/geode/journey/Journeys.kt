package dev.geode.journey

import android.content.Context
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasScrollToNodeAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.printToLog
import androidx.core.content.edit
import androidx.test.core.app.ApplicationProvider
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.ThemeStore
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
) {
    awaitOrDump("a node with ${matcher.description}", timeoutMs) {
        onAllNodes(matcher).fetchSemanticsNodes().isNotEmpty()
    }
}

/** Waits until no node matches, e.g. a dialog that should have closed. */
fun ComposeTestRule.awaitNoNode(
    matcher: SemanticsMatcher,
    timeoutMs: Long = JOURNEY_TIMEOUT_MS,
) {
    awaitOrDump("no node with ${matcher.description}", timeoutMs) {
        onAllNodes(matcher).fetchSemanticsNodes().isEmpty()
    }
}

/**
 * The Player tab with an empty queue. Its "Nothing playing" overline is drawn uppercased by
 * CrystalOverline, so the title under it is the stable thing to look for.
 */
fun ComposeTestRule.awaitEmptyPlayer() = awaitNode(hasText("Pick something to play"))

/**
 * [ComposeTestRule.waitUntil] that, on timeout, prints every window's semantics tree to logcat
 * (tag [DUMP_TAG]) before failing. CI keeps each test's logcat, so a journey that stalls on an
 * unexpected screen shows which screen it was.
 */
private fun ComposeTestRule.awaitOrDump(
    what: String,
    timeoutMs: Long,
    condition: () -> Boolean,
) {
    try {
        waitUntil(timeoutMs, condition)
    } catch (e: ComposeTimeoutException) {
        runCatching {
            onAllNodes(isRoot()).fetchSemanticsNodes().indices.forEach { i ->
                onAllNodes(isRoot())[i].printToLog(DUMP_TAG, maxDepth = Int.MAX_VALUE)
            }
        }
        throw AssertionError("Timed out after $timeoutMs ms waiting for $what", e)
    }
}

private const val DUMP_TAG = "JourneyTree"

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
    scrollLazyListsTo(matcher)
    awaitNode(matcher)
    val node = onAllNodes(matcher).onFirst()
    // performScrollTo throws when no ancestor scrolls; most of the shell doesn't.
    runCatching { node.performScrollTo() }
    node.performClick()
}

/**
 * A LazyColumn composes only the rows on screen, so a button further down (Settings › Export's
 * "Export video…") has no node until something scrolls to it. Asks every lazy list on screen to
 * scroll to [matcher]; the ones that don't hold it fail, and that failure is ignored.
 */
fun ComposeTestRule.scrollLazyListsTo(matcher: SemanticsMatcher) {
    waitForIdle()
    if (onAllNodes(matcher).fetchSemanticsNodes().isNotEmpty()) return
    val lists = onAllNodes(hasScrollToNodeAction())
    lists.fetchSemanticsNodes().indices.forEach { i -> runCatching { lists[i].performScrollToNode(matcher) } }
}

/** Matches a node whose text matches [regex] in full. */
fun hasTextMatching(regex: Regex): SemanticsMatcher =
    SemanticsMatcher("text matches $regex") { node ->
        val texts = node.config.getOrNull(SemanticsProperties.Text).orEmpty()
        texts.any { regex.matches(it.text) }
    }
