package dev.geode.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.isSpecified
import dev.geode.ui.ColorDerive

/**
 * The colour scheme, built from [GeodeColors].
 *
 * [accentSource] is the current visual's main colour; the primary colours are derived from it
 * through [VisualAccent] (its hue, lifted until it reads on black). Null, the default, keeps the
 * theme's own #9CC7FF family exactly as it was, so nothing changes on screen until a caller feeds
 * the visual's colour in. [accentIntensity] scales the accent's saturation (the Look setting);
 * [fontColorOverride] is the user's chosen text colour, applied only where it stays readable on
 * the canvas.
 */
fun geodeColorScheme(
    accentIntensity: Float = 1f,
    fontColorOverride: Color? = null,
    accentSource: Color? = null,
): ColorScheme {
    fun Color.intensity(): Color = Color(ColorDerive.scaleSaturation(toArgbInt(), accentIntensity))
    // Intensity first, then the rule, so a fully saturated Look setting cannot push the accent
    // back under the contrast floor.
    val accent = accentSource?.let { VisualAccent.from(it.intensity()) }
    val base =
        darkColorScheme(
            primary = accent ?: GeodeColors.Primary.intensity(),
            onPrimary = accent?.let(VisualAccent::onAccent) ?: GeodeColors.OnPrimary,
            primaryContainer = accent?.let(VisualAccent::container) ?: GeodeColors.PrimaryContainer,
            onPrimaryContainer = accent?.let(VisualAccent::onContainer) ?: GeodeColors.OnPrimaryContainer,
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
        onSurfaceVariant = secondaryTextFor(font),
        onPrimaryContainer = font,
        onSecondaryContainer = font,
    )
}

/**
 * Secondary text dims the user's font colour, but a font colour that only just passes on its own
 * would fail once dimmed (a 4.7:1 grey at 78% is 3.2:1), so the dimming backs off until the blended
 * colour still meets the body-text minimum, down to no dimming at all.
 */
internal fun secondaryTextFor(font: Color): Color {
    var alpha = SECONDARY_TEXT_ALPHA
    while (alpha < 1f) {
        val dimmed = font.copy(alpha = alpha)
        val blended = dimmed.compositeOver(GeodeColors.Canvas)
        if (contrastRatio(blended, GeodeColors.Canvas) >= GeodeContrast.BODY_MIN) return dimmed
        alpha += 0.04f
    }
    return font
}

private const val SECONDARY_TEXT_ALPHA = 0.78f

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

/**
 * The screen's one title (the heading TalkBack users jump between).
 *
 * This and the three roles below are the compact type scale: the redesign uses these sizes of
 * the M3 scale and no more, so screens read alike (one Title Large per screen, Title Small over
 * each group, Body Medium in rows, Body Small for a row's second line). Screens name the role,
 * not the size, so the scale can be retuned here.
 */
val Typography.screenTitle: TextStyle get() = titleLarge

/** A group's header inside a screen or sheet. */
val Typography.sectionHeader: TextStyle get() = titleSmall

/** A row's main line. */
val Typography.rowText: TextStyle get() = bodyMedium

/** A row's second line: its value or a short description. */
val Typography.rowSupporting: TextStyle get() = bodySmall

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
