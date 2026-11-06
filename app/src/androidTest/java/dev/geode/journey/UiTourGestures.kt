package dev.geode.journey

import android.graphics.PointF
import android.os.SystemClock
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import dev.geode.data.BehaviourPrefs
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import org.json.JSONObject
import kotlin.math.max
import kotlin.math.min

/**
 * One gesture the tour drives on the full-screen visual, with where it starts and ends (window
 * pixels) so the report can say whether it collided with a system gesture inset.
 *
 * [gesture] is the [CanvasGesture] it is meant to be, whose expected action comes from
 * [BehaviourPrefs.DEFAULT_GESTURES], the owner's map, rather than from a copy here: a remapped
 * default shows up in the report as a new expectation, not as a wrong one. [edge] marks the
 * deliberately edge-started variants, which the app is expected to leave to Android.
 */
class TourGesture(
    val name: String,
    val gesture: CanvasGesture?,
    val start: PointF,
    val end: PointF,
    val edge: Boolean = false,
    val drive: () -> Unit,
) {
    val expectedAction: GestureAction? get() = gesture?.let { BehaviourPrefs.DEFAULT_GESTURES[it] ?: GestureAction.OFF }
}

/**
 * The gestures on the visual, in the order the tour drives them: every one-finger and
 * two-finger gesture of `ui/VisualGestures.kt` with the defaults' action beside it. Positions are
 * fractions of the window, kept clear of the stage icons (‹ › at the sides' middle, the icon row
 * top-right, Record bottom-right) and of the mini player and tab bar at the bottom, so a gesture
 * that does nothing is the detector's doing, not a button's.
 */
fun UiTour.visualGestures(): List<TourGesture> {
    val r = min(window.width, window.height) * SWIRL_RADIUS
    val centre = at(0.5f, 0.42f)
    val pinchA = UiTourTouch.line(at(0.45f, 0.42f), at(0.22f, 0.42f), PAIR_STEPS)
    val pinchB = UiTourTouch.line(at(0.55f, 0.42f), at(0.78f, 0.42f), PAIR_STEPS)
    val dragA = UiTourTouch.line(at(0.38f, 0.32f), at(0.38f, 0.56f), PAIR_STEPS)
    val dragB = UiTourTouch.line(at(0.62f, 0.32f), at(0.62f, 0.56f), PAIR_STEPS)
    return listOf(
        TourGesture("tap-top", CanvasGesture.TAP_TOP, at(0.5f, 0.2f), at(0.5f, 0.2f)) { UiTourTouch.tap(at(0.5f, 0.2f)) },
        TourGesture("tap-bottom", CanvasGesture.TAP_BOTTOM, at(0.5f, 0.6f), at(0.5f, 0.6f)) { UiTourTouch.tap(at(0.5f, 0.6f)) },
        TourGesture("double-tap", CanvasGesture.DOUBLE_TAP, centre, centre) { UiTourTouch.doubleTap(centre) },
        swipe("swipe-left", CanvasGesture.SWIPE_SIDEWAYS, at(0.8f, 0.4f), at(0.2f, 0.4f)),
        swipe("swipe-right", CanvasGesture.SWIPE_SIDEWAYS, at(0.2f, 0.4f), at(0.8f, 0.4f)),
        swipe("swipe-up", CanvasGesture.SWIPE_UP, at(0.5f, 0.62f), at(0.5f, 0.28f)),
        swipe("swipe-down", CanvasGesture.SWIPE_DOWN, at(0.5f, 0.24f), at(0.5f, 0.6f)),
        TourGesture("hold", CanvasGesture.LONG_PRESS, centre, centre) { UiTourTouch.hold(centre, HOLD_MS) },
        TourGesture("swirl", CanvasGesture.SWIRL, PointF(centre.x + r, centre.y), PointF(centre.x + r, centre.y)) {
            UiTourTouch.circles(centre, r, SWIRL_TURNS)
        },
        TourGesture("two-finger-drag", CanvasGesture.TWO_FINGER_DRAG, dragA.first(), dragA.last()) {
            UiTourTouch.twoFingers(dragA, dragB)
        },
        TourGesture("pinch", CanvasGesture.PINCH_TWIST, pinchA.first(), pinchA.last()) { UiTourTouch.twoFingers(pinchA, pinchB) },
    )
}

/**
 * The same swipes started inside the system gesture insets, plus a tap there. The detector
 * ignores a swipe or swirl that starts in `WindowInsets.systemGestures` (the strips are
 * Android's), but still takes a tap. Where the emulator reports no inset on a side, the start is
 * [EDGE_FALLBACK_PX] from the edge: the outermost pixels are what a thumb hits first anyway.
 */
