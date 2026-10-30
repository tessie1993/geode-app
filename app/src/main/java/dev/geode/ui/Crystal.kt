package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.GeodeColors
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.LocalReducedMotion
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.geodeColorScheme
import dev.geode.ui.theme.geodeTypography
import dev.geode.ui.theme.rememberUiSounds
import kotlin.math.max

/**
 * The app's one theme: black glass on the Material 3 type scale.
 *
 * The `Crystal*` names are kept as the seam every screen already calls through; what they draw
 * is now plain Material with the [GeodeColors]/[GeodeGlass] tokens.
 */
@Composable
internal fun CrystalMaterialTheme(
    gui: GuiPrefs,
    content: @Composable () -> Unit,
) {
    val fontColor = gui.fontColorOverride?.let { Color(it) }?.takeIf { dev.geode.ui.theme.isReadableOnCanvas(it) }
    val scheme = geodeColorScheme(accentIntensity = gui.accentIntensity, fontColorOverride = fontColor)
    // The system's "remove animations" switch counts as reduced motion too.
    val reducedMotion = gui.reducedMotion || !android.animation.ValueAnimator.areAnimatorsEnabled()
    CompositionLocalProvider(
        LocalFontColor provides fontColor,
        LocalContentColor provides scheme.onBackground,
        LocalReducedMotion provides reducedMotion,
        LocalUiSounds provides rememberUiSounds(gui.uiSounds),
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
    geodeTypography(
        textScale.coerceIn(GuiPrefs.TEXT_SCALE_MIN, GuiPrefs.TEXT_SCALE_MAX),
    )

fun crystalShardShape(
    cut: Dp = 12.dp,
    minor: Dp = 4.dp,
): Shape = RoundedCornerShape(cut + minor)

/** A small selection dot: the indicator under the selected tab and nav item. */
@Composable
fun CrystalGem(
    color: Color,
    modifier: Modifier = Modifier,
    size: Dp = 7.dp,
    @Suppress("UNUSED_PARAMETER") glow: Boolean = true,
) {
    Box(
        modifier
            .size(size)
            .clip(CircleShape)
            .background(color),
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
                letterSpacing = 1.2.sp,
                fontWeight = FontWeight.Medium,
            ),
        color = color,
    )
}

@Composable
fun GlowTitle(
    text: String,
    modifier: Modifier = Modifier,
    style: TextStyle = MaterialTheme.typography.headlineSmall,
    @Suppress("UNUSED_PARAMETER") glow: Color = MaterialTheme.colorScheme.primary,
) {
    Text(text, modifier, style = style, color = MaterialTheme.colorScheme.onBackground)
}

/** A faint halo behind an active control. Kept subtle: black glass lights things, it doesn't glow. */
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
                    0f to color.copy(alpha = 0.16f * strength),
                    1f to Color.Transparent,
                    center = center,
                    radius = r,
                ),
            radius = r,
            center = center,
        )
    }

/**
 * A glass panel: [tint] at [opacity] over whatever is behind it, a faint top sheen and a hairline
 * edge. [glow] tints the edge, [glowStrength] how visible it is; [prismatic] marks a selected
 * panel with a brighter edge.
 */
fun Modifier.crystalPanel(
    opacity: Float,
    tint: Color,
    glow: Color,
    corner: Dp = 18.dp,
    glowStrength: Float = 1f,
    prismatic: Boolean = false,
): Modifier {
    val shape = RoundedCornerShape(corner)
    val fill = tint.copy(alpha = opacity.coerceIn(0f, 1f))
    val edge =
        if (prismatic) {
            glow.copy(alpha = GeodeGlass.EDGE_SELECTED)
        } else {
            Color.White.copy(alpha = (GeodeGlass.EDGE * glowStrength.coerceIn(0.4f, 1.4f)).coerceAtMost(1f))
        }
    return this
        .clip(shape)
        .background(fill)
        .background(
            Brush.verticalGradient(
                0f to Color.White.copy(alpha = GeodeGlass.SHEEN),
                0.5f to Color.Transparent,
            ),
        ).border(1.dp, edge, shape)
}

fun Modifier.luminousHairline(glow: Color): Modifier = background(glow.copy(alpha = glow.alpha * 0.5f))

/** The app canvas behind every screen: black, with a barely-there lift towards the top. */
@Composable
fun CrystalBackground(
    modifier: Modifier = Modifier,
    @Suppress("UNUSED_PARAMETER") reducedMotion: Boolean = false,
) {
    Box(
        modifier.background(
            Brush.verticalGradient(
                0f to GeodeColors.CanvasRaised,
                0.6f to GeodeColors.Canvas,
            ),
        ),
    )
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
