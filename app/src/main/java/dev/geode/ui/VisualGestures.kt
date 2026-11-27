package dev.geode.ui

import android.os.SystemClock
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
import dev.geode.R
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import dev.geode.data.Preset
import dev.geode.data.VizUiState
import dev.geode.engine.bridge.CatalogueNative
import dev.geode.engine.bridge.SoundNative
import dev.geode.engine.bridge.TouchNative
import dev.geode.pro.FeatureGate
import dev.geode.ui.theme.StoneHaptics
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
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
 * What a gesture is (a tap top or bottom of [tapSplitPx], a double tap, a swipe, a swirl, the hold,
 * a two-finger drag or pinch / twist) is native (`core/viz/GestureRead`, one reader per stage): this
 * loop feeds it each down, event and timeout, with the platform's slop and timeouts, and does what
 * it answers. Whichever pair gesture is given [GestureAction.TOUCH_PLAY] hands the fingers to the
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
 * distances from the window's edges, so the reader is told the window (`LocalWindowInfo.containerSize`
 * from (0, 0)) and the stage's place in it ([WindowPlacement]): in the expanded shell the stage sits
 * beside the rail inside `safeDrawingPadding()`, and its own (0, 0) is not the window's.
 *
 * The ring shows at `ViewConfiguration.longPressTimeoutMillis`, which on Android is the platform's
 * long-press timeout, so it follows Accessibility › Touch & hold delay. The double tap's second down
 * is held to `doubleTapMinTimeMillis` and `doubleTapTimeoutMillis` (the window Compose's own
 * `detectTapGestures` uses) and to the platform's double-tap slop
 * (`android.view.ViewConfiguration.getScaledDoubleTapSlop`; Compose's `ViewConfiguration` has no
 * such slop). A touch that turns out not to be a second tap is read again from its own down as a
 * new gesture, so a quick tap then a swipe keeps the swipe.
 *
 * TalkBack users get each mapped action as a custom action ([a11yActions]): with TalkBack on, a
 * two-finger drag is its scroll, so the gestures themselves are out of reach.
 *
 * Callbacks go through [rememberUpdatedState] because the pointer loop is keyed only on the
 * insets, so a remapped gesture or a new lambda never restarts a gesture in flight; the map is
 * handed to the reader at each down.
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
            val points = FloatArray(CatalogueNative.catalogueValue("touchPoints").toInt() * 2)
            val pinchStep = FloatArray(2)
            val actions = IntArray(CanvasGesture.entries.size)
            val decided = IntArray(3)
            val reader = TouchNative.gestureCreate()
            try {
                awaitEachGesture {
                    var next: PointerInputChange? = awaitFirstDown(requireUnconsumed = false)
                    // A touch the double-tap wait saw but could not use comes back round as `next`.
                    while (next != null) {
                        val down: PointerInputChange = next
                        next = null
                        val start = down.position
                        // Read per down, not once: the strips change with rotation and the navigation mode.
                        val window = windowInfo.containerSize
                        TouchNative.gestureArea(
                            reader,
                            window.width.toFloat(),
                            window.height.toFloat(),
                            edges.getLeft(this, direction).toFloat(),
                            edges.getTop(this).toFloat(),
                            edges.getRight(this, direction).toFloat(),
                            edges.getBottom(this).toFloat(),
                            placement.origin.x,
                            placement.origin.y,
                            placement.placed,
                        )
                        TouchNative.gestureConfigure(
                            reader,
                            viewConfiguration.touchSlop,
                            doubleSlop,
                            density,
                            viewConfiguration.longPressTimeoutMillis,
                            viewConfiguration.doubleTapMinTimeMillis,
                            viewConfiguration.doubleTapTimeoutMillis,
                        )
                        for (gesture in CanvasGesture.entries) actions[gesture.ordinal] = action(gesture).ordinal
                        TouchNative.gestureDown(reader, start.x, start.y, down.uptimeMillis, actions, armed)
                        try {
                            follow(
                                reader = reader,
                                downId = down.id,
                                start = start,
                                ring = ring,
                                points = points,
                                pinchStep = pinchStep,
                                onHold = {
                                    if (StoneHaptics.enabled) haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                    fire(CanvasGesture.LONG_PRESS, 0)
                                },
                                onSwirl = {
                                    if (StoneHaptics.enabled) haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                    fire(CanvasGesture.SWIRL, 0)
                                },
                                onPinch = { factor, degrees -> pinch?.invoke(factor, degrees) },
                                onTouch = { live -> touch(points, live) },
                            )
                        } finally {
                            // Also on cancellation (the visual leaves mid-gesture): a ring or a published
                            // finger left behind would stay on screen, or keep steering, forever.
                            ring.at = null
                            if (TouchNative.gestureRelease(reader)) touch(points, 0)
                        }
                        val gesture = CanvasGesture.entries.getOrNull(TouchNative.gestureEnd(reader, split(), decided))
                        if (gesture != null && decided[1] == 0) {
                            fire(gesture, decided[0])
                        } else if (gesture != null) {
                            tapDown(down.uptimeMillis)
                            val second = if (decided[2] > 0) awaitSecondTap(reader, decided[2].toLong()) else SecondTap.NONE
                            if (second.double) tapDown(second.downMs)
                            fire(if (second.double) CanvasGesture.DOUBLE_TAP else gesture, 0)
                            next = second.next
                        }
                    }
                }
            } finally {
                TouchNative.gestureDestroy(reader)
            }
        }.semantics {
            // Named, so TalkBack can land on the visual at all and offer its actions there.
            contentDescription = surface
            customActions = a11yActions
        }
}

