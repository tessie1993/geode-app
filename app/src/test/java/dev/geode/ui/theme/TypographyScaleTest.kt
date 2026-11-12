package dev.geode.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import org.junit.Assert.assertEquals
import org.junit.Test

class TypographyScaleTest {
    private fun Typography.styles(): List<TextStyle> =
        listOf(
            displayLarge,
            displayMedium,
            displaySmall,
            headlineLarge,
            headlineMedium,
            headlineSmall,
            titleLarge,
            titleMedium,
            titleSmall,
            bodyLarge,
            bodyMedium,
            bodySmall,
            labelLarge,
            labelMedium,
            labelSmall,
        )

    @Test
    fun everyStyleScalesLikeAFontScale() {
        val base = Typography().styles()
        val scaled = geodeTypography(1.3f).styles()
        base.zip(scaled).forEach { (b, s) ->
            assertEquals(b.fontSize.value * 1.3f, s.fontSize.value, 0.001f)
            assertEquals(b.lineHeight.value * 1.3f, s.lineHeight.value, 0.001f)
            assertEquals(b.letterSpacing.value * 1.3f, s.letterSpacing.value, 0.001f)
            assertEquals(b.letterSpacing.type, s.letterSpacing.type)
        }
    }

    @Test
    fun oneIsMaterialsScale() {
        assertEquals(Typography().styles(), geodeTypography(1f).styles())
    }
}
