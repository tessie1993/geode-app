package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.safeGestures
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameMillis
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerEvent
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onLayoutRectChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.toOffset
import androidx.compose.ui.unit.toSize
import dev.geode.R
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import dev.geode.pro.FeatureGate
import dev.geode.render.TouchField
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin
import android.view.ViewConfiguration as PlatformViewConfiguration

/**
 * Where the long-press guard ring is drawn, if anywhere. The detector sets it once the finger has
 * held still for the system's long-press timeout and clears it when the finger moves, lifts or the hold fires; the
 * ring ([HoldRing]) fills itself from that moment, so neither side recomposes per frame.
 */
@Stable
internal class HoldRingState {
    var at by mutableStateOf<Offset?>(null)
}

/**
 * The things a gesture on the visual can do, one lambda per [GestureAction]. The detector only
 * says which [CanvasGesture] happened; [run] looks the action up in Settings › Gestures & controls'
 * map, so any gesture can be given any action and the detector never learns what they mean.
 * [stepStyle], [stepPreset] and [stepPlaylist] take +1 for next and -1 for previous; a gesture with
 * no direction of its own steps forward.
 */
internal class VisualActions(
    val showMiniPlayer: () -> Unit,
    val quickSettings: () -> Unit,
    val toggleSheet: () -> Unit,
    val stepStyle: (Int) -> Unit,
    val stepPreset: (Int) -> Unit,
    val stepPlaylist: (Int) -> Unit,
    val macros: () -> Unit,
    val record: () -> Unit,
    val randomizeAll: () -> Unit,
    val touchPulse: () -> Unit,
    val playPause: () -> Unit,
) {
    fun run(
        action: GestureAction,
        step: Int,
    ) {
        val dir = if (step < 0) -1 else 1
        when (action) {
            GestureAction.OFF -> Unit
            GestureAction.SHOW_MINI_PLAYER -> showMiniPlayer()
            GestureAction.QUICK_SETTINGS -> quickSettings()
            GestureAction.TOGGLE_SHEET -> toggleSheet()
            GestureAction.STEP_STYLE -> stepStyle(dir)
            GestureAction.STEP_PRESET -> stepPreset(dir)
            GestureAction.STEP_PLAYLIST -> stepPlaylist(dir)
            GestureAction.MACROS -> macros()
            GestureAction.RECORD -> record()
            GestureAction.RANDOMIZE_ALL -> randomizeAll()
            // A discrete gesture given touch play stirs the visual once, the way TalkBack's action does.
            GestureAction.TOUCH_PLAY -> touchPulse()
            GestureAction.PLAY_PAUSE -> playPause()
        }
    }
}

