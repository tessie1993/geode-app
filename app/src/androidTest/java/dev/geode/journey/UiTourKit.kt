package dev.geode.journey

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.PointF
import android.os.SystemClock
import android.util.Log
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsNode
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.core.graphics.Insets
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import androidx.test.services.storage.TestStorage
import dev.geode.data.GeodePrefsFiles
import dev.geode.ui.MainActivity
import org.json.JSONArray
import org.json.JSONObject

/** The compose rule every tour case runs under. */
typealias TourRule = AndroidComposeTestRule<ActivityScenarioRule<MainActivity>, MainActivity>

/**
 * One tour case's walk: numbered screenshots, best-effort steps and the report rows that say what
 * each step expected and what it saw.
 *
 * Every step is best-effort by design. The tour exists to put the real UI in front of a design
 * audit, so a control that is missing, renamed or unreachable is a finding to write down, not a
 * reason to stop: a tour that dies at step 5 shows five screens. [step] catches the failure,
 * records it, still takes the screenshot (which usually shows why) and carries on. Only two
 * things end a case: the app crashing (the instrumentation process dies with it, and Orchestrator
 * reports it) and the activity never coming up ([requireShell]).
 *
 * Files go through [TestStorage] under `ui-tour/<config>/`: they survive Orchestrator's
 * clearPackageData, and the workflow pulls `/sdcard/googletest/test_outputfiles/ui-tour` after
 * each run. Each case writes its own `report-<NNN>-<area>.json` and `.md`, rewritten after every
 * row so a case that crashes halfway still leaves the rows it got to; the workflow merges them
 * into `ui-tour-report.md` and `ui-tour-report.json` next to the screenshots.
 */
