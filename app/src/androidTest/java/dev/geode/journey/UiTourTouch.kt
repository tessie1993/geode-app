package dev.geode.journey

import android.graphics.PointF
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/**
 * Real touches, injected as the touchscreen would send them, in screen pixels.
 *
 * Compose's `performTouchInput` would be simpler, but it delivers events straight into one
 * composable, past the window, past the system's gesture handling and past the GL view's own
 * listeners, so it cannot tell whether a swipe that starts in a system gesture inset reaches the
 * app at all, which is half of what the gesture part of the tour is for. These go through the
 * input dispatcher like a finger: gesture navigation's input monitor sees them first and can take
 * an edge swipe for back or home, and the shade can take one from the top.
 *
 * Timing is real time. The visual's detector (`ui/VisualGestures.kt`) decides a hold on a
 * coroutine timer and a double tap on the double-tap timeout, so the waits between events are
 * actual sleeps, and every event carries the uptime at which it was sent.
 */
object UiTourTouch {
    /** One tap: down and up [TAP_MS] apart, short of anything that reads as a hold. */
    fun tap(at: PointF) = path(listOf(at), holdMs = TAP_MS)

    /** Two taps well inside the double-tap timeout (300 ms on stock Android). */
    fun doubleTap(at: PointF) {
        tap(at)
        SystemClock.sleep(DOUBLE_TAP_GAP_MS)
        tap(at)
    }

    /** A finger held still at [at] for [ms], then lifted. */
    fun hold(
        at: PointF,
        ms: Long,
    ) = path(listOf(at), holdMs = ms)

    /** A straight swipe in [steps] moves of [stepMs] each: slow enough not to fling a list. */
    fun swipe(
        from: PointF,
        to: PointF,
        steps: Int = SWIPE_STEPS,
        stepMs: Long = STEP_MS,
    ) = path(line(from, to, steps), stepMs = stepMs)

    /**
     * One finger drawn round [centre] [turns] times. The detector counts a swirl from the
     * heading's total turn, so the circle needs no particular size, only enough steps per turn
     * that each step's heading is clean.
     */
    fun circles(
        centre: PointF,
        radius: Float,
        turns: Int,
    ) {
        val steps = turns * CIRCLE_STEPS
        val points =
            (0..steps).map { i ->
                val a = i.toFloat() / CIRCLE_STEPS * 2f * PI.toFloat()
                PointF(centre.x + radius * cos(a), centre.y + radius * sin(a))
            }
        path(points, stepMs = STEP_MS)
    }

    /**
     * Two fingers, each along its own path, moving together step by step. Down is the first
     * finger then the second (ACTION_POINTER_DOWN), up the reverse, which is the order a hand
     * produces and the order the detector's `pairing` expects.
     */
    fun twoFingers(
        a: List<PointF>,
        b: List<PointF>,
        stepMs: Long = STEP_MS,
    ) {
        require(a.size == b.size && a.isNotEmpty()) { "both fingers need the same number of points" }
        val down = SystemClock.uptimeMillis()
        send(down, MotionEvent.ACTION_DOWN, listOf(a.first()))
        send(down, MotionEvent.ACTION_POINTER_DOWN or POINTER_1, listOf(a.first(), b.first()))
        for (i in 1 until a.size) {
            SystemClock.sleep(stepMs)
            send(down, MotionEvent.ACTION_MOVE, listOf(a[i], b[i]))
        }
        send(down, MotionEvent.ACTION_POINTER_UP or POINTER_1, listOf(a.last(), b.last()))
        send(down, MotionEvent.ACTION_UP, listOf(a.last()))
    }

    /** The points of a straight line, both ends included. */
    fun line(
        from: PointF,
        to: PointF,
        steps: Int,
    ): List<PointF> = (0..steps).map { i -> lerp(from, to, i.toFloat() / steps) }

    /** System back, as the navigation bar's button sends it. */
    fun back() = key(KeyEvent.KEYCODE_BACK)

    fun key(code: Int) {
        val down = SystemClock.uptimeMillis()
        listOf(KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP).forEach { action ->
            val event =
                KeyEvent(
                    down,
                    SystemClock.uptimeMillis(),
                    action,
                    code,
                    0,
                    0,
                    KeyCharacterMap.VIRTUAL_KEYBOARD,
                    0,
                    0,
                    InputDevice.SOURCE_KEYBOARD,
                )
            TourDevice.automation.injectInputEvent(event, true)
        }
    }

    /**
     * One finger along [points]: down at the first, held [holdMs] there, then a move to each
     * next point [stepMs] apart, and up at the last.
     */
    private fun path(
        points: List<PointF>,
        holdMs: Long = 0L,
        stepMs: Long = STEP_MS,
    ) {
        val down = SystemClock.uptimeMillis()
        send(down, MotionEvent.ACTION_DOWN, listOf(points.first()))
        if (holdMs > 0) SystemClock.sleep(holdMs)
        for (i in 1 until points.size) {
            SystemClock.sleep(stepMs)
            send(down, MotionEvent.ACTION_MOVE, listOf(points[i]))
        }
        send(down, MotionEvent.ACTION_UP, listOf(points.last()))
    }

    private fun send(
        downTime: Long,
        action: Int,
        points: List<PointF>,
    ): Boolean {
        val properties =
            Array(points.size) { i ->
                MotionEvent.PointerProperties().apply {
                    id = i
                    toolType = MotionEvent.TOOL_TYPE_FINGER
                }
            }
        val coords =
            Array(points.size) { i ->
                MotionEvent.PointerCoords().apply {
                    x = points[i].x
                    y = points[i].y
                    pressure = 1f
                    size = 1f
                }
            }
        val event =
            MotionEvent.obtain(
                downTime,
                SystemClock.uptimeMillis(),
                action,
                points.size,
                properties,
                coords,
                0,
                0,
                1f,
                1f,
                0,
                0,
                InputDevice.SOURCE_TOUCHSCREEN,
                0,
            )
        return try {
            TourDevice.automation.injectInputEvent(event, true)
        } finally {
            event.recycle()
        }
    }

    private fun lerp(
        a: PointF,
        b: PointF,
        t: Float,
    ) = PointF(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t)

    /** The second pointer's index, shifted into a POINTER_DOWN / POINTER_UP action. */
    private const val POINTER_1 = 1 shl MotionEvent.ACTION_POINTER_INDEX_SHIFT
    private const val TAP_MS = 60L
    private const val DOUBLE_TAP_GAP_MS = 90L
    private const val STEP_MS = 12L
    private const val SWIPE_STEPS = 24
    private const val CIRCLE_STEPS = 48
}