/**
 * Every gesture on the full-screen visual, read by one detector, because pointers belong to whoever
 * reads them first: two detectors would each see half a gesture.
 *
 * One finger: a tap (top or bottom of the visible visual, split at [tapSplitPx]), a double tap, a
 * swipe sideways, up or down past [SWIPE_DP], a swirl (two circles drawn with the finger, counted as
 * the heading's total turn) and a long press held through the red ring. Two fingers are a drag while
 * they move together, and a pinch / twist from the moment their spread or angle changes past its
 * threshold; whichever of the two is given [GestureAction.TOUCH_PLAY] hands the fingers to the
 * visualizer's touch field ([onTouchPoints], in y-up NDC) while it lasts, which is what touch play
 * is. While [onPinch] is given (Settings › Gestures & controls › Touch › Pinch and twist the canvas), a pinch also
 * hands it each event's scale factor and twist in degrees, which move the Zoom and Rotation sliders
 * (clamped natively, `core/viz/TouchTransform`). One finger never touches the visual (ui-flow
 * decisions).
 *
 * A swipe, a swirl, a two-finger drag or a pinch only counts when it starts outside
 * `WindowInsets.safeGestures`, and for two fingers only when both downs do: those strips stay
 * Android's (back at the sides, home at the bottom, the shade at the top). Compose's insets guide
 * names `safeGestures` for exactly this ("avoids system gestures clashing with app gestures"); it
 * is the union of the system, mandatory and tappable gesture insets, so it also covers the
 * three-button bar's and the tappable shade's edge, which `systemGestures` alone misses. Insets are
 * distances from the window's edges, so a down is compared in window coordinates
 * (the stage's window position, kept by [WindowPlacement]; the window itself is
 * `LocalWindowInfo.containerSize` from (0, 0)): in the expanded shell the stage sits beside the rail
 * inside `safeDrawingPadding()`, and its own (0, 0) is not the window's. Until the stage's first
 * placement is reported no down is in a strip ([WindowPlacement.isOurs]): an unplaced origin would
 * put every down in the top-left strips and drop the first gesture after the screen opens. A tap in
 * a strip is still ours, since Android does nothing with a tap.
 *
 * The ring shows at `ViewConfiguration.longPressTimeoutMillis`, which on Android is the platform's
 * long-press timeout, so it follows Accessibility › Touch & hold delay; a lift before that is a tap,
 * a lift once the ring shows does nothing, as decided: the ring is the guard, and a press that long
 * was not a tap. The ring then fills over [HOLD_FILL_MS].
 *
 * The double tap waits out the double-tap timeout before a single tap fires, so it is only waited
 * for while the map gives the double tap an action. The second down counts only between
 * `doubleTapMinTimeMillis` and `doubleTapTimeoutMillis` after the first lift (the window Compose's
 * own `detectTapGestures` uses) and within the platform's double-tap slop of the first
 * (`android.view.ViewConfiguration.getScaledDoubleTapSlop`; Compose's `ViewConfiguration` has no
 * such slop). Anything else, or a second touch that wanders past the touch slop, fires the single
 * tap and is read again from its own down as a new gesture, so a quick tap then a swipe keeps the
 * swipe.
 *
 * A swirl attempt (the heading has turned past [SWIRL_ATTEMPT_RAD]) never falls through to a
 * swipe when it lifts short of two circles: a half-drawn circle was not meant to step the style.
 *
 * TalkBack users get each mapped action as a custom action ([a11yActions]): with TalkBack on, a
 * two-finger drag is its scroll, so the gestures themselves are out of reach.
 *
 * Callbacks go through [rememberUpdatedState] because the pointer loop is keyed only on the
 * insets, so a remapped gesture or a new lambda never restarts a gesture in flight.
 */