class UiTour(
    val compose: TourRule,
    private val setup: TourSetup,
) {
    val config: TourConfig get() = setup.config
    private val case: TourCase get() = setup.case
    private val storage = TestStorage()
    private val rows = JSONArray()
    private var shots = 0
    private var device: JSONObject = JSONObject()
    private val context: Context = ApplicationProvider.getApplicationContext()
    private val dir: String get() = "ui-tour/${config.id}"
    private val reportName: String get() = "report-%03d-%s".format(case.index * CASE_STRIDE, case.area)

    /** The window, in screen pixels, as the gestures and the insets are measured. */
    var window: TourWindow = TourWindow.EMPTY
        private set

    // -- steps -------------------------------------------------------------------------------

    /**
     * Runs [block], screenshots the result and records a row. [block] may return a String, what
     * it observed, which goes in the row; anything else (Unit, a Boolean) leaves the screenshot
     * to say it. Never throws, except for the errors no test survives (out of memory, an
     * interrupt).
     */
    fun step(
        area: String,
        name: String,
        expected: String = "",
        block: () -> Any? = { null },
    ): Boolean {
        val started = SystemClock.uptimeMillis()
        val result = attempt(block)
        val shot = screenshot(area, name)
        row(
            area = area,
            step = name,
            shots = listOfNotNull(shot),
            expected = expected,
            observed = (result.getOrNull() as? String).orEmpty(),
            error = result.exceptionOrNull()?.let(::describe),
            ms = SystemClock.uptimeMillis() - started,
        )
        if (result.isFailure) ensureForeground()
        return result.isSuccess
    }

    /** A step that is all screenshot. */
    fun shot(
        area: String,
        name: String,
    ) = step(area, name)

    /**
     * The activity's first frame. The one step that may fail the case: with no shell there is no
     * tour, and a run that says "all steps failed" would hide that the app never started.
     */
    fun requireShell(
        area: String,
        what: SemanticsMatcher,
    ) {
        val up = step(area, "launch", "the app's first screen") { compose.awaitNode(what) }
        measureWindow()
        writeReport()
        check(up) { "the activity never came up (no node with ${what.description})" }
    }

    /** One report row; [step] and the gesture runner both write through it. */
    fun row(
        area: String,
        step: String,
        shots: List<String>,
        expected: String,
        observed: String,
        error: String? = null,
        ms: Long = 0L,
        extra: JSONObject? = null,
    ) {
        val row =
            JSONObject()
                .put("config", config.id)
                .put("case", case.area)
                .put("area", area)
                .put("step", step)
                .put("screenshots", JSONArray(shots))
                .put("expected", expected)
                .put("observed", observed)
                .put("error", error ?: JSONObject.NULL)
                .put("ms", ms)
        extra?.keys()?.forEach { key -> row.put(key, extra.get(key)) }
        rows.put(row)
        Log.i(TAG, "${config.id} $area/$step: ${error ?: observed.ifEmpty { "ok" }}")
        writeReport()
    }

    /** The next numbered screenshot, or null when the device would not give one. */
    fun screenshot(
        area: String,
        name: String,
    ): String? {
        runCatching { compose.waitForIdle() }
        SystemClock.sleep(SHOT_SETTLE_MS)
        val n = case.index * CASE_STRIDE + shots++
        val file = "%03d_%s_%s_%s_%s.png".format(n, slug(area), slug(name), config.orientationTag, config.fontTag)
        val bitmap = runCatching { TourDevice.automation.takeScreenshot() }.getOrNull() ?: return null
        return runCatching {
            storage.openOutputFile("$dir/$file").use { bitmap.compress(Bitmap.CompressFormat.PNG, PNG_QUALITY, it) }
            file
        }.getOrNull().also { bitmap.recycle() }
    }

    // -- finding and touching things ---------------------------------------------------------

    /** Whether any node, any window, matches; false while no compose window is up at all. */
    fun has(matcher: SemanticsMatcher): Boolean = nodes(matcher).isNotEmpty()

    fun nodes(matcher: SemanticsMatcher): List<SemanticsNode> =
        runCatching {
            compose.onAllNodes(matcher).fetchSemanticsNodes(atLeastOneRootRequired = false)
        }.getOrElse { emptyList() }

    /** [has] polled for up to [timeoutMs]. */
    fun waitFor(
        matcher: SemanticsMatcher,
        timeoutMs: Long = FIND_TIMEOUT_MS,
    ): Boolean {
        val deadline = SystemClock.uptimeMillis() + timeoutMs
        while (SystemClock.uptimeMillis() < deadline) {
            if (has(matcher)) return true
            SystemClock.sleep(POLL_MS)
        }
        return has(matcher)
    }

    /**
     * Taps the first clickable node labelled [label] (text, else content description), the way
     * the journeys' `tap` does, but with the tour's shorter wait: a missing control costs the
     * tour [FIND_TIMEOUT_MS], not the journeys' 20 s, since the tour expects to meet some.
     */
    fun tap(label: String) = tap(hasText(label) or hasContentDescription(label))

    fun tap(what: SemanticsMatcher) {
        val matcher = what and hasClickAction()
        compose.scrollLazyListsTo(matcher)
        check(waitFor(matcher)) { "no clickable node with ${what.description}" }
        val node = compose.onAllNodes(matcher).onFirst()
        runCatching { node.performScrollTo() }
        node.performClick()
        runCatching { compose.waitForIdle() }
    }

    /** Taps [label] if it is there; true when it was. For the optional bits of a screen. */
    fun tapIfPresent(label: String): Boolean = runCatching { tap(label) }.isSuccess

    /**
     * Runs the TalkBack custom action [label] on the first node offering it: how the tour reaches
     * the sheet's expand, collapse and close without a drag, as a TalkBack user does.
     */
    fun customAction(label: String): Boolean {
        val node = nodes(hasCustomAction(label)).firstOrNull() ?: return false
        val action = node.config.getOrNull(SemanticsActions.CustomActions)?.firstOrNull { it.label == label } ?: return false
        val done = compose.runOnUiThread { action.action() }
        runCatching { compose.waitForIdle() }
        return done
    }

    /** The first matching node's bounds in its window, or null. */
    fun bounds(matcher: SemanticsMatcher): Rect? = nodes(matcher).firstOrNull()?.boundsInWindow

    /** A point of the window as fractions of its width and height. */
    fun at(
        fx: Float,
        fy: Float,
    ) = PointF(window.width * fx, window.height * fy)

    /** A swipe up the middle of the screen: scrolls the list under it down by about half a page. */
    fun scrollDown() = TourTouch.swipe(at(0.5f, SCROLL_FROM), at(0.5f, SCROLL_TO))

    /**
     * Screenshots a scrolling screen a page at a time: [step] for its top, then up to [pages]
     * swipes, stopping early once a swipe changes nothing (the bottom of the list).
     */
    fun pages(
        area: String,
        name: String,
        pages: Int = DEFAULT_PAGES,
    ) {
        shot(area, name)
        var last = fingerprint()
        for (i in 1..pages) {
            val moved =
                attempt {
                    scrollDown()
                    val now = fingerprint()
                    (now != last).also { last = now }
                }.getOrDefault(false)
            if (!moved) break
            shot(area, "$name-scroll$i")
        }
    }

    /** Back, then a moment for the screen it lands on. */
    fun back() {
        TourTouch.back()
        runCatching { compose.waitForIdle() }
        SystemClock.sleep(SHORT_SETTLE_MS)
    }

    /**
     * Puts the app back in front after a step left it (an edge swipe that went home, a PiP
     * window): REORDER_TO_FRONT brings the same activity instance back, so the compose rule
     * keeps its hierarchy. A notification shade left open is closed first.
     */
    fun ensureForeground() {
        if (resumedActivity() != null) return
        TourDevice.shell("cmd statusbar collapse")
        SystemClock.sleep(SHORT_SETTLE_MS)
        if (resumedActivity() != null) return
        val intent =
            Intent(context, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
        runCatching { context.startActivity(intent) }
        SystemClock.sleep(FOREGROUND_SETTLE_MS)
    }

    fun resumedActivity(): Activity? =
        onMain {
            ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).firstOrNull()
        }

    /** Which window has input focus, reduced to who has it: app, shade, home, or the raw line. */
    fun focus(): String {
        val line =
            TourDevice
                .shell("dumpsys window")
                .lineSequence()
                .firstOrNull { "mCurrentFocus" in it }
                ?.trim()
                .orEmpty()
        return when {
            context.packageName in line -> "app"
            "NotificationShade" in line || "StatusBar" in line -> "shade"
            "launcher" in line.lowercase() -> "home"
            line.isEmpty() -> "unknown"
            else -> line
        }
    }

    // -- the screen and its insets -----------------------------------------------------------

    /**
     * Reads the window size and its insets from the activity's root view: the system gesture
     * insets (where Android owns a swipe: back at the sides, home at the bottom, the shade at the
     * top), the mandatory ones (which no app can opt out of) and the tappable element (where a
     * tap lands on the system bars). Also written into the report's device block.
     */
    fun measureWindow() {
        val activity = runCatching { compose.activity }.getOrNull() ?: return
        window =
            runCatching {
                onMain {
                    val view = activity.window.decorView
                    val insets = ViewCompat.getRootWindowInsets(view)
                    val loc = IntArray(2).also(view::getLocationOnScreen)
                    TourWindow(
                        width = view.width,
                        height = view.height,
                        left = loc[0],
                        top = loc[1],
                        density = view.resources.displayMetrics.density,
                        gestures = insets?.getInsets(WindowInsetsCompat.Type.systemGestures()) ?: Insets.NONE,
                        mandatory = insets?.getInsets(WindowInsetsCompat.Type.mandatorySystemGestures()) ?: Insets.NONE,
                        tappable = insets?.getInsets(WindowInsetsCompat.Type.tappableElement()) ?: Insets.NONE,
                        bars = insets?.getInsets(WindowInsetsCompat.Type.systemBars()) ?: Insets.NONE,
                    )
                }
            }.getOrDefault(TourWindow.EMPTY)
        device =
            window
                .toJson()
                .put("navigationMode", TourDevice.shell("settings get secure navigation_mode").trim())
                .put("fontScaleSetting", TourDevice.shell("settings get system font_scale").trim())
                .put("orientation", config.orientationTag)
                .put("fontScale", config.fontScale.toDouble())
        writeReport()
    }

    // -- what the app shows ------------------------------------------------------------------

    /** What the tour can read about the app's state at this moment, for before/after rows. */
    fun state(): TourState {
        val handle = bounds(hasContentDescription(SHEET_HANDLE))
        val selected =
            nodes(SemanticsMatcher.expectValue(SemanticsProperties.Selected, true)).mapNotNull { node ->
                val text = node.config.getOrNull(SemanticsProperties.Text)?.joinToString(" ") { it.text }
                val said = node.config.getOrNull(SemanticsProperties.ContentDescription)?.joinToString(" ")
                text?.takeIf { it.isNotBlank() } ?: said
            }
        val live = vizPrefs().getString(LIVE_STATE_KEY, null)
        return TourState(
            sheetTop = handle?.let { it.top / window.height.coerceAtLeast(1) },
            panel = has(hasContentDescription(PANEL_HANDLE)),
            quickSettings = has(hasText(QUICK_SETTINGS_TITLE)),
            macros = has(hasContentDescription(MACROS_EDIT)),
            paywall = has(hasText(PAYWALL_RESTORE)),
            chromeAwake = has(hasContentDescription(FULLSCREEN) and hasClickAction()),
            miniPlayer = has(hasCustomAction(MINI_PLAYER_OPEN)),
            recording = has(hasContentDescription(STOP_TAKE)),
            recordSaveAsked = has(hasText(RECORD_SAVE_TITLE)),
            selected = selected.sorted(),
            sceneId = live?.let { SCENE_ID.find(it)?.groupValues?.get(1) },
            paramsHash = live?.replace(SCENE_ID, "")?.hashCode() ?: 0,
            resumed = resumedActivity() != null,
        )
    }

    private fun vizPrefs() = context.getSharedPreferences(GeodePrefsFiles.VIZ, Context.MODE_PRIVATE)

    /** Texts and positions of what is on screen: equal before and after a swipe means it moved nothing. */
    private fun fingerprint(): String =
        nodes(SemanticsMatcher("any node") { true })
            .take(FINGERPRINT_NODES)
            .joinToString("|") { node ->
                val text = node.config.getOrNull(SemanticsProperties.Text)?.joinToString { it.text }.orEmpty()
                "$text@${node.boundsInWindow.top.toInt()}"
            }

    private fun writeReport() {
        val json =
            JSONObject()
                .put("config", config.id)
                .put("case", case.area)
                .put("caseIndex", case.index)
                .put("device", device)
                .put("rows", rows)
        runCatching {
            storage.openOutputFile("$dir/$reportName.json").use { it.write(json.toString(2).toByteArray()) }
            storage.openOutputFile("$dir/$reportName.md").use { it.write(markdown().toByteArray()) }
        }.onFailure { Log.w(TAG, "report not written", it) }
    }

    private fun markdown(): String =
        buildString {
            appendLine("## ${config.id} · ${case.area}")
            appendLine()
            appendLine("Device: `$device`")
            appendLine()
            appendLine("| step | screenshots | expected | observed | inset collision | error |")
            appendLine("|---|---|---|---|---|---|")
            for (i in 0 until rows.length()) {
                val r = rows.getJSONObject(i)
                val cells =
                    listOf(
                        "${r.getString("area")}/${r.getString("step")}",
                        r.getJSONArray("screenshots").join(" ").replace("\"", ""),
                        r.getString("expected"),
                        r.getString("observed"),
                        r.optString("insetCollision", ""),
                        if (r.isNull("error")) "" else r.getString("error"),
                    )
                appendLine(cells.joinToString(" | ", "| ", " |") { cell(it) })
            }
        }

    private fun cell(text: String) = text.replace("|", "\\|").replace("\n", " ")

    companion object {
        const val TAG = "UiTour"

        /** A case's screenshots number from `index * CASE_STRIDE`. */
        const val CASE_STRIDE = 100

        // Labels the state reads, as the app's strings have them (res/values). A rename shows up
        // as a state that never changes, which the report makes plain.
        const val SHEET_HANDLE = "Options sheet"
        const val PANEL_HANDLE = "Options panel edge"
        const val SHEET_CLOSE = "Hide the options"
        const val SHEET_EXPAND = "Show more options"
        const val SHEET_COLLAPSE = "Show fewer options"
        const val PANEL_WIDER = "Make the options wider"
        const val PANEL_NARROWER = "Make the options narrower"
        const val FULLSCREEN = "Show or hide the options"
        const val QUICK_SETTINGS_TITLE = "Visual settings"
        const val MACROS_EDIT = "Edit and make macros"
        const val PAYWALL_RESTORE = "Restore purchase"
        const val PAYWALL_CLOSE = "Close"
        const val MINI_PLAYER_OPEN = "Open Player"
        const val STOP_TAKE = "Stop recording this take"
        const val RECORD_SAVE_TITLE = "Where should takes be saved?"
        const val LIVE_STATE_KEY = "live_state"
        private val SCENE_ID = Regex("\"sceneId\"\\s*:\\s*\"([^\"]*)\"")

        const val FIND_TIMEOUT_MS = 6_000L
        private const val POLL_MS = 100L
        private const val SHOT_SETTLE_MS = 500L
        const val SHORT_SETTLE_MS = 600L
        private const val FOREGROUND_SETTLE_MS = 3_000L
        private const val PNG_QUALITY = 100
        private const val DEFAULT_PAGES = 4
        private const val FINGERPRINT_NODES = 40
        private const val SCROLL_FROM = 0.72f
        private const val SCROLL_TO = 0.40f
    }
}

