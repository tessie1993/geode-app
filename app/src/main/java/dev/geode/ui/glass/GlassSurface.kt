package dev.geode.ui.glass

import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.drawOutline
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.translate

/**
 * Draws the frosted-glass look shared by every primitive in `ui/glass`: a pastel-tinted soft
 * shadow under the shape, a translucent white body, a faint iridescent tint, a lighter rim and a
 * top-left specular highlight. Pure [drawWithCache] drawing (layered, decreasing-alpha shapes
 * standing in for a blurred shadow) so it works down to API 26 without
 * [android.graphics.RenderEffect]. Everything is drawn with [androidx.compose.ui.draw.drawWithCache]
 * `onDrawBehind`, so it never blurs or otherwise touches the element's own content (icon/text).
 *
 * @param shape the outline to draw and clip to.
 * @param tint an accent colour mixed into the body and rim (e.g. mint for a selected pill).
 * @param selected draws a stronger tint wash, matching the "Home" pill in ref-05.
 * @param glow 0..1 strength of an outer glow ring (press/hold feedback layers this in).
 */
fun Modifier.glassSurface(
    shape: Shape = GlassShapes.tile,
    tint: Color? = null,
    selected: Boolean = false,
    glow: Float = 0f,
): Modifier =
    composed {
        this.drawWithCache {
            val outline = shape.createOutline(size, layoutDirection, this)
            val path = Path().apply { addOutline(outline) }
            onDrawBehind {
                drawGlass(outline, path, tint, selected, glow)
            }
        }
    }

private fun DrawScope.drawGlass(
    outline: Outline,
    path: Path,
    tint: Color?,
    selected: Boolean,
    glow: Float,
) {
    drawGlassShadow(outline)
    clipPath(path) {
        drawGlassBody(tint, selected)
        drawGlassSpecular()
    }
    drawGlassRim(outline, tint)
    if (glow > 0.01f) drawGlassGlowRing(outline, tint, glow)
}

/**
 * Layered, decreasing-alpha copies of the shape offset downward: a shadow that reads as blurred
 * without an actual blur pass, so it looks right on API 26 too.
 */
private fun DrawScope.drawGlassShadow(outline: Outline) {
    val maxOffset = GlassElevation.shadowBlur.toPx() * 0.6f
    val layerCount = 4
    for (i in layerCount downTo 1) {
        val t = i / layerCount.toFloat()
        val alpha = 0.10f * (1f - t + 0.25f)
        translate(top = maxOffset * t) {
            drawOutline(outline, color = GlassPalette.glassShadow.copy(alpha = alpha))
        }
    }
}

private fun DrawScope.drawGlassBody(
    tint: Color?,
    selected: Boolean,
) {
    drawRect(GlassPalette.glassFill)
    drawRect(
        brush =
            Brush.linearGradient(
                colors =
                    listOf(
                        GlassPalette.mint.copy(alpha = 0.10f),
                        GlassPalette.lavender.copy(alpha = 0.10f),
                        GlassPalette.peach.copy(alpha = 0.10f),
                    ),
                start = Offset(0f, 0f),
                end = Offset(size.width, size.height),
            ),
    )
    val wash = tint ?: GlassPalette.mint
    if (selected) {
        drawRect(wash.copy(alpha = 0.34f))
    } else if (tint != null) {
        drawRect(wash.copy(alpha = 0.16f))
    }
}

private fun DrawScope.drawGlassRim(
    outline: Outline,
    tint: Color?,
) {
    val rimColor = if (tint != null) tint.copy(alpha = 0.7f) else GlassPalette.glassRim
    drawOutline(outline, color = rimColor, style = Stroke(width = GlassElevation.rimWidth.toPx()))
}

private fun DrawScope.drawGlassSpecular() {
    val radius = size.minDimension * 0.9f
    val center = Offset(size.width * 0.22f, size.height * 0.18f)
    drawCircle(
        brush =
            Brush.radialGradient(
                colors = listOf(Color.White.copy(alpha = GlassElevation.SPECULAR_ALPHA), Color.Transparent),
                center = center,
                radius = radius,
            ),
        radius = radius,
        center = center,
    )
}

private fun DrawScope.drawGlassGlowRing(
    outline: Outline,
    tint: Color?,
    glow: Float,
) {
    val glowColor = (tint ?: GlassPalette.mint).copy(alpha = (0.55f * glow).coerceIn(0f, 0.55f))
    drawOutline(
        outline,
        color = glowColor,
        style = Stroke(width = GlassElevation.glowBlur.toPx() * 0.25f * glow),
        blendMode = BlendMode.Plus,
    )
}