@Composable
internal fun Modifier.visualGestures(
    actionFor: (CanvasGesture) -> GestureAction,
    ring: HoldRingState,
    tapSplitPx: () -> Float,
    holdArmed: Boolean,
    onGesture: (CanvasGesture, Int) -> Unit,
    onTouchPoints: (FloatArray, Int) -> Unit,
    onPinch: ((zoomFactor: Float, degrees: Float) -> Unit)?,
    a11yActions: List<CustomAccessibilityAction>,
    onTapDown: (Long) -> Unit = {},
): Modifier {
    val action by rememberUpdatedState(actionFor)
    val tapDown by rememberUpdatedState(onTapDown)
    val split by rememberUpdatedState(tapSplitPx)
    val armed by rememberUpdatedState(holdArmed)
    val fire by rememberUpdatedState(onGesture)
    val touch by rememberUpdatedState(onTouchPoints)
    val pinch by rememberUpdatedState(onPinch)
    val haptics = LocalHapticFeedback.current
    val edges = WindowInsets.safeGestures
    val direction = LocalLayoutDirection.current
    val surface = stringResource(R.string.visuals_gesture_surface)
    val context = LocalContext.current
    val doubleSlop = remember(context) { PlatformViewConfiguration.get(context).scaledDoubleTapSlop.toFloat() }
    val placement = remember { WindowPlacement() }
    val windowInfo = LocalWindowInfo.current
    return this
        .windowPlacement(placement)
        .pointerInput(edges, direction, doubleSlop) {
            val points = FloatArray(TouchField.MAX_POINTS * 2)
            // Read per down, not once: the strips change with rotation and the navigation mode.
            val ours = { local: Offset ->
                val safe =
                    gestureSafeArea(
                        window = Rect(Offset.Zero, windowInfo.containerSize.toSize()),
                        left = edges.getLeft(this, direction).toFloat(),
                        top = edges.getTop(this).toFloat(),
                        right = edges.getRight(this, direction).toFloat(),
                        bottom = edges.getBottom(this).toFloat(),
                    )
                placement.isOurs(local, safe)
            }
            awaitEachGesture {
                var next: PointerInputChange? = awaitFirstDown(requireUnconsumed = false)
                // A touch the double-tap wait saw but could not use comes back round as `next`.
                while (next != null) {
                    val down: PointerInputChange = next
                    next = null
                    val start = down.position
                    val startOurs = ours(start)
                    val read =
                        GestureRead(
                            start = start,
                            startMs = down.uptimeMillis,
                            holds = armed && action(CanvasGesture.LONG_PRESS) != GestureAction.OFF,
                            swirls = startOurs && action(CanvasGesture.SWIRL) != GestureAction.OFF,
                            slop = viewConfiguration.touchSlop,
                            stepPx = SWIRL_STEP_DP.dp.toPx(),
                            showMs = viewConfiguration.longPressTimeoutMillis,
                            pairOurs = { a, b -> startOurs && ours(a) && ours(b) },
                        )
                    // Two fingers touch the visual only while what they are doing so far (a drag, or a
                    // pinch / twist once the spread or angle has changed) is mapped to touch play; the
                    // moment it reads as something else they let go of it.
                    val publishTo: (PointerEvent) -> Unit = { event ->
                        val kind = read.pairKind
                        read.pinchStep()?.let { (factor, degrees) -> pinch?.invoke(factor, degrees) }
                        if (kind != null && action(kind) == GestureAction.TOUCH_PLAY) {
                            publish(event, points, size.width.toFloat(), size.height.toFloat(), touch)
                            read.published = true
                        } else if (read.published) {
                            touch(points, 0)
                            read.published = false
                        }
                    }
                    try {
                        follow(
                            read = read,
                            downId = down.id,
                            ring = ring,
                            onHold = {
                                haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                fire(CanvasGesture.LONG_PRESS, 0)
                            },
                            onSwirl = {
                                haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                fire(CanvasGesture.SWIRL, 0)
                            },
                            publishTo = publishTo,
                        )
                    } finally {
                        // Also on cancellation (the visual leaves mid-gesture): a ring or a published
                        // finger left behind would stay on screen, or keep steering, forever.
                        ring.at = null
                        if (read.published) touch(points, 0)
                    }
                    val far = SWIPE_DP.dp.toPx()
                    when {
                        read.multi -> read.pairGesture(far)?.takeIf { action(it) != GestureAction.TOUCH_PLAY }?.let { fire(it, 0) }
                        read.done -> Unit
                        read.moved && startOurs && !read.swirling -> read.swipe(far)?.let { (g, step) -> fire(g, step) }
                        read.moved -> Unit
                        read.ringShown || read.now - read.startMs > read.showMs -> Unit
                        else -> {
                            tapDown(down.uptimeMillis)
                            val tap = if (start.y < split()) CanvasGesture.TAP_TOP else CanvasGesture.TAP_BOTTOM
                            val second =
                                if (action(CanvasGesture.DOUBLE_TAP) != GestureAction.OFF) {
                                    awaitSecondTap(start, read.now, doubleSlop, read.slop)
                                } else {
                                    SecondTap.NONE
                                }
                            if (second.double) tapDown(second.downMs)
                            fire(if (second.double) CanvasGesture.DOUBLE_TAP else tap, 0)
                            next = second.next
                        }
                    }
                }
            }
        }.semantics {
            // Named, so TalkBack can land on the visual at all and offer its actions there.
            contentDescription = surface
            customActions = a11yActions
        }
}

/**
 * Follows one gesture's pointers until every finger is off (or the finger that started it is).
 * Fires the hold and the swirl as they happen, since both are decided mid-gesture; everything
 * else is decided from [read] once this returns.
 */
