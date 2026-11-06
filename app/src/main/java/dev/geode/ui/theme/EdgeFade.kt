package dev.geode.ui.theme

import androidx.compose.foundation.gestures.ScrollableState
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Fades the ends of a scrolling container that has more to show that way, so a row cut at the
 * edge (a tab label, a line of text under a page header) dissolves into the glass and reads as
 * "scroll for more" instead of a half-cut word. An end with nothing beyond it is drawn as is, so a
 * list at its top or a row that fits looks unchanged. Read in the draw pass: scrolling redraws the
 * fade without recomposing.
 */
fun Modifier.edgeFade(
    state: ScrollableState,
    horizontal: Boolean,
    length: Dp = 24.dp,
): Modifier =
    graphicsLayer { compositingStrategy = CompositingStrategy.Offscreen }
        .drawWithContent {
            drawContent()
            val extent = if (horizontal) size.width else size.height
            val fade = length.toPx().coerceAtMost(extent / 2)

            fun at(v: Float) = if (horizontal) Offset(v, 0f) else Offset(0f, v)
            if (state.canScrollBackward) {
                drawRect(
                    Brush.linearGradient(0f to Color.Transparent, 1f to Color.Black, start = at(0f), end = at(fade)),
                    blendMode = BlendMode.DstIn,
                )
            }
            if (state.canScrollForward) {
                drawRect(
                    Brush.linearGradient(0f to Color.Black, 1f to Color.Transparent, start = at(extent - fade), end = at(extent)),
                    blendMode = BlendMode.DstIn,
                )
            }
        }
