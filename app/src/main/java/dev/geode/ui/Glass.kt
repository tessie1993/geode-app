package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

fun Modifier.readingPlate(
    opacity: Float,
    tint: Color,
    corner: Dp = 0.dp,
    glow: Color? = null,
): Modifier {
    val a = opacity.coerceIn(0f, 0.92f)
    val shape: Shape = if (corner > 0.dp) RoundedCornerShape(corner) else RectangleShape
    return this
        .clip(shape)
        .background(
            Brush.verticalGradient(
                0f to lerp(tint, Color.Black, 0.4f).copy(alpha = (a + 0.14f).coerceAtMost(0.94f)),
                0.14f to tint.copy(alpha = a),
                0.86f to tint.copy(alpha = a),
                1f to lerp(tint, Color.Black, 0.4f).copy(alpha = (a + 0.1f).coerceAtMost(0.94f)),
            ),
        ).then(
            if (glow == null) {
                Modifier
            } else {
                Modifier
                    .border(
                        1.dp,
                        Brush.verticalGradient(
                            0f to glow.copy(alpha = 0.55f),
                            0.6f to glow.copy(alpha = 0.14f),
                            1f to glow.copy(alpha = 0.32f),
                        ),
                        shape,
                    )
            },
        )
}

fun Modifier.glassScrim(
    color: Color = Color.Black,
    maxAlpha: Float = 0.35f,
): Modifier =
    background(
        Brush.verticalGradient(
            0f to Color.Transparent,
            1f to color.copy(alpha = maxAlpha.coerceIn(0f, 1f)),
        ),
    )