private suspend fun AwaitPointerEventScope.follow(
    read: GestureRead,
    downId: PointerId,
    ring: HoldRingState,
    onHold: () -> Unit,
    onSwirl: () -> Unit,
    publishTo: (PointerEvent) -> Unit,
) {
    while (true) {
        val event =
            if (read.waitingOnHold) {
                withTimeoutOrNull((read.holdDeadline - read.now).coerceAtLeast(1L)) { awaitPointerEvent() }
            } else {
                awaitPointerEvent()
            }
        when {
            event == null -> if (read.holdTick(ring)) onHold()
            read.pairing(event, ring) -> {
                if (read.trackPair(event)) return
                publishTo(event)
            }
            else -> {
                val change = event.changes.firstOrNull { it.id == downId }
                if (change == null || !change.pressed) return
                if (read.moveTo(change.position, ring)) onSwirl()
            }
        }
    }
}

/**
 * What the wait for a second tap found: a [double] tap, or not, and then maybe the [next] down,
 * a touch that was not a second tap and is read again from its own start as a gesture of its own.
 */
private class SecondTap(
    val double: Boolean,
    val next: PointerInputChange?,
    val downMs: Long = 0L,
) {
    companion object {
        val NONE = SecondTap(double = false, next = null)
    }
}

/**
 * Waits up to the double-tap timeout after the first lift (at [firstUpMs]) for a second finger,
 * then for it to lift. A down sooner than `doubleTapMinTimeMillis` is skipped as a bounce, as
 * Compose's `detectTapGestures` does. The second down must land within [doubleSlop] of [first]
 * (the platform's double-tap slop, [isSecondTap]) and stay within [slop] (the touch slop) until it
 * lifts; otherwise it is handed back as [SecondTap.next] the moment that is known, so the gesture
 * it starts (a swipe after a quick tap, say) is read whole instead of swallowed here. Handing it
 * back rather than returning matters: `awaitEachGesture` waits for every finger to lift before its
 * next round, so a finger still down when this returns would never be read.
 */
private suspend fun AwaitPointerEventScope.awaitSecondTap(
    first: Offset,
    firstUpMs: Long,
    doubleSlop: Float,
    slop: Float,
): SecondTap {
    val earliest = firstUpMs + viewConfiguration.doubleTapMinTimeMillis
    val second =
        withTimeoutOrNull(viewConfiguration.doubleTapTimeoutMillis) {
            var down: PointerInputChange
            do {
                down = awaitFirstDown(requireUnconsumed = false)
            } while (down.uptimeMillis < earliest)
            down
        } ?: return SecondTap.NONE
    if (!isSecondTap(second.position - first, doubleSlop)) return SecondTap(double = false, next = second)
    do {
        val change = awaitPointerEvent().changes.firstOrNull { it.id == second.id }
        if (change != null && (change.position - second.position).getDistance() > slop) {
            return SecondTap(double = false, next = second)
        }
    } while (change != null && change.pressed)
    return SecondTap(double = true, next = null, downMs = second.uptimeMillis)
}

/**
 * Whether a second down [fromFirst] (its offset from the first tap's down) is close enough to be
 * the second tap of a double tap: within [doubleTapSlopPx], the platform's `scaledDoubleTapSlop`.
 * Further away it is a new touch, not a double tap.
 */
internal fun isSecondTap(
    fromFirst: Offset,
    doubleTapSlopPx: Float,
): Boolean = fromFirst.getDistance() <= doubleTapSlopPx

/**
 * Whether a finger's heading has turned far enough ([turnRad], either way) that the gesture is a
 * swirl attempt: past half a circle ([SWIRL_ATTEMPT_RAD]). A swipe's heading wobbles but never
 * turns back on itself, so an arc this far round was a circle being drawn, and a swirl given up
 * short of two circles must not step the style as a swipe would.
 */
internal fun swirlAttempted(turnRad: Float): Boolean = abs(turnRad) > SWIRL_ATTEMPT_RAD

/**
 * Where a layout sits in the window, kept from its last reported placement so a pointer handler,
 * which only gets local positions, can compare a down with the window insets (measured from the
 * window's edges). A plain object, not state: nothing draws from it, so a move must not recompose.
 *
 * [placed] stays false until the first report. Before it [origin] is (0, 0) whatever the stage's
 * real place, so [isOurs] gives every down to the app rather than guess: the strips are back from
 * the first report on, which `onLayoutRectChanged` sends one debounce after the first layout.
 */
internal class WindowPlacement {
    var origin = Offset.Zero
    var placed = false

