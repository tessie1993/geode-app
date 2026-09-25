package dev.geode.ui.opaline

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.LocalTextSelectionColors
import androidx.compose.foundation.text.selection.TextSelectionColors
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.geode.ui.GuiPrefs

val LocalOpalineReducedMotion = staticCompositionLocalOf { false }
val LocalOpalinePalette = staticCompositionLocalOf { OpalineThemeId.DEFAULT.palette }
val LocalOpalineRenderer = staticCompositionLocalOf { OpalineRenderer.shared }

/** Accessors for the current Opaline theme. */
object Opaline {
    val palette: OpalinePalette
        @Composable @ReadOnlyComposable
        get() = LocalOpalinePalette.current

    val renderer: OpalineRenderer
        @Composable @ReadOnlyComposable
        get() = LocalOpalineRenderer.current

    val reducedMotion: Boolean
        @Composable @ReadOnlyComposable
        get() = LocalOpalineReducedMotion.current
}

@Composable
fun OpalineTheme(
    gui: GuiPrefs,
    content: @Composable () -> Unit,
) = OpalineTheme(
    theme = gui.materialTheme,
    textScale = gui.textScale,
    reducedMotion = gui.reducedMotion || gui.liquidMotion <= 0f,
    content = content,
)

@Composable
fun OpalineTheme(
    theme: OpalineThemeId = OpalineThemeId.DEFAULT,
    textScale: Float = 1f,
    reducedMotion: Boolean = false,
    content: @Composable () -> Unit,
) {
    val palette = theme.palette
    val renderer = LocalOpalineRenderer.current
    LaunchedEffect(renderer) { renderer.bake() }
    val scale = textScale.coerceIn(GuiPrefs.TEXT_SCALE_MIN, GuiPrefs.TEXT_SCALE_MAX)
    CompositionLocalProvider(
        LocalOpalinePalette provides palette,
        LocalOpalineRenderer provides renderer,
        LocalOpalineReducedMotion provides reducedMotion,
    ) {
        MaterialTheme(
            colorScheme =
                darkColorScheme(
                    primary = palette.accent,
                    onPrimary = palette.ink,
                    primaryContainer = palette.gel,
                    onPrimaryContainer = palette.onGel,
                    secondary = palette.gel,
                    onSecondary = palette.onGel,
                    secondaryContainer = palette.slab,
                    onSecondaryContainer = palette.text,
                    tertiary = palette.water,
                    onTertiary = palette.ink,
                    background = palette.environment,
                    onBackground = palette.text,
                    surface = palette.slab,
                    onSurface = palette.text,
                    surfaceVariant = palette.environmentLow,
                    onSurfaceVariant = palette.textMuted,
                    surfaceContainer = palette.slab,
                    surfaceContainerHigh = palette.slab,
                    surfaceContainerHighest = palette.slab,
                    outline = palette.gel.copy(alpha = 0.45f),
                    outlineVariant = palette.gel.copy(alpha = 0.2f),
                    error = palette.error,
                    onError = palette.ink,
                ),
            shapes =
                Shapes(
                    extraSmall = RoundedCornerShape(10.dp),
                    small = RoundedCornerShape(16.dp),
                    medium = RoundedCornerShape(22.dp),
                    large = RoundedCornerShape(28.dp),
                    extraLarge = RoundedCornerShape(36.dp),
                ),
            typography = opalineTypography(scale),
        ) {
            CompositionLocalProvider(
                LocalContentColor provides palette.text,
                LocalTextSelectionColors provides TextSelectionColors(palette.accent, palette.accent.copy(alpha = 0.35f)),
                content = content,
            )
        }
    }
}

private fun opalineTypography(scale: Float): Typography {
    fun style(
        size: Float,
        weight: FontWeight,
        tracking: Float = 0f,
        leading: Float = 1.3f,
    ) = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = weight,
        fontSize = (size * scale).sp,
        lineHeight = (size * scale * leading).sp,
        letterSpacing = tracking.sp,
    )
    return Typography(
        displayLarge = style(44f, FontWeight.Light, -0.5f, 1.15f),
        displayMedium = style(36f, FontWeight.Light, -0.3f, 1.15f),
        displaySmall = style(30f, FontWeight.Normal, -0.2f, 1.2f),
        headlineLarge = style(30f, FontWeight.Medium, -0.3f, 1.2f),
        headlineMedium = style(25f, FontWeight.Medium, -0.2f, 1.2f),
        headlineSmall = style(21f, FontWeight.Medium, -0.1f, 1.25f),
        titleLarge = style(19f, FontWeight.Medium),
        titleMedium = style(16f, FontWeight.Medium, 0.1f),
        titleSmall = style(14.5f, FontWeight.Medium, 0.1f),
        bodyLarge = style(16f, FontWeight.Normal, 0.15f, 1.45f),
        bodyMedium = style(14f, FontWeight.Normal, 0.15f, 1.45f),
        bodySmall = style(12.5f, FontWeight.Normal, 0.2f, 1.4f),
        labelLarge = style(14f, FontWeight.SemiBold, 0.2f),
        labelMedium = style(12f, FontWeight.Medium, 0.4f),
        labelSmall = style(11f, FontWeight.Medium, 0.5f),
    )
}