/**
 * Follows one gesture's pointers until the reader says its fingers are gone, waiting on the hold
 * timer while the reader runs one. Fires the hold and the swirl as they happen, hands a pinch step
 * to [onPinch] and touch play's fingers (y-up NDC, in [points]) to [onTouch].
 */
private suspend fun AwaitPointerEventScope.follow(
    reader: Long,
    downId: PointerId,
    start: Offset,
    ring: HoldRingState,
    points: FloatArray,
    pinchStep: FloatArray,
    onHold: () -> Unit,
    onSwirl: () -> Unit,
    onPinch: (Float, Float) -> Unit,
    onTouch: (Int) -> Unit,
) {
    while (true) {
        val wait = TouchNative.gestureWaitMs(reader, SystemClock.uptimeMillis())
        val event = if (wait > 0) withTimeoutOrNull(wait) { awaitPointerEvent() } else awaitPointerEvent()
        var live = 0
        val answer =
            if (event == null) {
                TouchNative.gestureTick(reader)
            } else {
                live = pressedInto(event, points)
                val primaryDown = event.changes.firstOrNull { it.id == downId }?.pressed == true
                val at = event.changes.firstOrNull()?.uptimeMillis ?: -1L
                TouchNative.gestureEvent(reader, at, points, live, primaryDown, pinchStep)
            }
        ring.at = if (answer and TouchNative.GESTURE_RING != 0) start else null
        if (answer and TouchNative.GESTURE_HOLD != 0) onHold()
        if (answer and TouchNative.GESTURE_SWIRL != 0) onSwirl()
        if (answer and TouchNative.GESTURE_OVER != 0) return
        if (answer and TouchNative.GESTURE_PINCH != 0) onPinch(pinchStep[0], pinchStep[1])
        if (answer and TouchNative.GESTURE_PUBLISH != 0) {
            TouchNative.touchToNdc(points, live, size.width.toFloat(), size.height.toFloat())
            onTouch(live)
        } else if (answer and TouchNative.GESTURE_RELEASE != 0) {
            onTouch(0)
        }
    }
}

