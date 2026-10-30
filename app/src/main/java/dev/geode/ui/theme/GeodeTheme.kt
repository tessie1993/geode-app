package dev.geode.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.isSpecified

/** Minimum contrast ratios, from WCAG 2.1 AA. */
object GeodeContrast {
    const val BODY_MIN = 4.5f
    const val HINT_MIN = 3.0f
}

/**
 * The colour scheme, built from [GeodeColors].
 *
 * [accentIntensity] scales the accent's saturation (the Look setting); [fontColorOverride] is the
 * user's chosen text colour, applied only where it stays readable on the canvas.
 */
fun geodeColorScheme(
    accentIntensity: Float = 1f,
    fontColorOverride: Color? = null,
): ColorScheme {
    fun Color.intensity(): Color = Color(dev.geode.ui.ColorDerive.scaleSaturation(toArgbInt(), accentIntensity))
    val base =
        darkColorScheme(
            primary = GeodeColors.Primary.intensity(),
            onPrimary = GeodeColors.OnPrimary,
            primaryContainer = GeodeColors.PrimaryContainer,
            onPrimaryContainer = GeodeColors.OnPrimaryContainer,
            secondary = GeodeColors.Secondary,
            onSecondary = GeodeColors.OnSecondary,
            secondaryContainer = GeodeColors.SecondaryContainer,
            onSecondaryContainer = GeodeColors.OnSecondaryContainer,
            tertiary = GeodeColors.Tertiary.intensity(),
            onTertiary = GeodeColors.OnTertiary,
            background = GeodeColors.Canvas,
            onBackground = GeodeColors.OnCanvas,
            surface = GeodeColors.Surface,
            onSurface = GeodeColors.OnSurface,
            surfaceVariant = GeodeColors.SurfaceContainerHigh,
            onSurfaceVariant = GeodeColors.OnSurfaceVariant,
            surfaceContainerLowest = GeodeColors.CanvasRaised,
            surfaceContainerLow = GeodeColors.SurfaceContainerLow,
            surfaceContainer = GeodeColors.SurfaceContainer,
            surfaceContainerHigh = GeodeColors.SurfaceContainerHigh,
            surfaceContainerHighest = GeodeColors.SurfaceContainerHighest,
            surfaceBright = GeodeColors.SurfaceContainerHighest,
            surfaceDim = GeodeColors.Canvas,
            outline = GeodeColors.Outline,
            outlineVariant = GeodeColors.OutlineVariant,
            error = GeodeColors.Error,
            onError = GeodeColors.OnError,
            errorContainer = GeodeColors.ErrorContainer,
            onErrorContainer = GeodeColors.OnErrorContainer,
            scrim = GeodeColors.Scrim,
        )
    val font = fontColorOverride?.takeIf { isReadableOnCanvas(it) } ?: return base
    return base.copy(
        onBackground = font,
        onSurface = font,
        onSurfaceVariant = font.copy(alpha = 0.78f),
        onPrimaryContainer = font,
        onSecondaryContainer = font,
    )
}

/** Whether [color] meets the body-text contrast minimum against the black canvas. */
fun isReadableOnCanvas(color: Color): Boolean = contrastRatio(color, GeodeColors.Canvas) >= GeodeContrast.BODY_MIN

/**
 * The Material 3 type scale on the system sans, scaled by the user's text-size setting.
 *
 * The scale itself is Material's; the app only multiplies it, so system font and OEM font
 * choices carry through.
 */
fun geodeTypography(textScale: Float = 1f): Typography {
    val base = Typography()
    if (textScale == 1f) return base

    fun TextStyle.scaled(): TextStyle =
        copy(
            fontSize = fontSize.scaledBy(textScale),
            lineHeight = lineHeight.scaledBy(textScale),
        )
    return Typography(
        displayLarge = base.displayLarge.scaled(),
        displayMedium = base.displayMedium.scaled(),
        displaySmall = base.displaySmall.scaled(),
        headlineLarge = base.headlineLarge.scaled(),
        headlineMedium = base.headlineMedium.scaled(),
        headlineSmall = base.headlineSmall.scaled(),
        titleLarge = base.titleLarge.scaled(),
        titleMedium = base.titleMedium.scaled(),
        titleSmall = base.titleSmall.scaled(),
        bodyLarge = base.bodyLarge.scaled(),
        bodyMedium = base.bodyMedium.scaled(),
        bodySmall = base.bodySmall.scaled(),
        labelLarge = base.labelLarge.scaled(),
        labelMedium = base.labelMedium.scaled(),
        labelSmall = base.labelSmall.scaled(),
    )
}

private fun TextUnit.scaledBy(factor: Float): TextUnit = if (isSpecified) this * factor else this

internal fun contrastRatio(
    a: Color,
    b: Color,
): Float {
    val la = a.luminance()
    val lb = b.luminance()
    return (maxOf(la, lb) + 0.05f) / (minOf(la, lb) + 0.05f)
}

private fun Color.toArgbInt(): Int =
    ((alpha * 255f + 0.5f).toInt() shl 24) or
        ((red * 255f + 0.5f).toInt() shl 16) or
        ((green * 255f + 0.5f).toInt() shl 8) or
        (blue * 255f + 0.5f).toInt()
