package dev.geode.journey

import android.content.Context
import androidx.compose.ui.layout.boundsInParent
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsNode
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.SemanticsNodeInteraction
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
import androidx.compose.ui.test.performSemanticsAction
import androidx.compose.ui.test.printToLog
import androidx.compose.ui.text.AnnotatedString
import androidx.core.content.edit
import androidx.test.core.app.ApplicationProvider
import androidx.test.espresso.IdlingPolicies
import androidx.test.espresso.IdlingResourceTimeoutException
import dev.geode.data.AppStateCodec
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.PerformanceSettings
import dev.geode.data.PerformanceStore
import dev.geode.data.StartScreen
import dev.geode.geodeContainer
import kotlinx.coroutines.runBlocking
import org.junit.rules.ExternalResource
import java.util.concurrent.TimeUnit

/**
 * Shared plumbing for the end-to-end journeys in this package.
 *
 * Orchestrator runs with clearPackageData, so every test starts as a fresh install: boot
 * animation, safety notice, first-run setup and the tutorial would all stand between the test
 * and the screen it means to exercise. [Onboarded] writes the same prefs those screens write
 * when the user gets past them, before the activity is launched, so the shell comes up on the
 * Player tab. The keys are AppStateCodec's own constants, so a rename breaks the build here rather
 * than silently putting every journey back behind onboarding.
 *
 * The file alone is not enough: the instrumented process runs `GeodeApp.onCreate` before any rule,
 * and that builds the container, whose app-state repository reads the file once and keeps its
 * value. So the seeded state is also handed to that repository ([publishSeededAppState]); without
 * it the shell draws the fresh install's safety notice over every journey.
 *
 * It also pins Settings › Performance to [PerformanceSettings.SAVER]. Left unset, the app picks
 * a mode from the device's RAM on first run, so the same journey would render at a different
 * scale on each AVD. On SwiftShader every frame is drawn on the CPU, so the stage's native
 * render thread and every window's HWUI render thread queue on the same host core: the heavier the
 * stage, the longer each app frame waits. Pinning keeps that cost fixed whatever AVD boots.
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
                putBoolean(AppStateCodec.KEY_SAFETY_ACKNOWLEDGED, true)
                putBoolean(AppStateCodec.KEY_SETUP_DONE, true)
                putBoolean(AppStateCodec.KEY_TUTORIAL_SEEN, true)
            }
        }
        val general = context.getSharedPreferences(GeodePrefsFiles.GENERAL, Context.MODE_PRIVATE)
        PerformanceStore(general, context).set(PerformanceSettings.SAVER)
        // The journeys start from the empty Player; a new install opens on Library (#796). Through the
        // app's own repository: it read the file when the application started, before this rule.
        runBlocking { context.geodeContainer.behaviour.update { it.copy(startScreen = StartScreen.PLAYER) } }
        if (skipOnboarding) publishSeededAppState(context)
        // One HWUI frame took 22-27 s on the CI emulator's shared SwiftShader core, against Espresso's 26 s idling budget.
        IdlingPolicies.setIdlingResourceTimeout(IDLING_RESOURCE_TIMEOUT_S, TimeUnit.SECONDS)
        IdlingPolicies.setMasterPolicyTimeout(MASTER_POLICY_TIMEOUT_S, TimeUnit.SECONDS)
    }

    override fun after() {
        // The policies are process-wide: tests outside the journeys keep Espresso's defaults.
        IdlingPolicies.setIdlingResourceTimeout(DEFAULT_IDLING_RESOURCE_TIMEOUT_S, TimeUnit.SECONDS)
        IdlingPolicies.setMasterPolicyTimeout(DEFAULT_MASTER_POLICY_TIMEOUT_S, TimeUnit.SECONDS)
    }
}

/** Espresso's idling budget in the journeys, well above the slowest frame CI has logged; the master policy stays above it. */
private const val IDLING_RESOURCE_TIMEOUT_S = 60L
private const val MASTER_POLICY_TIMEOUT_S = 90L

/** Espresso's own defaults (IdlingPolicies), restored after each journey. */
private const val DEFAULT_IDLING_RESOURCE_TIMEOUT_S = 26L
private const val DEFAULT_MASTER_POLICY_TIMEOUT_S = 60L

/**
 * Gives the app's live app-state repository the onboarding state a rule has just written to the prefs
 * file. The repository loaded that file when the application started, before the rule ran, and
 * the shell draws from its copy; `update` is the call the app's own screens make, and it waits
 * for a load still in flight, so that load cannot put the old values back.
 */
internal fun publishSeededAppState(context: Context) {
    val seeded = AppStateCodec.read(context.getSharedPreferences(GeodePrefsFiles.GENERAL, Context.MODE_PRIVATE))
    runBlocking { context.geodeContainer.appState.update { seeded } }
}

/**
 * Generous: the first frame waits on the native core loading, and on the SwiftShader emulator
 * HWUI frames queue behind the stage on one host core for seconds (logcat: "Skipped 276 frames").
 * A wait that succeeds returns as soon as its node appears, so the ceiling costs nothing on a pass.
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
        dumpTrees()
        throw AssertionError("Timed out after $timeoutMs ms waiting for $what", e)
    } catch (e: IdlingResourceTimeoutException) {
        // Espresso gave up on Compose going idle: a stall, not a missing node. Dump all the same.
        dumpTrees()
        throw AssertionError("Compose never went idle while waiting for $what", e)
    }
}

private fun ComposeTestRule.dumpTrees() {
    runCatching {
        onAllNodes(isRoot()).fetchSemanticsNodes().indices.forEach { i ->
            onAllNodes(isRoot())[i].printToLog(DUMP_TAG, maxDepth = Int.MAX_VALUE)
        }
    }
}

/**
 * Puts [text] in a text field through its SetText action, without focusing it: performTextInput
 * focuses the field, which raises the soft keyboard, and on the CI emulator the keyboard's first
 * draw held the app's next frame past Espresso's idling timeout.
 */
fun SemanticsNodeInteraction.setTextWithoutKeyboard(text: String) {
    performSemanticsAction(SemanticsActions.SetText) { it(AnnotatedString(text)) }
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
 *
 * Lists with no room on their scroll axis are skipped too: performScrollToNode pages by the
 * viewport's own size until the list is at its end, so a 0 px list (the Styles list under a
 * peeking sheet at font scale 1.5, #1420) scrolls by nothing forever.
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
        if (!isGrid && node.canPage()) {
            runCatching { lists[i].performScrollToNode(matcher) }
        }
    }
}

/** Whether one viewport's step along each scroll axis moves the list: the same size performScrollToNode pages by. */
private fun SemanticsNode.canPage(): Boolean {
    val viewport = layoutInfo.coordinates.boundsInParent().size
    val vertical = SemanticsProperties.VerticalScrollAxisRange in config
    val horizontal = SemanticsProperties.HorizontalScrollAxisRange in config
    return (!vertical || viewport.height >= 1f) && (!horizontal || viewport.width >= 1f)
}

/** How long [scrollLazyListsTo] lets a node appear by itself before it scrolls for it. */
private const val SETTLE_TIMEOUT_MS = 3_000L

/** Matches a node whose text matches [regex] in full. */
fun hasTextMatching(regex: Regex): SemanticsMatcher =
    SemanticsMatcher("text matches $regex") { node ->
        val texts = node.config.getOrNull(SemanticsProperties.Text).orEmpty()
        texts.any { regex.matches(it.text) }
    }
