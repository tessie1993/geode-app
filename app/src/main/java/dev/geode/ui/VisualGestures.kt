package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.systemGestures
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
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerEvent
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
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

/**
 * Where the long-press guard ring is drawn, if anywhere. The detector sets it once the finger has
 * held still for [HOLD_SHOW_MS] and clears it when the finger moves, lifts or the hold fires; the
 * ring ([HoldRing]) fills itself from that moment, so neither side recomposes per frame.
 */
@Stable
internal class HoldRingState {
    var at by mutableStateOf<Offset?>(null)
}

/**
 * The things a gesture on the visual can do, one lambda per [GestureAction]. The detector only
 * says which [CanvasGesture] happened; [run] looks the action up in Settings › Behaviour's gesture
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
 * reads them first: two detectors would each see half a gesture (the same reason
 * `VisualizerScreen.visualizerTouch` is one).
 *
 * One finger: a tap (top or bottom of the visible visual, split at [tapSplitPx]), a double tap, a
 * swipe sideways, up or down past [SWIPE_DP], a swirl (two circles drawn with the finger, counted as
 * the heading's total turn) and a long press held through the red ring. Two fingers: a drag or a
 * pinch / twist; while either is given [GestureAction.TOUCH_PLAY] the fingers are handed to the
 * visualizer's touch field ([onTouchPoints], in y-up NDC) as they move, which is what touch play is.
 * One finger never touches the visual (ui-flow decisions).
 *
 * A swipe or a swirl only counts when it starts outside `WindowInsets.systemGestures`: those strips
 * stay Android's (back at the sides, home at the bottom, the shade at the top). A tap there is still
 * ours, since Android does nothing with a tap.
 *
 * The double tap waits out the double-tap timeout before a single tap fires, so it is only waited
 * for while the map gives the double tap an action. Lifting once the ring shows does nothing, as
 * decided: the ring is the guard, and a press that long was not a tap.
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
    a11yActions: List<CustomAccessibilityAction>,
): Modifier {
    val action by rememberUpdatedState(actionFor)
    val split by rememberUpdatedState(tapSplitPx)
    val armed by rememberUpdatedState(holdArmed)
    val fire by rememberUpdatedState(onGesture)
    val touch by rememberUpdatedState(onTouchPoints)
    val haptics = LocalHapticFeedback.current
    val edges = WindowInsets.systemGestures
    val direction = LocalLayoutDirection.current
    val surface = stringResource(R.string.visuals_gesture_surface)
    return this
        .pointerInput(edges, direction) {
            val points = FloatArray(TouchField.MAX_POINTS * 2)
            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false)
                val start = down.position
                val ours =
                    start.x > edges.getLeft(this, direction) &&
                        start.x < size.width - edges.getRight(this, direction) &&
                        start.y > edges.getTop(this) &&
                        start.y < size.height - edges.getBottom(this)
                val read =
                    GestureRead(
                        start = start,
                        startMs = down.uptimeMillis,
                        holds = armed && action(CanvasGesture.LONG_PRESS) != GestureAction.OFF,
                        swirls = ours && action(CanvasGesture.SWIRL) != GestureAction.OFF,
                        slop = viewConfiguration.touchSlop,
                        stepPx = SWIRL_STEP_DP.dp.toPx(),
                    )
                val touchPlay =
                    action(CanvasGesture.TWO_FINGER_DRAG) == GestureAction.TOUCH_PLAY ||
                        action(CanvasGesture.PINCH_TWIST) == GestureAction.TOUCH_PLAY
                val publishTo: ((PointerEvent) -> Unit)? =
                    if (touchPlay) {
                        { event ->
                            publish(event, points, size.width.toFloat(), size.height.toFloat(), touch)
                            read.published = true
                        }
                    } else {
                        null
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
                    read.moved && ours -> read.swipe(far)?.let { (g, step) -> fire(g, step) }
                    read.moved -> Unit
                    read.ringShown || read.now - read.startMs > viewConfiguration.longPressTimeoutMillis -> Unit
                    else -> {
                        val tap = if (start.y < split()) CanvasGesture.TAP_TOP else CanvasGesture.TAP_BOTTOM
                        val double = action(CanvasGesture.DOUBLE_TAP) != GestureAction.OFF && awaitSecondTap(read.slop)
                        fire(if (double) CanvasGesture.DOUBLE_TAP else tap, 0)
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
    publishTo: ((PointerEvent) -> Unit)?,
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
                publishTo?.invoke(event)
                if (read.trackPair(event)) return
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
 * Waits up to the double-tap timeout for a second finger, then for it to lift. True when it came
 * and stayed put; a second touch that wanders off is the start of something else, not a double tap.
 */
