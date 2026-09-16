package dev.geode.ui.glass

import androidx.compose.animation.core.Spring
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.geode.ui.GuiPrefs
import dev.geode.ui.theme.MaliFamily
import dev.geode.ui.theme.MysteryQuestFamily

/**
 * Colour tokens for the "liquid glass" design system (see docs/design/liquid-glass/README.md).
 * A single source of truth: every glass primitive reads these instead of hardcoding colour.
 */
object GlassPalette {
    val base = Color(0xFF8C95BB)
    val baseLight = Color(0xFFA4ACCB)
    val baseShadow = Color(0xFF6F78A3)

    val mint = Color(0xFFBFEBD8)
    val lavender = Color(0xFFCDBDF0)
    val peach = Color(0xFFF6CDB2)
    val pink = Color(0xFFF3BFD8)
    val sky = Color(0xFFBFD8F2)

    val glassFill: Color = Color.White.copy(alpha = 0.32f)
    val glassRim: Color = Color.White.copy(alpha = 0.55f)
    val glassShadow: Color = baseShadow.copy(alpha = 0.35f)

    val textPrimary: Color = Color.White.copy(alpha = 0.92f)
    val textSecondary: Color = Color.White.copy(alpha = 0.66f)
}

/** Shape tokens shared by every glass primitive. */
object GlassShapes {
    val pill: Shape = RoundedCornerShape(percent = 50)
    val bubble: Shape = CircleShape
    val tile: Shape = RoundedCornerShape(28.dp)
    val sheet: Shape = RoundedCornerShape(topStart = 32.dp, topEnd = 32.dp)

    /** M3 [Shapes] built from the same radii, so stock components read consistently too. */
    fun materialShapes(): Shapes =
        Shapes(
            extraSmall = RoundedCornerShape(12.dp),
            small = RoundedCornerShape(16.dp),
            medium = RoundedCornerShape(20.dp),
            large = RoundedCornerShape(28.dp),
            extraLarge = RoundedCornerShape(32.dp),
        )
}

/** Motion tokens for touch effects and drift animation, in the units each site needs. */
object GlassMotion {
    const val PRESS_SCALE = 0.96f
    val SPRING_STIFFNESS = Spring.StiffnessMediumLow
    const val RIPPLE_DURATION_MS = 900
    const val HOLD_THRESHOLD_MS = 350L
    const val INK_DURATION_MS = 1600
    const val DROP_STRETCH_MAX = 1.35f
    const val BUBBLE_DRIFT_PERIOD_MIN_MS = 12_000
    const val BUBBLE_DRIFT_PERIOD_MAX_MS = 20_000
}

/** Elevation / draw-effect tokens: blur radii, rim width, specular strength. */
object GlassElevation {
    val rimWidth: Dp = 1.dp
    val shadowBlur: Dp = 18.dp
    val glowBlur: Dp = 24.dp
    const val SPECULAR_ALPHA = 0.5f
}

/** Type scale for the glass UI: one family, light weights, titles 28 sp / body 16 sp / labels 13 sp. */
object GlassTypography {
    fun build(textScale: Float): Typography {
        fun style(
            family: FontFamily,
            size: Int,
            weight: FontWeight,
            lineHeight: Float = size * 1.35f,
        ) = TextStyle(
            fontFamily = family,
            fontWeight = weight,
            fontSize = (size * textScale).sp,
            lineHeight = (lineHeight * textScale).sp,
        )
        return Typography(
            displayLarge = style(MysteryQuestFamily, 34, FontWeight.Normal),
            displayMedium = style(MysteryQuestFamily, 30, FontWeight.Normal),
            displaySmall = style(MysteryQuestFamily, 28, FontWeight.Normal),
            headlineLarge = style(MaliFamily, 28, FontWeight.Normal),
            headlineMedium = style(MaliFamily, 24, FontWeight.Normal),
            headlineSmall = style(MaliFamily, 22, FontWeight.Normal),
            titleLarge = style(MaliFamily, 20, FontWeight.Medium),
            titleMedium = style(MaliFamily, 18, FontWeight.Medium),
            titleSmall = style(MaliFamily, 16, FontWeight.Medium),
            bodyLarge = style(MaliFamily, 16, FontWeight.Normal),
            bodyMedium = style(MaliFamily, 15, FontWeight.Normal),
            bodySmall = style(MaliFamily, 13, FontWeight.Normal),
            labelLarge = style(MaliFamily, 14, FontWeight.Normal),
            labelMedium = style(MaliFamily, 13, FontWeight.Normal),
            labelSmall = style(MaliFamily, 12, FontWeight.Normal),
        )
    }
}