    fun toWindow(local: Offset): Offset = origin + local

    /** Whether a down at [local] is the app's: anywhere until placed, then only inside [safe] (window px). */
    fun isOurs(
        local: Offset,
        safe: Rect,
    ): Boolean = !placed || safe.contains(toWindow(local))
}

/**
 * Keeps [placement] current through `Modifier.onLayoutRectChanged`, not `onGloballyPositioned`: the
 * jetpack-compose-performance skill (references/layouts.md, "Throttled Bounds Tracking") names it
 * for window-relative bounds, since it is throttled and debounced where `onGloballyPositioned` runs
 * after every layout pass that may move the node. The stage only moves when the shell relayouts
 * (rotation, the rail, a resized window), so the default 64 ms debounce costs nothing: the report
 * lands once the move settles, and a down before it is read by [WindowPlacement.isOurs].
 * `RelativeLayoutBounds.positionInWindow` is the node's top-left in window coordinates, the frame
 * the insets are in.
 */
internal fun Modifier.windowPlacement(placement: WindowPlacement): Modifier =
    onLayoutRectChanged { bounds ->
        placement.origin = bounds.positionInWindow.toOffset()
        placement.placed = true
    }

/**
 * The part of [window] (window px) outside the gesture strips, given each inset in px: a down
 * inside it is the app's, a down outside it is left to the system.
 */
internal fun gestureSafeArea(
    window: Rect,
    left: Float,
    top: Float,
    right: Float,
    bottom: Float,
): Rect = Rect(window.left + left, window.top + top, window.right - right, window.bottom - bottom)

/** The fingers that are down in [event], in y-up NDC (the scenes' frame), handed to [send]. */
private fun publish(
    event: PointerEvent,
    points: FloatArray,
    width: Float,
    height: Float,
    send: (FloatArray, Int) -> Unit,
) {
    val w = width.coerceAtLeast(1f)
    val h = height.coerceAtLeast(1f)
    var live = 0
    for (change in event.changes) {
        if (!change.pressed || live >= TouchField.MAX_POINTS) continue
        points[live * 2] = change.position.x / w * 2f - 1f
        points[live * 2 + 1] = 1f - change.position.y / h * 2f
        live++
    }
    send(points, live)
}

