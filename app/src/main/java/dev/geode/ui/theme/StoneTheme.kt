package dev.geode.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import dev.geode.R
import dev.geode.ui.ThemeContrast

val LocalThemePack = staticCompositionLocalOf { ThemePackCatalog.all.first() }

val MaliFamily =
    FontFamily(
        Font(R.font.mali_regular, FontWeight.Normal),
        Font(R.font.mali_medium, FontWeight.Medium),
        Font(R.font.mali_semibold, FontWeight.SemiBold),
        Font(R.font.mali_bold, FontWeight.Bold),
    )

val MysteryQuestFamily = FontFamily(Font(R.font.mystery_quest_regular, FontWeight.Normal))

fun stoneTypography(textScale: Float = 1f): Typography {
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
        displayLarge = style(MysteryQuestFamily, 42, FontWeight.Normal),
        displayMedium = style(MysteryQuestFamily, 36, FontWeight.Normal),
        displaySmall = style(MysteryQuestFamily, 30, FontWeight.Normal),
        headlineLarge = style(MysteryQuestFamily, 30, FontWeight.Normal),
        headlineMedium = style(MaliFamily, 26, FontWeight.SemiBold),
        headlineSmall = style(MaliFamily, 24, FontWeight.SemiBold),
        titleLarge = style(MaliFamily, 22, FontWeight.SemiBold),
        titleMedium = style(MaliFamily, 18, FontWeight.Medium),
        titleSmall = style(MaliFamily, 16, FontWeight.Medium),
        bodyLarge = style(MaliFamily, 16, FontWeight.Normal),
        bodyMedium = style(MaliFamily, 15, FontWeight.Normal),
        bodySmall = style(MaliFamily, 13, FontWeight.Normal),
        labelLarge = style(MaliFamily, 14, FontWeight.Medium),
        labelMedium = style(MaliFamily, 13, FontWeight.Medium),
        labelSmall = style(MaliFamily, 12, FontWeight.Medium),
    )
}

fun ThemePack.colorScheme(
    accentIntensity: Float = 1f,
    backgroundDim: Float = 0f,
    fontColorOverride: Color? = null,
): ColorScheme {
    val p = palette

    fun Color.intensity(): Color = Color(dev.geode.ui.ColorDerive.scaleSaturation(toArgbInt(), accentIntensity))

    fun Color.dimmed(): Color = Color(dev.geode.ui.ColorDerive.dim(toArgbInt(), backgroundDim))
    val primary = p.primary.intensity()
    val secondary = p.secondary.intensity()
    val background = p.background.dimmed()
    val surface = p.surface.dimmed()
    val surfaceHigh = p.surfaceHigh.dimmed()

    val onBackground = readableOn(p.onBackground, background, ThemeContrast.BODY_CONTRAST_MIN)
    val onSurface = readableOn(p.onSurface, surface, ThemeContrast.BODY_CONTRAST_MIN)
    val base =
        if (isLight) {
            lightColorScheme(
                primary = primary,
                onPrimary = p.onSurface,
                secondary = secondary,
                tertiary = p.accent.intensity(),
                background = background,
                onBackground = onBackground,
                surface = surface,
                onSurface = onSurface,
                surfaceVariant = surfaceHigh,
                onSurfaceVariant = readableOn(p.muted, surfaceHigh, ThemeContrast.HINT_CONTRAST_MIN),
                surfaceContainer = surface,
                surfaceContainerHigh = surfaceHigh,
                primaryContainer = surfaceHigh,
                onPrimaryContainer = readableOn(p.accent, surfaceHigh, ThemeContrast.HINT_CONTRAST_MIN),
                secondaryContainer = surfaceHigh,
                onSecondaryContainer = readableOn(p.onSurface, surfaceHigh, ThemeContrast.BODY_CONTRAST_MIN),
                outline = p.outline,
                error = p.danger,
            )
        } else {
            darkColorScheme(
                primary = primary,
                onPrimary = p.background,
                secondary = secondary,
                tertiary = p.accent.intensity(),
                background = background,
                onBackground = onBackground,
                surface = surface,
                onSurface = onSurface,
                surfaceVariant = surfaceHigh,
                onSurfaceVariant = readableOn(p.muted, surfaceHigh, ThemeContrast.HINT_CONTRAST_MIN),
                surfaceContainer = surface,
                surfaceContainerHigh = surfaceHigh,
                primaryContainer = surfaceHigh,
                onPrimaryContainer = readableOn(p.accent, surfaceHigh, ThemeContrast.HINT_CONTRAST_MIN),
                secondaryContainer = surfaceHigh,
                onSecondaryContainer = readableOn(p.onSurface, surfaceHigh, ThemeContrast.BODY_CONTRAST_MIN),
                outline = p.outline,
                error = p.danger,
            )
        }
    return if (fontColorOverride != null) {
        base.copy(
            onBackground = fontColorOverride,
            onSurface = fontColorOverride,
            onSurfaceVariant = fontColorOverride,
            onPrimaryContainer = fontColorOverride,
            onSecondaryContainer = fontColorOverride,
            onTertiaryContainer = fontColorOverride,
            onErrorContainer = fontColorOverride,
        )
    } else {
        base
    }
}

private fun readableOn(
    authored: Color,
    surface: Color,
    minRatio: Float,
): Color {
    if (contrastRatio(authored, surface) >= minRatio) return authored
    val end = if (contrastRatio(Color.Black, surface) >= contrastRatio(Color.White, surface)) Color.Black else Color.White
    var t = 0f
    var tone = authored
    while (t < 1f && contrastRatio(tone, surface) < minRatio) {
        t = minOf(1f, t + 0.08f)
        tone = Color(dev.geode.ui.ColorDerive.lerpArgb(authored.toArgbInt(), end.toArgbInt(), t))
    }
    return tone
}

private fun contrastRatio(
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
