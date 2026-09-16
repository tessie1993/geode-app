package dev.geode.ui.glass

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

private const val SWEEP_START_DEG = 135f
private const val SWEEP_TOTAL_DEG = 270f
private const val DRAG_DEGREES_PER_PX = 0.35f

/** A rotary glass dial with an arc readout; drag vertically or in an arc to turn. */
@Composable
fun GlassKnob(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    enabled: Boolean = true,
    knobSize: Dp = 56.dp,
) {
    val span = valueRange.endInclusive - valueRange.start
    val fraction = if (span > 0f) ((value - valueRange.start) / span).coerceIn(0f, 1f) else 0f
    val currentOnChange = rememberUpdatedState(onValueChange)
    val currentRange = rememberUpdatedState(valueRange)

    Box(
        modifier
            .size(knobSize)
            .glassSurface(shape = GlassShapes.bubble)
            .floatOnWater(strength = 0.4f)
            .then(
                if (enabled) {
                    Modifier.pointerInput(Unit) {
                        var accumulated = fraction
                        detectDragGestures { _, dragAmount ->
                            val delta = -dragAmount.y * DRAG_DEGREES_PER_PX / SWEEP_TOTAL_DEG
                            accumulated = (accumulated + delta).coerceIn(0f, 1f)
                            val r = currentRange.value
                            currentOnChange.value(r.start + accumulated * (r.endInclusive - r.start))
                        }
                    }
                } else {
                    Modifier
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.size(knobSize)) {
            val stroke = 3.dp.toPx()
            val arcSize = Size(size.width - stroke, size.height - stroke)
            val topLeft = Offset(stroke / 2f, stroke / 2f)
            drawArc(
                color = GlassPalette.glassRim.copy(alpha = 0.4f),
                startAngle = SWEEP_START_DEG,
                sweepAngle = SWEEP_TOTAL_DEG,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = Stroke(width = stroke),
            )
            drawArc(
                brush =
                    Brush.sweepGradient(
                        listOf(GlassPalette.mint, GlassPalette.lavender, GlassPalette.peach, GlassPalette.mint),
                    ),
                startAngle = SWEEP_START_DEG,
                sweepAngle = SWEEP_TOTAL_DEG * fraction,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = Stroke(width = stroke),
            )
        }
    }
}
