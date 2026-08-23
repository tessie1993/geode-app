package dev.geode.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.imageResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.LocalThemePack
import dev.geode.ui.theme.ThemePack
import dev.geode.ui.theme.colorScheme
import dev.geode.ui.theme.stoneTypography
import kotlin.math.max
import kotlin.math.roundToInt

@Composable
internal fun CrystalMaterialTheme(
    pack: ThemePack,
    gui: GuiPrefs,
    content: @Composable () -> Unit,
) {
    val fontColor = pack.resolvedFontColor(gui.fontColorOverride, gui.backgroundDim)
    val scheme =
        pack.colorScheme(
            accentIntensity = gui.accentIntensity,
            backgroundDim = gui.backgroundDim,
            fontColorOverride = fontColor?.let { Color(it) },
        )
    CompositionLocalProvider(
        LocalThemePack provides pack,
        LocalFontColor provides fontColor?.let { Color(it) },
        LocalContentColor provides scheme.onBackground,
    ) {
        MaterialTheme(
            colorScheme = scheme,
            shapes = gui.cornerStyle.shapes(),
            typography = crystalTypography(gui.textScale),
            content = content,
        )
    }
}

fun crystalTypography(textScale: Float = 1f): Typography =
    stoneTypography(
        textScale.coerceIn(GuiPrefs.TEXT_SCALE_MIN, GuiPrefs.TEXT_SCALE_MAX),
    )

fun crystalShardShape(
    cut: Dp = 12.dp,
    minor: Dp = 4.dp,
): Shape = RoundedCornerShape((cut + minor) * 1.2f)

@Composable
fun CrystalGem(
    color: Color,
    modifier: Modifier = Modifier,
    size: Dp = 7.dp,
    glow: Boolean = true,
) {
    Box(
        modifier
            .size(size)
            .then(if (glow) Modifier.softGlow(color, size) else Modifier)
            .rotate(45f)
            .background(Brush.linearGradient(listOf(lerp(color, Color.White, 0.55f), color))),
    )
}

@Composable
fun CrystalOverline(
    text: String,
    modifier: Modifier = Modifier,
    color: Color = accentTextColor(),
) {
    Text(
        text.uppercase(),
        modifier,
        style =
            MaterialTheme.typography.labelSmall.copy(
                letterSpacing = 2.6.sp,
                fontWeight = FontWeight.Medium,
            ),
        color = color.copy(alpha = 0.9f),
    )
}

@Composable
fun GlowTitle(
    text: String,
    modifier: Modifier = Modifier,
    style: TextStyle = MaterialTheme.typography.headlineSmall,
    glow: Color = MaterialTheme.colorScheme.primary,
) {
    Text(
        text,
        modifier,
        style = style.copy(shadow = Shadow(color = glow.copy(alpha = 0.8f), blurRadius = 28f)),
        color = MaterialTheme.colorScheme.onBackground,
    )
}

fun Modifier.softGlow(
    color: Color,
    radius: Dp = 16.dp,
    strength: Float = 1f,
): Modifier =
    drawBehind {
        val r = max(size.width, size.height) / 2f + radius.toPx()
        drawCircle(
            brush =
                Brush.radialGradient(
                    0f to color.copy(alpha = 0.45f * strength),
                    1f to Color.Transparent,
                    center = center,
                    radius = r,
                ),
            radius = r,
            center = center,
        )
    }

fun Modifier.crystalPanel(
    opacity: Float,
    tint: Color,
    glow: Color,
    corner: Dp = 18.dp,
    glowStrength: Float = 1f,
    facets: Float = 1f,
    prismatic: Boolean = false,
    sheen: Color = glow,
): Modifier =
    composed {
        val pack = LocalThemePack.current
        val tile = ImageBitmap.imageResource(pack.material.tile)
        val alpha = opacity.coerceIn(0f, 1f)
        val texAlpha = (pack.material.surfaceOpacity * facets.coerceIn(0f, 1.5f)).coerceIn(0f, 1f)
        val shape = RoundedCornerShape(corner)
        val borderBrush =
            if (prismatic) {
                Brush.sweepGradient(
                    listOf(
                        glow.copy(alpha = 0.9f),
                        Color.White.copy(alpha = 0.95f),
                        sheen.copy(alpha = 0.85f),
                        glow.copy(alpha = 0.35f),
                        sheen.copy(alpha = 0.8f),
                        Color.White.copy(alpha = 0.9f),
                        glow.copy(alpha = 0.9f),
                    ),
                )
            } else {
                Brush.verticalGradient(
                    0f to glow.copy(alpha = (0.85f * glowStrength).coerceAtMost(1f)),
                    0.55f to glow.copy(alpha = 0.22f * glowStrength),
                    1f to glow.copy(alpha = 0.45f * glowStrength),
                )
            }
        this
            .clip(shape)
            .background(tint.copy(alpha = alpha))
            .drawBehind {
                if (texAlpha > 0f) {
                    val tw = tile.width
                    val th = tile.height
                    if (tw > 0 && th > 0) {
                        clipRect {
                            var y = 0
                            while (y < size.height.roundToInt()) {
                                var x = 0
                                while (x < size.width.roundToInt()) {
                                    drawImage(
                                        tile,
                                        dstOffset = IntOffset(x, y),
                                        dstSize = IntSize(tw, th),
                                        alpha = texAlpha,
                                    )
                                    x += tw
                                }
                                y += th
                            }
                        }
                    }
                }
            }.border(1.dp, borderBrush, shape)
    }

fun Modifier.luminousHairline(glow: Color): Modifier =
    background(
        Brush.horizontalGradient(
            0f to Color.Transparent,
            0.5f to glow.copy(alpha = 0.7f),
            1f to Color.Transparent,
        ),
    )

@Composable
fun CrystalBackground(
    modifier: Modifier = Modifier,
    @Suppress("UNUSED_PARAMETER") reducedMotion: Boolean = false,
) {
    val pack = LocalThemePack.current
    val cs = MaterialTheme.colorScheme
    BoxWithConstraints(modifier) {
        val art =
            when {
                maxWidth > maxHeight -> pack.material.ambientLandscape
                else -> pack.material.ambientPortrait
            }
        Image(
            painter = painterResource(art),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxSize(),
        )
        Box(
            Modifier
                .fillMaxSize()
                .background(cs.background.copy(alpha = 0.16f)),
        )
    }
}

@Composable
fun VisualizerCanvasHost(
    view: VisualizerView,
    modifier: Modifier = Modifier,
) {
    AndroidView(
        factory = {
            (view.parent as? android.view.ViewGroup)?.removeView(view)
            view
        },
        modifier = modifier,
    )
}