/** One gesture's running totals, so the pointer loop above stays a list of decisions. */
private class GestureRead(
    val start: Offset,
    val startMs: Long,
    private val holds: Boolean,
    private val swirls: Boolean,
    val slop: Float,
    private val stepPx: Float,
    /** When the ring shows: the system's long-press timeout. */
    val showMs: Long,
    /** Whether a pair that came down at these two local points is ours (both outside the strips). */
    private val pairOurs: (Offset, Offset) -> Boolean,
) {
    var moved = false
    var multi = false

    /** The heading has turned past half a circle: a swirl attempt, never a swipe ([swirlAttempted]). */
    var swirling = false
        private set

    /** Both downs of the pair were outside the strips; a pair that was not stays unread. */
    private var pairInside = false

    /** A hold or a swirl already fired: the lift that follows means nothing. */
    var done = false
    var ringShown = false
    var published = false

    /** The latest event's time; the hold timer counts from [startMs] to it. */
    var now = startMs
    private var last = start
    private var segmentStart = start
    private var heading = Float.NaN
    private var turn = 0f
    private var pairStart = Offset.Zero
    private var spanStart = 1f
    private var angleStart = 0f
    private var pan = Offset.Zero
    private var zoom = 1f
    private var twist = 0f
    private var lastZoom = 1f
    private var lastTwist = 0f

    /**
     * What two fingers are doing, once they have shown it: [CanvasGesture.TWO_FINGER_DRAG] when
     * they move together past the slop, [CanvasGesture.PINCH_TWIST] as soon as their spread or
     * angle changes past its threshold. A pinch stays a pinch: it never turns back into a drag.
     */
    var pairKind: CanvasGesture? = null
        private set

    /** Whether the next event is waited for with the hold timer running. */
    val waitingOnHold: Boolean get() = holds && !moved && !multi && !done

    /** When the ring shows, then when it closes. */
    val holdDeadline: Long get() = startMs + if (ringShown) showMs + HOLD_FILL_MS else showMs

    /** The hold timer ran out: the ring shows, or, once shown, closes. True when the hold fires. */
    fun holdTick(ring: HoldRingState): Boolean {
        now = holdDeadline
        if (!ringShown) {
            ringShown = true
            ring.at = start
            return false
        }
        ring.at = null
        done = true
        return true
    }

    /** Takes [event]'s time; true once a second finger has come down in this gesture. */
    fun pairing(
        event: PointerEvent,
        ring: HoldRingState,
    ): Boolean {
        now = event.changes.firstOrNull()?.uptimeMillis ?: now
        if (!multi && event.changes.count { it.pressed } >= 2) {
            multi = true
            ring.at = null
            val (a, b) = pair(event) ?: return true
            pairInside = pairOurs(a, b)
            pairStart = (a + b) / 2f
            spanStart = (b - a).getDistance().coerceAtLeast(1f)
            angleStart = atan2(b.y - a.y, b.x - a.x)
        }
        return multi
    }

    /** Two fingers' pan, zoom and twist so far. True when every finger has lifted. */
    fun trackPair(event: PointerEvent): Boolean {
        val (a, b) = pair(event) ?: return event.changes.none { it.pressed }
        // A pair with a finger in a system strip is Android's: it never drags, pinches or plays.
        if (!pairInside) return false
        pan = (a + b) / 2f - pairStart
        zoom = (b - a).getDistance() / spanStart
        twist = wrap(atan2(b.y - a.y, b.x - a.x) - angleStart)
        pairKind =
            when {
                pairKind == CanvasGesture.PINCH_TWIST -> pairKind
                abs(zoom - 1f) > PINCH_ZOOM || abs(twist) > PINCH_TWIST_RAD -> CanvasGesture.PINCH_TWIST
                pan.getDistance() > slop -> CanvasGesture.TWO_FINGER_DRAG
                else -> pairKind
            }
        return false
    }

    /**
     * A pinch's scale factor and twist in degrees since the last event, once the pair reads as a
     * pinch ([pairKind]); null before, or when nothing changed. A pinch is taken from the event it is
     * recognised on, never from the pair's start, so the canvas does not jump when it is.
     */
    fun pinchStep(): Pair<Float, Float>? {
        val factor = zoom / lastZoom
        val degrees = Math.toDegrees(wrap(twist - lastTwist).toDouble()).toFloat()
        lastZoom = zoom
        lastTwist = twist
        if (pairKind != CanvasGesture.PINCH_TWIST || (factor == 1f && degrees == 0f)) return null
        return factor to degrees
    }

    /** One finger moved to [pos]. True when this move completes the swirl. */
    fun moveTo(
        pos: Offset,
        ring: HoldRingState,
    ): Boolean {
        if (!moved && (pos - start).getDistance() > slop) {
            moved = true
            ring.at = null
        }
        last = pos
        if (!moved || !swirls || done) return false
        val turned = turnBy(pos)
        if (swirlAttempted(turned)) swirling = true
        if (turned < SWIRL_TURN_RAD) return false
        done = true
        return true
    }

    /** What two fingers did, once they lift: a pinch or twist, else a drag past [farPx]. */
    fun pairGesture(farPx: Float): CanvasGesture? =
        when {
            pairKind == CanvasGesture.PINCH_TWIST -> CanvasGesture.PINCH_TWIST
            pan.getDistance() > farPx -> CanvasGesture.TWO_FINGER_DRAG
            else -> null
        }

    /** A one-finger swipe past [farPx] by its main axis; left is next, like turning a page. */
    fun swipe(farPx: Float): Pair<CanvasGesture, Int>? {
        val drag = last - start
        val sideways = abs(drag.x) >= abs(drag.y)
        val step = if (drag.x < 0) 1 else -1
        return when {
            sideways && abs(drag.x) > farPx -> CanvasGesture.SWIPE_SIDEWAYS to step
            sideways -> null
            drag.y < -farPx -> CanvasGesture.SWIPE_UP to 0
            drag.y > farPx -> CanvasGesture.SWIPE_DOWN to 0
            else -> null
        }
    }

    /**
     * The total turn of the finger's heading so far, in radians, after a step to [pos]. Headings are
     * taken over steps of at least [stepPx], so the jitter of a finger at rest never adds up to a circle.
     */
    private fun turnBy(pos: Offset): Float {
        val step = pos - segmentStart
        if (step.getDistance() < stepPx) return abs(turn)
        val h = atan2(step.y, step.x)
        if (!heading.isNaN()) turn += wrap(h - heading)
        heading = h
        segmentStart = pos
        return abs(turn)
    }

    private fun pair(event: PointerEvent): Pair<Offset, Offset>? {
        val down = event.changes.filter { it.pressed }
        return if (down.size < 2) null else down[0].position to down[1].position
    }

    private fun wrap(angle: Float): Float {
        var a = angle
        while (a > PI) a -= (2 * PI).toFloat()
        while (a < -PI) a += (2 * PI).toFloat()
        return a
    }
}