/**
 * Runs [block], keeping its failure as a value. The errors no code survives (out of memory, a
 * stack overflow, an interrupt from the runner's timeout) are rethrown rather than written into a
 * report the process may not live to save.
 */
fun <T> attempt(block: () -> T): Result<T> =
    runCatching(block).onFailure { if (it is VirtualMachineError || it is InterruptedException) throw it }

/** A failure as one report cell: its type and the first line of its message. */
fun describe(error: Throwable): String {
    val message = error.message?.lineSequence()?.firstOrNull().orEmpty()
    return "${error.javaClass.simpleName}: $message".trimEnd(' ', ':')
}

/** A file-name-safe form of a step's name. */
fun slug(text: String): String = text.lowercase().replace(Regex("[^a-z0-9]+"), "-").trim('-').ifEmpty { "step" }

/** Matches a node offering the TalkBack custom action [label]. */
fun hasCustomAction(label: String): SemanticsMatcher =
    SemanticsMatcher("has custom action \"$label\"") { node ->
        node.config.getOrNull(SemanticsActions.CustomActions)?.any { it.label == label } == true
    }

/** The window the tour measured, in pixels, with its insets. */
data class TourWindow(
    val width: Int,
    val height: Int,
    val left: Int,
    val top: Int,
    val density: Float,
    val gestures: Insets,
    val mandatory: Insets,
    val tappable: Insets,
    val bars: Insets,
) {
    /** Whether [p] (window pixels) lies in a system gesture strip, and which: "left", "top", … */
    fun gestureEdge(p: PointF): String? = edge(p, gestures)

    fun mandatoryEdge(p: PointF): String? = edge(p, mandatory)

    private fun edge(
        p: PointF,
        insets: Insets,
    ): String? =
        when {
            p.x < insets.left -> "left"
            p.x > width - insets.right -> "right"
            p.y < insets.top -> "top"
            p.y > height - insets.bottom -> "bottom"
            else -> null
        }

    fun toJson(): JSONObject =
        JSONObject()
            .put("widthPx", width)
            .put("heightPx", height)
            .put("windowLeftPx", left)
            .put("windowTopPx", top)
            .put("density", density.toDouble())
            .put("systemGestures", insetsJson(gestures))
            .put("mandatorySystemGestures", insetsJson(mandatory))
            .put("tappableElement", insetsJson(tappable))
            .put("systemBars", insetsJson(bars))

    private fun insetsJson(i: Insets) =
        JSONObject()
            .put("left", i.left)
            .put("top", i.top)
            .put("right", i.right)
            .put("bottom", i.bottom)

    companion object {
        val EMPTY = TourWindow(0, 0, 0, 0, 1f, Insets.NONE, Insets.NONE, Insets.NONE, Insets.NONE)
    }
}

