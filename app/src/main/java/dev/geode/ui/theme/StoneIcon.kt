package dev.geode.ui.theme

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.material3.LocalContentColor
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Fill
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

enum class StoneIconFinish { ETCHED, RELIEF }

object StoneIconDefaults {
    val Size: Dp = 24.dp
}

@Composable
fun StoneIconArt(
    icon: StoneIcon,
    contentDescription: String?,
    modifier: Modifier = Modifier,
    finish: StoneIconFinish = StoneIconFinish.ETCHED,
    tint: Color = LocalContentColor.current,
) {
    val palette = LocalThemePack.current.palette
    val glyph = remember(icon) { parseGlyph(icon) }

    val ink = tint.copy(alpha = 1f)
    val inkAlpha = tint.alpha
    val shadow =
        when (finish) {
            StoneIconFinish.ETCHED -> palette.backgroundDeep
            StoneIconFinish.RELIEF -> palette.glow
        }
    val highlight =
        when (finish) {
            StoneIconFinish.ETCHED -> palette.glow
            StoneIconFinish.RELIEF -> Color.White
        }

    val described =
        if (contentDescription == null) {
            Modifier
        } else {
            Modifier.semantics { this.contentDescription = contentDescription }
        }

    Canvas(modifier.then(Modifier.size(StoneIconDefaults.Size)).then(described)) {
        val factor = size.minDimension / STONE_ICON_VIEWPORT
        scale(factor, pivot = Offset.Zero) {
            drawGlyphLayer(glyph, ink, StoneIconLayer(shadow, SHADOW_WIDTH, SHADOW_ALPHA * inkAlpha, 1.5f, 2f))
            drawGlyphLayer(glyph, ink, StoneIconLayer(highlight, HIGHLIGHT_WIDTH, HIGHLIGHT_ALPHA * inkAlpha, -1f, -1f))
            drawGlyphLayer(glyph, ink, StoneIconLayer(ink, INK_WIDTH, inkAlpha, 0f, 0f))
        }
    }
}

private class StoneIconLayer(
    val stroke: Color,
    val width: Float,
    val alpha: Float,
    val dx: Float,
    val dy: Float,
)

private class ParsedGlyph(
    val stroked: List<Path>,
    val filled: List<Path>,
)

private fun parseGlyph(icon: StoneIcon): ParsedGlyph {
    val art = requireNotNull(StoneIconGeometry[icon]) { "no shipped geometry for icon $icon" }

    fun parse(data: List<String>) = data.map { PathParser().parsePathString(it).toPath() }
    return ParsedGlyph(stroked = parse(art.stroked), filled = parse(art.filled))
}

private fun DrawScope.drawGlyphLayer(
    glyph: ParsedGlyph,
    fill: Color,
    layer: StoneIconLayer,
) {
    translate(layer.dx, layer.dy) {
        val stroke = Stroke(width = layer.width, cap = StrokeCap.Round, join = StrokeJoin.Round)
        for (path in glyph.stroked) {
            drawPath(path, layer.stroke, alpha = layer.alpha, style = stroke)
        }
        for (path in glyph.filled) {
            drawPath(path, fill, alpha = layer.alpha, style = Fill)
        }
    }
}

private const val SHADOW_WIDTH = 7f
private const val SHADOW_ALPHA = 0.48f
private const val HIGHLIGHT_WIDTH = 5f
private const val HIGHLIGHT_ALPHA = 0.34f
private const val INK_WIDTH = 5.5f