private suspend fun AwaitPointerEventScope.awaitSecondTap(slop: Float): Boolean {
    val second = withTimeoutOrNull(viewConfiguration.doubleTapTimeoutMillis) { awaitFirstDown(requireUnconsumed = false) } ?: return false
    var wandered = false
    do {
        val change = awaitPointerEvent().changes.firstOrNull { it.id == second.id }
        if (change != null && (change.position - second.position).getDistance() > slop) wandered = true
    } while (change != null && change.pressed)
    return !wandered
}

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
) {
    var moved = false
    var multi = false

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

    /** Whether the next event is waited for with the hold timer running. */
    val waitingOnHold: Boolean get() = holds && !moved && !multi && !done

    /** When the ring shows, then when it closes. */
    val holdDeadline: Long get() = startMs + if (ringShown) HOLD_SHOW_MS + HOLD_FILL_MS else HOLD_SHOW_MS

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
            pairStart = (a + b) / 2f
            spanStart = (b - a).getDistance().coerceAtLeast(1f)
            angleStart = atan2(b.y - a.y, b.x - a.x)
        }
        return multi
    }

    /** Two fingers' pan, zoom and twist so far. True when every finger has lifted. */
    fun trackPair(event: PointerEvent): Boolean {
        val (a, b) = pair(event) ?: return event.changes.none { it.pressed }
        pan = (a + b) / 2f - pairStart
        zoom = (b - a).getDistance() / spanStart
        twist = wrap(atan2(b.y - a.y, b.x - a.x) - angleStart)
        return false
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
        if (turnBy(pos) < SWIRL_TURN_RAD) return false
        done = true
        return true
    }

    /** What two fingers did, once they lift: a pinch or twist past its threshold, else a drag past [farPx]. */
    fun pairGesture(farPx: Float): CanvasGesture? =
        when {
            abs(zoom - 1f) > PINCH_ZOOM || abs(twist) > PINCH_TWIST_RAD -> CanvasGesture.PINCH_TWIST
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
 * in the gesture map's order. Recording leaves the list while a take runs: the red Record icon is
 * its stop.
 */
@Composable
internal fun rememberVisualA11yActions(
    actionFor: (CanvasGesture) -> GestureAction,
    actions: VisualActions,
    recording: Boolean,
    onOpenLive: () -> Unit,
): List<CustomAccessibilityAction> {
    val labels =
        mapOf(
            GestureAction.SHOW_MINI_PLAYER to stringResource(R.string.visuals_gesture_show_mini_player),
            GestureAction.QUICK_SETTINGS to stringResource(R.string.visuals_gesture_quick_settings),
            GestureAction.TOGGLE_SHEET to stringResource(R.string.visuals_gesture_toggle_sheet),
            // The playlist only steps forward (AutoVisualsController.stepVizPlaylist), so it has one action.
            GestureAction.STEP_PLAYLIST to stringResource(R.string.visuals_gesture_next_look),
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
        )
    val live = stringResource(R.string.visuals_view_live)
    val mapped = CanvasGesture.entries.map(actionFor).distinct()
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
    // The live view has its own stage icon, which fades with the chrome; TalkBack keeps it here too.
    list += CustomAccessibilityAction(live) { true.also { onOpenLive() } }
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

/** A still finger shows the ring after this, so a tap never flashes it. */
private const val HOLD_SHOW_MS = 250L

/** The ring's fill; the hold fires when it closes (~0.8 s, ui-flow decisions). */
private const val HOLD_FILL_MS = 800L

/** Two circles; a little short of 4π, since a drawn circle rarely closes exactly. */
private const val SWIRL_TURN_RAD = 3.6f * PI.toFloat()
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
