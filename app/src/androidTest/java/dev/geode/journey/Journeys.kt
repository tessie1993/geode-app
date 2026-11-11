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
import dev.geode.data.PerformanceStore
import dev.geode.data.StartScreen
import dev.geode.data.ThemeStore
import dev.geode.geodeContainer
import dev.geode.render.PerformanceSettings
import kotlinx.coroutines.runBlocking
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
 *
 * The file alone is not enough: the instrumented process runs `GeodeApp.onCreate` before any rule,
 * and that builds the container, whose `UserDataRepository` reads the GUI prefs once and keeps
 * them. So the seeded prefs are also handed to that repository ([publishSeededGuiPrefs]); without
 * it the shell draws the fresh install's safety notice over every journey.
 *
 * It also pins Settings › Performance to [PerformanceSettings.SAVER]. Left unset, the app picks
 * a mode from the device's RAM on first run, so the same journey rendered at half scale on the
 * 2 GB CI emulator and would render at 0.75 on a 4 GB one. On swiftshader every GL frame is drawn
 * on the CPU, and GLSurfaceView holds the main thread until the first frame of each new surface
 * is drawn: the heavier that frame, the longer the stall, and stalls of ~4.5 s per live-view
 * transition are what got the instrumented process killed in CI (see tools/connected-tests.sh).
 * Pinning keeps that cost fixed whatever AVD the workflow boots.
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
        val general = context.getSharedPreferences(GeodePrefsFiles.GENERAL, Context.MODE_PRIVATE)
        PerformanceStore(general, context).set(PerformanceSettings.SAVER)
        // The journeys start from the empty Player; a new install opens on Library (#796). Through the
        // app's own repository: it read the file when the application started, before this rule.
        runBlocking { context.geodeContainer.behaviour.update { it.copy(startScreen = StartScreen.PLAYER) } }
        if (skipOnboarding) publishSeededGuiPrefs(context)
    }
}

/**
 * Gives the app's live `UserDataRepository` the GUI prefs a rule has just written to the prefs
 * file. The repository loaded that file when the application started, before the rule ran, and
 * the shell draws from its copy; `setGuiPrefs` is the call the app's own screens make, and it
 * also marks the repository loaded, so a load still in flight cannot put the old values back.
 */
internal fun publishSeededGuiPrefs(context: Context) {
    val seeded = ThemeStore(context.getSharedPreferences(GeodePrefsFiles.GENERAL, Context.MODE_PRIVATE)).loadGui()
    runBlocking { context.geodeContainer.userData.setGuiPrefs(seeded) }
}

/**
 * Generous: the first frame waits on the native core loading and the GL surface, and on the
 * swiftshader emulator each new GL surface stalls the main thread for ~4.5 s (logcat: "Skipped
 * 276 frames"). Two such stalls back to back left a 10 s wait with nothing in hand. A wait that
 * succeeds returns as soon as its node appears, so the ceiling costs nothing on a pass.
 */
const val JOURNEY_TIMEOUT_MS = 20_000L

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
 * A LazyColumn composes only the rows on screen, so a button further down (a Settings tab's
 * lower rows) has no node until something scrolls to it. Asks every lazy list on screen to
 * scroll to [matcher]; the ones that don't hold it fail, and that failure is ignored.
 *
 * It first gives the node a moment to appear on its own: right after a tap that changes page,
 * the old page's lists are still on screen and the wanted node is not yet composed, and
 * scrolling the wrong list there is wasted at best. Grids are skipped (a lazy grid reports
 * neither its rows nor its columns, -1 and -1, where a lazy list knows one of the two):
 * performScrollToNode on the Styles grid never returned, which hung the journeys for the
 * whole job. A journey that needs a card in the grid filters or searches to it instead.
 */
fun ComposeTestRule.scrollLazyListsTo(matcher: SemanticsMatcher) {
    waitForIdle()
    val appeared =
        runCatching {
            waitUntil(SETTLE_TIMEOUT_MS) { onAllNodes(matcher).fetchSemanticsNodes().isNotEmpty() }
        }.isSuccess
    if (appeared) return
    val lists = onAllNodes(hasScrollToNodeAction())
    lists.fetchSemanticsNodes().forEachIndexed { i, node ->
        val info = node.config.getOrNull(SemanticsProperties.CollectionInfo)
        val isGrid = info != null && info.rowCount < 0 && info.columnCount < 0
        if (!isGrid) {
            runCatching { lists[i].performScrollToNode(matcher) }
        }
    }
}

/** How long [scrollLazyListsTo] lets a node appear by itself before it scrolls for it. */
private const val SETTLE_TIMEOUT_MS = 3_000L

/** Matches a node whose text matches [regex] in full. */
fun hasTextMatching(regex: Regex): SemanticsMatcher =
    SemanticsMatcher("text matches $regex") { node ->
        val texts = node.config.getOrNull(SemanticsProperties.Text).orEmpty()
        texts.any { regex.matches(it.text) }
    }
