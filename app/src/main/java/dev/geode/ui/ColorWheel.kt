package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.drag
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.widthIn
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.geode.R
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.sin

private const val HUE_STEP = 15f
private const val SV_STEP = 0.05f

/** Where the ring and the square sit in a wheel of the given size; shared by drawing and touch. */
private class WheelGeometry(
    width: Float,
    height: Float,
) {
    val cx = width / 2f
    val cy = height / 2f
    val outer = min(width, height) / 2f
    val ringWidth = outer * 0.18f
    val ringMid = outer - ringWidth / 2f

    /** Inside this the touch belongs to the saturation and brightness square. */
    val inner = outer - ringWidth - outer * 0.05f

    /** The square's half side: it fits inside the ring's inner circle. */
    val half = inner * 0.7071f

    fun onRing(p: Offset) = hypot(p.x - cx, p.y - cy) > inner

    fun hueAt(p: Offset): Float = ((atan2(p.y - cy, p.x - cx) * 180f / PI.toFloat()) + 360f) % 360f

    fun saturationAt(p: Offset): Float = ((p.x - (cx - half)) / (2f * half)).coerceIn(0f, 1f)

    fun valueAt(p: Offset): Float = 1f - ((p.y - (cy - half)) / (2f * half)).coerceIn(0f, 1f)
}

/**
 * The colour wheel: a hue ring with a draggable handle, and inside it the saturation and brightness square
 * with a draggable picker circle. [hue] is degrees 0..360 (0 at the right, clockwise); [saturation] and
 * [value] are 0..1. TalkBack gets the same six moves as custom actions, since a drag has no spoken form.
 */
@Composable
internal fun ColorWheel(
    hue: Float,
    saturation: Float,
    value: Float,
    onChange: (hue: Float, saturation: Float, value: Float) -> Unit,
    modifier: Modifier = Modifier,
) {
    val current by rememberUpdatedState(Triple(hue, saturation, value))
    val change by rememberUpdatedState(onChange)
    val ringColors = remember { List(13) { Color.hsv((it * 30f) % 360f, 1f, 1f) } }
    val picked = Color.hsv(hue.coerceIn(0f, 360f), saturation.coerceIn(0f, 1f), value.coerceIn(0f, 1f))
    val description = stringResource(R.string.palette_wheel_description)
    val labels =
        listOf(
            stringResource(R.string.palette_wheel_hue_forward),
            stringResource(R.string.palette_wheel_hue_back),
            stringResource(R.string.palette_wheel_more_saturated),
            stringResource(R.string.palette_wheel_less_saturated),
            stringResource(R.string.palette_wheel_brighter),
            stringResource(R.string.palette_wheel_darker),
        )
    val state = stringResource(R.string.palette_wheel_state, hue.toInt(), (saturation * 100).toInt(), (value * 100).toInt())
    Canvas(
        modifier =
            modifier
                .widthIn(max = 280.dp)
                .aspectRatio(1f)
                .pointerInput(Unit) {
                    awaitEachGesture {
                        val geometry = WheelGeometry(size.width.toFloat(), size.height.toFloat())
                        val down = awaitFirstDown()
                        val onRing = geometry.onRing(down.position)

                        fun apply(at: Offset) {
                            val (h, s, v) = current
                            if (onRing) {
                                change(geometry.hueAt(at), s, v)
                            } else {
                                change(h, geometry.saturationAt(at), geometry.valueAt(at))
                            }
                        }
                        apply(down.position)
                        down.consume()
                        drag(down.id) { move ->
                            apply(move.position)
                            move.consume()
                        }
                    }
                }.semantics {
                    contentDescription = description
                    stateDescription = state
                    customActions =
                        listOf(
                            CustomAccessibilityAction(labels[0]) {
                                change((current.first + HUE_STEP) % 360f, current.second, current.third)
                                true
                            },
                            CustomAccessibilityAction(labels[1]) {
                                change((current.first - HUE_STEP + 360f) % 360f, current.second, current.third)
                                true
                            },
                            CustomAccessibilityAction(labels[2]) {
                                change(current.first, (current.second + SV_STEP).coerceAtMost(1f), current.third)
                                true
                            },
                            CustomAccessibilityAction(labels[3]) {
                                change(current.first, (current.second - SV_STEP).coerceAtLeast(0f), current.third)
                                true
                            },
                            CustomAccessibilityAction(labels[4]) {
                                change(current.first, current.second, (current.third + SV_STEP).coerceAtMost(1f))
                                true
                            },
                            CustomAccessibilityAction(labels[5]) {
                                change(current.first, current.second, (current.third - SV_STEP).coerceAtLeast(0f))
                                true
                            },
                        )
                },
    ) {
        val g = WheelGeometry(size.width, size.height)
        val center = Offset(g.cx, g.cy)
        val pureHue = Color.hsv(hue.coerceIn(0f, 360f), 1f, 1f)
        val ring = Brush.sweepGradient(ringColors, center)
        drawCircle(brush = ring, radius = g.ringMid, center = center, style = Stroke(g.ringWidth))
        val topLeft = Offset(g.cx - g.half, g.cy - g.half)
        val square = Size(2f * g.half, 2f * g.half)
        val across = Brush.horizontalGradient(listOf(Color.White, pureHue), startX = topLeft.x, endX = topLeft.x + square.width)
        val shade = Brush.verticalGradient(listOf(Color.Transparent, Color.Black), startY = topLeft.y, endY = topLeft.y + square.height)
        drawRect(brush = across, topLeft = topLeft, size = square)
        drawRect(brush = shade, topLeft = topLeft, size = square)
        val handleRadius = g.ringWidth * 0.6f
        val angle = hue * PI.toFloat() / 180f
        val handle = Offset(g.cx + g.ringMid * cos(angle), g.cy + g.ringMid * sin(angle))
        drawCircle(Color.Black, handleRadius + 1.5.dp.toPx(), handle)
        drawCircle(Color.White, handleRadius, handle)
        drawCircle(pureHue, handleRadius - 2.5.dp.toPx(), handle)
        val picker =
            Offset(
                topLeft.x + saturation.coerceIn(0f, 1f) * square.width,
                topLeft.y + (1f - value.coerceIn(0f, 1f)) * square.height,
            )
        drawCircle(Color.Black, 10.5.dp.toPx(), picker)
        drawCircle(Color.White, 9.dp.toPx(), picker)
        drawCircle(picked, 6.5.dp.toPx(), picker)
    }
}