/** Per-viewer glass settings that draw helpers and touch effects read without a prop-drilled param. */
internal data class GlassSettings(
    val reducedMotion: Boolean,
    val tint: Float,
    val opacity: Float,
    val bubbleDensity: Float,
    val liquidMotion: Float,
)

private val defaultGlassSettings = GlassSettings(
    reducedMotion = false,
    tint = 0.5f,
    opacity = 0.34f,
    bubbleDensity = 0.5f,
    liquidMotion = 1f,
)

internal val LocalGlass = staticCompositionLocalOf { defaultGlassSettings }

private fun glassColorScheme(): ColorScheme =
    darkColorScheme(
        primary = GlassPalette.mint,
        onPrimary = GlassPalette.baseShadow,
        secondary = GlassPalette.lavender,
        onSecondary = GlassPalette.baseShadow,
        tertiary = GlassPalette.peach,
        onTertiary = GlassPalette.baseShadow,
        background = GlassPalette.base,
        onBackground = GlassPalette.textPrimary,
        surface = GlassPalette.baseLight,
        onSurface = GlassPalette.textPrimary,
        surfaceVariant = GlassPalette.base,
        onSurfaceVariant = GlassPalette.textSecondary,
        surfaceContainer = GlassPalette.glassFill.copy(alpha = 0.14f).compositeOverBase(),
        surfaceContainerHigh = GlassPalette.glassFill.copy(alpha = 0.22f).compositeOverBase(),
        primaryContainer = GlassPalette.mint.copy(alpha = 0.28f).compositeOverBase(),
        onPrimaryContainer = GlassPalette.textPrimary,
        secondaryContainer = GlassPalette.lavender.copy(alpha = 0.28f).compositeOverBase(),
        onSecondaryContainer = GlassPalette.textPrimary,
        outline = GlassPalette.glassRim,
        error = GlassPalette.pink,
        onError = GlassPalette.baseShadow,
    )

/** Flattens a translucent tint onto the opaque base so M3 surfaces (which assume opaque colours) stay legible. */
private fun Color.compositeOverBase(): Color {
    val bg = GlassPalette.base
    val a = alpha
    return Color(
        red = red * a + bg.red * (1f - a),
        green = green * a + bg.green * (1f - a),
        blue = blue * a + bg.blue * (1f - a),
        alpha = 1f,
    )
}

/**
 * The single glass Material theme: one M3 dark colour scheme built from [GlassPalette] so stock
 * M3 components (dialogs, text fields, snackbars, …) read correctly even where a screen has not
 * yet been rewritten onto a dedicated Glass* primitive, plus [LocalGlass] for the draw helpers.
 */
@Composable
fun GlassMaterialTheme(
    gui: GuiPrefs,
    content: @Composable () -> Unit,
) {
    val settings =
        remember(gui.reducedMotion, gui.glassTint, gui.glassOpacity, gui.bubbleDensity, gui.liquidMotion) {
            GlassSettings(
                reducedMotion = gui.reducedMotion,
                tint = gui.glassTint,
                opacity = gui.glassOpacity,
                bubbleDensity = gui.bubbleDensity,
                liquidMotion = gui.liquidMotion,
            )
        }
    CompositionLocalProvider(LocalGlass provides settings) {
        MaterialTheme(
            colorScheme = glassColorScheme(),
            shapes = GlassShapes.materialShapes(),
            typography = GlassTypography.build(gui.textScale),
            content = content,
        )
    }
}