fun UiTour.edgeGestures(): List<TourGesture> {
    val w = window.width.toFloat()
    val h = window.height.toFloat()
    val g = window.gestures
    val left = edgeStart(g.left)
    val right = w - edgeStart(g.right)
    val top = edgeStart(g.top)
    val bottom = h - edgeStart(g.bottom)
    return listOf(
        swipe("edge-swipe-right-from-left", CanvasGesture.SWIPE_SIDEWAYS, PointF(left, h * 0.45f), at(0.65f, 0.45f), edge = true),
        swipe("edge-swipe-left-from-right", CanvasGesture.SWIPE_SIDEWAYS, PointF(right, h * 0.45f), at(0.35f, 0.45f), edge = true),
        swipe("edge-swipe-down-from-top", CanvasGesture.SWIPE_DOWN, PointF(w * 0.5f, top), at(0.5f, 0.5f), edge = true),
        swipe("edge-swipe-up-from-bottom", CanvasGesture.SWIPE_UP, PointF(w * 0.5f, bottom), at(0.5f, 0.5f), edge = true),
        TourGesture("edge-tap-top", CanvasGesture.TAP_TOP, PointF(w * 0.5f, top), PointF(w * 0.5f, top), edge = true) {
            UiTourTouch.tap(PointF(w * 0.5f, top))
        },
    )
}

private fun swipe(
    name: String,
    gesture: CanvasGesture,
    from: PointF,
    to: PointF,
    edge: Boolean = false,
) = TourGesture(name, gesture, from, to, edge) { UiTourTouch.swipe(from, to) }

private fun edgeStart(insetPx: Int): Float = max(insetPx / 2f, EDGE_FALLBACK_PX)

/**
 * Drives [g] from a clean stage and writes one row: the screenshots before and after, the
 * expected action, what changed (sheet, overlays, style, params, focus), whether its start or end
 * fell in a system gesture inset, and a verdict. [prepare] puts the stage back to the visual
 * alone (sheet hidden, no overlay, chrome asleep) so each gesture is judged on its own.
 */
fun UiTour.runGesture(
    area: String,
    g: TourGesture,
    prepare: () -> Unit,
) {
    val started = SystemClock.uptimeMillis()
    attempt(prepare)
    val before = state()
    val shotBefore = screenshot(area, "${g.name}-before")
    val driven = attempt(g.drive)
    val after = awaitChange(before, GESTURE_TIMEOUT_MS, GESTURE_SETTLE_MS)
    val focus = focus()
    val shotAfter = screenshot(area, "${g.name}-after")
    val changes = after.changesFrom(before)
    val startEdge = window.gestureEdge(g.start)
    val endEdge = window.gestureEdge(g.end)
    val safeStart = window.safeGesturesEdge(g.start)
    val safeEnd = window.safeGesturesEdge(g.end)
    val collision = startEdge != null || endEdge != null || safeStart != null || safeEnd != null
    // Focus alone over-reads: a popup of the app's own has no package in its window name.
    val systemTook = !after.resumed || focus == "shade" || focus == "home"
    // A side swipe the system turns into back reaches the app as back, which brings a hidden
    // sheet back: the app reacting, but to Android's gesture rather than its own.
    val backRan = g.gesture == CanvasGesture.SWIPE_SIDEWAYS && !before.sheetShown && after.sheetShown
    val verdict =
        when {
            driven.isFailure -> "error"
            g.edge && systemTook -> "system took it"
            g.edge && backRan -> "system took it (back; the app's back brought the sheet up)"
            g.edge && changes.isEmpty() -> "neither reacted"
            g.edge -> "app reacted"
            else -> judge(g.expectedAction, before, after)
        }
    val expected =
        if (g.edge) {
            "Android's (back, home or shade); the app ignores a swipe that starts in the inset, a tap is still the app's"
        } else {
            "${g.expectedAction} (${g.gesture})"
        }
    val extra =
        JSONObject()
            .put("gesture", g.gesture?.name ?: JSONObject.NULL)
            .put("expectedAction", g.expectedAction?.name ?: JSONObject.NULL)
            .put("start", point(g.start))
            .put("end", point(g.end))
            .put("startInset", startEdge ?: JSONObject.NULL)
            .put("endInset", endEdge ?: JSONObject.NULL)
            .put("startMandatoryInset", window.mandatoryEdge(g.start) ?: JSONObject.NULL)
            .put("startSafeGesturesInset", safeStart ?: JSONObject.NULL)
            .put("endSafeGesturesInset", safeEnd ?: JSONObject.NULL)
            .put("insetCollision", if (collision) "yes" else "no")
            .put("focusAfter", focus)
            .put("systemTook", systemTook)
            .put("appReacted", changes.any { it != "app left the foreground" })
            .put("verdict", verdict)
            .put("before", before.summary())
            .put("after", after.summary())
    row(
        area = area,
        step = g.name,
        shots = listOfNotNull(shotBefore, shotAfter),
        expected = expected,
        observed = "$verdict: " + changes.ifEmpty { listOf("nothing changed") }.joinToString("; ") + "; focus=$focus",
        error = driven.exceptionOrNull()?.let(::describe),
        ms = SystemClock.uptimeMillis() - started,
        extra = extra,
    )
    ensureForeground()
}