/**
 * The app's state as the tour can read it from outside: the semantics tree (which overlay is up,
 * where the sheet stands, which tab is selected) and the style on screen from the live state the
 * app saves (`geode-viz`, `live_state`), since the visual itself says nothing to semantics.
 */
data class TourState(
    /** The sheet handle's top as a fraction of the window height; null when there is no sheet. */
    val sheetTop: Float?,
    val panel: Boolean,
    val quickSettings: Boolean,
    val macros: Boolean,
    val paywall: Boolean,
    val chromeAwake: Boolean,
    val miniPlayer: Boolean,
    val recording: Boolean,
    val recordSaveAsked: Boolean,
    val selected: List<String>,
    val sceneId: String?,
    val paramsHash: Int,
    val resumed: Boolean,
) {
    /** The sheet's height by name, from where its handle stands. */
    val sheet: String
        get() =
            when {
                panel -> "panel"
                sheetTop == null -> "hidden"
                sheetTop < TALL_BELOW -> "tall"
                sheetTop < HALF_BELOW -> "half"
                else -> "peek"
            }

    val sheetShown: Boolean get() = sheet != "hidden"

    /** Everything that differs from [before], in words, for the report's observed column. */
    fun changesFrom(before: TourState): List<String> =
        buildList {
            if (sheet != before.sheet) add("sheet ${before.sheet}→$sheet")
            if (quickSettings != before.quickSettings) add(if (quickSettings) "quick settings opened" else "quick settings closed")
            if (macros != before.macros) add(if (macros) "macros drawer opened" else "macros drawer closed")
            if (paywall != before.paywall) add(if (paywall) "paywall opened" else "paywall closed")
            if (chromeAwake != before.chromeAwake) add(if (chromeAwake) "chrome woke" else "chrome slept")
            if (miniPlayer != before.miniPlayer) add(if (miniPlayer) "mini player shown" else "mini player hidden")
            if (recording != before.recording) add(if (recording) "recording started" else "recording stopped")
            if (recordSaveAsked && !before.recordSaveAsked) add("asked where takes go")
            if (selected != before.selected) add("selected ${before.selected}→$selected")
            if (sceneId != before.sceneId) add("style ${before.sceneId}→$sceneId")
            if (sceneId == before.sceneId && paramsHash != before.paramsHash) add("look's params changed")
            if (resumed != before.resumed) add(if (resumed) "app came back" else "app left the foreground")
        }

    fun summary(): String =
        listOfNotNull(
            "sheet=$sheet" + (sheetTop?.let { " (%.2f)".format(it) } ?: ""),
            "quick settings".takeIf { quickSettings },
            "macros".takeIf { macros },
            "paywall".takeIf { paywall },
            "chrome awake".takeIf { chromeAwake },
            "mini player".takeIf { miniPlayer },
            "recording".takeIf { recording },
            "style=$sceneId",
            "selected=$selected",
        ).joinToString(", ")

    private companion object {
        const val TALL_BELOW = 0.3f
        const val HALF_BELOW = 0.65f
    }
}