/**
 * The long-press guard: a red ring around the finger that fills over [HOLD_FILL_MS], recording
 * when it closes. The fill is a timer, not decoration, so it runs on the frame clock rather than an
 * animation spec: with the system's animations removed it still fills at the pace of the hold
 * (WCAG 2.3.3 keeps essential motion). Its progress is read only in the draw phase, so the fill
 * redraws without recomposing anything. A dark halo under the red keeps it readable over any
 * visual (non-text contrast).
 */
@Composable
internal fun HoldRing(
    state: HoldRingState,
    modifier: Modifier = Modifier,
) {
    val at = state.at
    val progress = remember { mutableFloatStateOf(0f) }
    LaunchedEffect(at) {
        progress.floatValue = 0f
        if (at == null) return@LaunchedEffect
        val t0 = withFrameMillis { it }
        while (progress.floatValue < 1f) {
            withFrameMillis { now -> progress.floatValue = ((now - t0).toFloat() / HOLD_FILL_MS).coerceAtMost(1f) }
        }
    }
    if (at == null) return
    val red = MaterialTheme.colorScheme.error
    Canvas(modifier.fillMaxSize()) {
        val r = RING_RADIUS_DP.dp.toPx()
        val stroke = RING_STROKE_DP.dp.toPx()
        drawCircle(Color.Black.copy(alpha = RING_HALO_ALPHA), radius = r, center = at, style = Stroke(stroke * 2f))
        drawCircle(red.copy(alpha = RING_TRACK_ALPHA), radius = r, center = at, style = Stroke(stroke))
        drawArc(
            color = red,
            startAngle = -90f,
            sweepAngle = 360f * progress.floatValue,
            useCenter = false,
            topLeft = Offset(at.x - r, at.y - r),
            size = Size(r * 2, r * 2),
            style = Stroke(stroke, cap = StrokeCap.Round),
        )
    }
}

/**
 * Touch play for TalkBack, and for a one-shot gesture given touch play: two points circling the
 * centre for about a second, a stir, then released. Released on cancellation as well, or the
 * touch field would keep its last points down.
 */
@Composable
internal fun rememberTouchPulse(onTouchPoints: (FloatArray, Int) -> Unit): () -> Unit {
    val scope = rememberCoroutineScope()
    val send by rememberUpdatedState(onTouchPoints)
    return remember(scope) {
        {
            scope.launch {
                val xy = FloatArray(4)
                try {
                    for (i in 0 until PULSE_STEPS) {
                        val a = i.toFloat() / PULSE_STEPS * 2f * PI.toFloat()
                        xy[0] = PULSE_RADIUS * cos(a)
                        xy[1] = PULSE_RADIUS * sin(a)
                        xy[2] = -xy[0]
                        xy[3] = -xy[1]
                        send(xy, 2)
                        delay(PULSE_FRAME_MS)
                    }
                } finally {
                    send(xy, 0)
                }
            }
            Unit
        }
    }
}

/**
 * The mapped actions as TalkBack custom actions, one per action (a step gives next and previous),
 * in the gesture map's order. Recording leaves the list while one runs: the red Record icon is
 * its stop.
 */