/**
 * Whether what changed is what [action] does. Null where the semantics tree cannot see the
 * effect at all: touch play stirs the visual and play/pause only the sound, and neither leaves
 * anything a test can read, so the screenshots are the evidence.
 */
private fun judge(
    action: GestureAction?,
    before: TourState,
    after: TourState,
): String {
    val paywalled = after.paywall && !before.paywall
    val ok: Boolean? =
        when (action) {
            null -> null
            GestureAction.OFF -> after.changesFrom(before).isEmpty()
            GestureAction.SHOW_MINI_PLAYER -> (after.miniPlayer && !before.miniPlayer) || (after.chromeAwake && !before.chromeAwake)
            GestureAction.QUICK_SETTINGS -> after.quickSettings && !before.quickSettings
            GestureAction.TOGGLE_SHEET -> after.sheetShown != before.sheetShown
            GestureAction.STEP_STYLE -> after.sceneId != before.sceneId
            GestureAction.STEP_PRESET -> after.sceneId != before.sceneId || after.paramsHash != before.paramsHash
            GestureAction.STEP_PLAYLIST -> after.sceneId != before.sceneId || after.paramsHash != before.paramsHash
            GestureAction.MACROS -> after.macros && !before.macros
            GestureAction.RECORD -> after.recording || after.recordSaveAsked || paywalled
            GestureAction.RANDOMIZE_ALL -> after.paramsHash != before.paramsHash || paywalled
            GestureAction.TOUCH_PLAY -> null
            GestureAction.PLAY_PAUSE -> null
        }
    return when {
        ok == null -> "not visible to the test (see screenshots)"
        ok && paywalled -> "as expected (Pro gate: paywall)"
        ok -> "as expected"
        else -> "MISMATCH"
    }
}

private fun point(p: PointF) =
    JSONObject()
        .put("x", p.x.toDouble())
        .put("y", p.y.toDouble())

/**
 * Back to the visual alone: overlays closed with back (as a user would), a running take
 * stopped, the sheet hidden with its ×, and the chrome left to fall asleep, so a tap on the
 * bottom is judged on waking it rather than on a chrome that was already awake.
 */
fun UiTour.stageBaseline() {
    for (i in 0 until MAX_DISMISSALS) {
        val s = state()
        if (!(s.quickSettings || s.macros || s.paywall || s.recordSaveAsked)) break
        back()
    }
    if (!has(hasContentDescription(LIVE_VISUAL))) runCatching { tap(VISUALS) }
    if (state().recording) runCatching { tap(UiTour.STOP_TAKE) }
    hideSheet()
    val deadline = SystemClock.uptimeMillis() + CHROME_SLEEP_MS
    while (has(hasContentDescription(UiTour.FULLSCREEN) and hasClickAction()) && SystemClock.uptimeMillis() < deadline) {
        SystemClock.sleep(POLL_MS)
    }
}

/** Hides the options sheet (or side panel) with its ×, else its TalkBack close action. */
fun UiTour.hideSheet() {
    if (!state().sheetShown) return
    val close = hasContentDescription(UiTour.SHEET_CLOSE) and hasClickAction()
    if (has(close)) runCatching { tap(close) } else customAction(UiTour.SHEET_CLOSE)
    SystemClock.sleep(UiTour.SHORT_SETTLE_MS)
}

/** The visual's own accessibility name (`visuals_gesture_surface`): present only on Visuals. */
const val LIVE_VISUAL = "Live visual"
const val VISUALS = "Visuals"

private const val HOLD_MS = 1_400L
private const val SWIRL_TURNS = 2
private const val SWIRL_RADIUS = 0.2f
private const val PAIR_STEPS = 24
private const val EDGE_FALLBACK_PX = 3f
private const val MAX_DISMISSALS = 3
private const val CHROME_SLEEP_MS = 12_000L
private const val POLL_MS = 150L

/**
 * How long a gesture's effect may take to show: a single tap waits out the double-tap timeout
 * before it fires, and a hold fills its ring first. A gesture that changes nothing (pinch, touch
 * play) costs this whole wait, which is the price of not judging "nothing" too early.
 */
private const val GESTURE_TIMEOUT_MS = 3_000L

/** Once something changed: the live state the style is read from is saved through a 400 ms window (`VizStateStore`). */
private const val GESTURE_SETTLE_MS = 700L