/** The fingers down in [event] as (x, y) px pairs into [points], in the event's order, as many as fit; how many. */
private fun pressedInto(
    event: PointerEvent,
    points: FloatArray,
): Int {
    var live = 0
    for (change in event.changes) {
        if (!change.pressed || live * 2 >= points.size) continue
        points[live * 2] = change.position.x
        points[live * 2 + 1] = change.position.y
        live++
    }
    return live
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
 * Waits up to [waitMs] after the first lift for a second finger, then for it to lift; the reader
 * says which downs are a bounce, which is the second tap and when it stopped being one. A touch that
 * is not a second tap is handed back as [SecondTap.next] the moment that is known, so the gesture
 * it starts (a swipe after a quick tap, say) is read whole instead of swallowed here. Handing it
 * back rather than returning matters: `awaitEachGesture` waits for every finger to lift before its
 * next round, so a finger still down when this returns would never be read.
 */
private suspend fun AwaitPointerEventScope.awaitSecondTap(
    reader: Long,
    waitMs: Long,
): SecondTap {
    var answer = TouchNative.GESTURE_SECOND_BOUNCE
    val second =
        withTimeoutOrNull(waitMs) {
            var down: PointerInputChange
            do {
                down = awaitFirstDown(requireUnconsumed = false)
                answer = TouchNative.gestureSecondDown(reader, down.position.x, down.position.y, down.uptimeMillis)
            } while (answer == TouchNative.GESTURE_SECOND_BOUNCE)
            down
        } ?: return SecondTap.NONE
    if (answer != TouchNative.GESTURE_SECOND_FOLLOW) return SecondTap(double = false, next = second)
    while (true) {
        val change = awaitPointerEvent().changes.firstOrNull { it.id == second.id }
        val at = change?.position ?: Offset.Zero
        when (TouchNative.gestureSecondMove(reader, change != null, at.x, at.y, change?.pressed == true)) {
            TouchNative.GESTURE_SECOND_DOUBLE -> return SecondTap(double = true, next = null, downMs = second.uptimeMillis)
            TouchNative.GESTURE_SECOND_NEW_TOUCH -> return SecondTap(double = false, next = second)
        }
    }
}

/**
 * Where a layout sits in the window, kept from its last reported placement so the gesture reader,
 * which gets local positions, can compare a down with the window insets (measured from the
 * window's edges). A plain object, not state: nothing draws from it, so a move must not recompose.
 * [placed] stays false until the first report; until then the reader gives every down to the app
 * rather than guess, and the strips are back from the first report on, which `onLayoutRectChanged`
 * sends one debounce after the first layout.
 */
internal class WindowPlacement {
    var origin = Offset.Zero
    var placed = false
}

/**
 * Keeps [placement] current through `Modifier.onLayoutRectChanged`, not `onGloballyPositioned`: the
 * jetpack-compose-performance skill (references/layouts.md, "Throttled Bounds Tracking") names it
 * for window-relative bounds, since it is throttled and debounced where `onGloballyPositioned` runs
 * after every layout pass that may move the node. The stage only moves when the shell relayouts
 * (rotation, the rail, a resized window), so the default 64 ms debounce costs nothing: the report
 * lands once the move settles. `RelativeLayoutBounds.positionInWindow` is the node's top-left in
 * window coordinates, the frame the insets are in.
 */
internal fun Modifier.windowPlacement(placement: WindowPlacement): Modifier =
    onLayoutRectChanged { bounds ->
        placement.origin = bounds.positionInWindow.toOffset()
        placement.placed = true
    }

/**
 * The long-press guard: a red ring around the finger that fills over
 * [TouchNative.GESTURE_HOLD_FILL_MS], recording when it closes. The fill is a timer, not decoration, so it runs on the frame clock rather than an
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
        val fillMs = TouchNative.GESTURE_HOLD_FILL_MS.toFloat()
        while (progress.floatValue < 1f) {
            withFrameMillis { now -> progress.floatValue = ((now - t0).toFloat() / fillMs).coerceAtMost(1f) }
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
                    // The stir's shape and pace are native (`core/viz/TouchGesture`); this only sends it.
                    var step = 0
                    while (true) {
                        val holdMs = TouchNative.touchPulse(step++, xy)
                        if (holdMs <= 0) break
                        send(xy, 2)
                        delay(holdMs.toLong())
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
    styleStepLabels: Pair<String, String>,
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
            GestureAction.STEP_STYLE to styleStepLabels,
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
 * Preset step for a gesture (and the style step on the Presets tab, [stepStage]): the next or
 * previous preset the user may apply, after the one on stage, in the Presets page's order. Locked
 * presets are left out, as the style step skips locked styles, so a swipe never lands on the
 * paywall. The walk is native (`geode_stage_step_index`).
 */
internal fun stepPreset(
    viewModel: PlayerViewModel,
    gate: FeatureGate,
    step: Int,
) {
    val viz = viewModel.vizState.value
    val open = openLooks(viz, gate)
    val next = SoundNative.stageStepIndex(BooleanArray(open.size) { true }, lookOnStage(viz, open), step)
    if (next >= 0) viewModel.applyPreset(open[next])
}

/** The saved looks the user may apply, in the Presets page's order. */
internal fun openLooks(
    viz: VizUiState,
    gate: FeatureGate,
): List<Preset> = viz.presets.filter { gate.allowsStyle(it.sceneId) }

/** Which of [looks] the stage shows (its style and params as saved), or -1. */
internal fun lookOnStage(
    viz: VizUiState,
    looks: List<Preset>,
): Int = looks.indexOfFirst { it.sceneId == viz.sceneId && it.params == viz.params }

private const val RING_RADIUS_DP = 44f
private const val RING_STROKE_DP = 5f
private const val RING_TRACK_ALPHA = 0.3f
private const val RING_HALO_ALPHA = 0.45f