@Composable
internal fun rememberVisualA11yActions(
    actionFor: (CanvasGesture) -> GestureAction,
    actions: VisualActions,
    recording: Boolean,
): List<CustomAccessibilityAction> {
    val labels =
        mapOf(
            GestureAction.SHOW_MINI_PLAYER to stringResource(R.string.visuals_gesture_show_mini_player),
            GestureAction.QUICK_SETTINGS to stringResource(R.string.visuals_gesture_quick_settings),
            GestureAction.TOGGLE_SHEET to stringResource(R.string.visuals_gesture_toggle_sheet),
            GestureAction.MACROS to stringResource(R.string.visuals_gesture_macros),
            GestureAction.RECORD to stringResource(R.string.visuals_gesture_record),
            GestureAction.RANDOMIZE_ALL to stringResource(R.string.visuals_gesture_randomize),
            GestureAction.TOUCH_PLAY to stringResource(R.string.visuals_gesture_touch_play),
            GestureAction.PLAY_PAUSE to stringResource(R.string.visuals_gesture_play_pause),
        )
    val steps =
        mapOf(
            GestureAction.STEP_STYLE to (stringResource(R.string.a11y_next_style) to stringResource(R.string.a11y_previous_style)),
            GestureAction.STEP_PRESET to
                (stringResource(R.string.visuals_gesture_next_preset) to stringResource(R.string.visuals_gesture_previous_preset)),
            GestureAction.STEP_PLAYLIST to
                (stringResource(R.string.visuals_gesture_next_look) to stringResource(R.string.visuals_gesture_previous_look)),
        )
    // Full screen (the sheet hidden) has its own stage icon, which fades with the chrome, so
    // TalkBack keeps it even when no gesture is mapped to it.
    val mapped = (CanvasGesture.entries.map(actionFor) + GestureAction.TOGGLE_SHEET).distinct()
    val list = mutableListOf<CustomAccessibilityAction>()
    for (a in mapped) {
        if (a == GestureAction.OFF || (a == GestureAction.RECORD && recording)) continue
        val step = steps[a]
        if (step != null) {
            list += CustomAccessibilityAction(step.first) { true.also { actions.run(a, 1) } }
            list += CustomAccessibilityAction(step.second) { true.also { actions.run(a, -1) } }
        } else {
            labels[a]?.let { label -> list += CustomAccessibilityAction(label) { true.also { actions.run(a, 0) } } }
        }
    }
    return list
}

/**
 * Preset step for a gesture: the next or previous preset the user may apply, after the one on
 * stage (matched by style and params), in the Presets page's order. Locked presets are skipped,
 * as the style step skips locked styles, so a swipe never lands on the paywall.
 */
internal fun stepPreset(
    viewModel: PlayerViewModel,
    gate: FeatureGate,
    step: Int,
) {
    val viz = viewModel.vizState.value
    val open = viz.presets.filter { gate.allowsStyle(it.sceneId) }
    if (open.isEmpty()) return
    val here = open.indexOfFirst { it.sceneId == viz.sceneId && it.params == viz.params }
    val next =
        when {
            here >= 0 -> Math.floorMod(here + step, open.size)
            step > 0 -> 0
            else -> open.lastIndex
        }
    viewModel.applyPreset(open[next])
}

/** How far a one-finger swipe, or a two-finger drag, must travel to count. */
private const val SWIPE_DP = 72f

/** The ring's fill; the hold fires when it closes (~0.8 s, ui-flow decisions). */
private const val HOLD_FILL_MS = 800L

/** Two circles; a little short of 4π, since a drawn circle rarely closes exactly. */
private const val SWIRL_TURN_RAD = 3.6f * PI.toFloat()

/** Half a circle: past this the finger is drawing a circle, not swiping. */
private const val SWIRL_ATTEMPT_RAD = PI.toFloat()
private const val SWIRL_STEP_DP = 6f
private const val PINCH_ZOOM = 0.2f
private const val PINCH_TWIST_RAD = 0.5f
private const val RING_RADIUS_DP = 44f
private const val RING_STROKE_DP = 5f
private const val RING_TRACK_ALPHA = 0.3f
private const val RING_HALO_ALPHA = 0.45f
private const val PULSE_STEPS = 36
private const val PULSE_FRAME_MS = 33L
private const val PULSE_RADIUS = 0.35f
