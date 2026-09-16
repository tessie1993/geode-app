package dev.geode.ui.glass

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/** A glass tube filled with a pastel gradient, e.g. playback / export progress. */
@Composable
fun GlassLinearProgress(
    progress: Float,
    modifier: Modifier = Modifier,
) {
    val fraction = progress.coerceIn(0f, 1f)
    Box(modifier.fillMaxWidth().height(10.dp)) {
        Box(Modifier.matchParentSize().glassSurface(shape = GlassShapes.pill))
        if (fraction > 0f) {
            Canvas(Modifier.matchParentSize()) {
                val y = size.height / 2f
                val endX = size.width * fraction
                drawLine(
                    brush =
                        Brush.horizontalGradient(
                            listOf(GlassPalette.mint, GlassPalette.lavender, GlassPalette.peach),
                            endX = endX.coerceAtLeast(1f),
                        ),
                    start = Offset(0f, y),
                    end = Offset(endX, y),
                    strokeWidth = size.height,
                    cap = StrokeCap.Round,
                )
            }
        }
    }
}

/** A glass ring filled clockwise from 12 o'clock, e.g. a countdown or a dial readout. */
@Composable
fun GlassCircularProgress(
    progress: Float,
    modifier: Modifier = Modifier,
    diameter: Dp = 40.dp,
) {
    val fraction = progress.coerceIn(0f, 1f)
    Box(modifier.size(diameter).glassSurface(shape = GlassShapes.bubble)) {
        Canvas(Modifier.matchParentSize()) {
            val stroke = 3.dp.toPx()
            val inset = stroke / 2f
            drawArc(
                brush =
                    Brush.sweepGradient(
                        listOf(GlassPalette.mint, GlassPalette.lavender, GlassPalette.peach, GlassPalette.mint),
                    ),
                startAngle = -90f,
                sweepAngle = 360f * fraction,
                useCenter = false,
                topLeft = Offset(inset, inset),
                size = Size(size.width - stroke, size.height - stroke),
                style = Stroke(width = stroke),
            )
        }
    }
}
