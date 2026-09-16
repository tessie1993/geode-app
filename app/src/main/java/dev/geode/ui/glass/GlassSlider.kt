package dev.geode.ui.glass

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Slider
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.unit.dp

/**
 * A glass tube track with an iridescent fill on the filled side and a pearl sphere thumb.
 * Same signature shape as `CrystalSlider` (`ui/CrystalControls.kt`) so a screen unit can swap the
 * call 1:1.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GlassSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    steps: Int = 0,
    enabled: Boolean = true,
) {
    Slider(
        value = value,
        onValueChange = onValueChange,
        modifier = modifier.floatOnWater(strength = 0.4f),
        valueRange = valueRange,
        steps = steps,
        enabled = enabled,
        thumb = {
            Box(
                Modifier
                    .size(24.dp)
                    .glassSurface(shape = GlassShapes.bubble, tint = GlassPalette.mint, glow = 0.5f),
            )
        },
        track = {
            val span = valueRange.endInclusive - valueRange.start
            val fraction = if (span > 0f) ((value - valueRange.start) / span).coerceIn(0f, 1f) else 0f
            val dim = if (enabled) 1f else 0.4f
            Box(Modifier.fillMaxWidth().height(14.dp)) {
                Box(Modifier.matchParentSize().glassSurface(shape = GlassShapes.pill))
                Canvas(Modifier.matchParentSize()) {
                    val y = size.height / 2f
                    if (steps > 0) {
                        for (i in 1..steps) {
                            val x = size.width * i / (steps + 1)
                            drawCircle(
                                GlassPalette.textPrimary.copy(alpha = 0.25f * dim),
                                radius = 1.5.dp.toPx(),
                                center = Offset(x, y),
                            )
                        }
                    }
                    if (fraction > 0f) {
                        val endX = size.width * fraction
                        drawLine(
                            brush =
                                Brush.horizontalGradient(
                                    listOf(GlassPalette.mint, GlassPalette.lavender, GlassPalette.peach),
                                    endX = endX.coerceAtLeast(1f),
                                ),
                            start = Offset(0f, y),
                            end = Offset(endX, y),
                            strokeWidth = 6.dp.toPx(),
                            cap = StrokeCap.Round,
                            alpha = 0.85f * dim,
                        )
                    }
                }
            }
        },
    )
}
