package dev.geode.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Text on the floating glass over the worst frame the visual can draw, pure white (#559).
 *
 * Black at the floor alpha over white, with the raised fill on top, is the lightest backdrop any
 * text on glass gets; these hold every text role, the outline and the accent rule to WCAG's
 * minimums against it, so a token change that lets the glass go lighter or a colour go dimmer
 * fails here instead of on a bright style.
 */
class GlassContrastTest {
    @Test
    fun worstBackdropIsTheFloorOverWhiteWithTheRaisedFill() {
        // 1 − 0.72 = 0.28, then 5% white over it: 0.28 + 0.05 × 0.72 = 0.316 in sRGB.
        // An sRGB Color keeps 8 bits a channel, so each copy(alpha) and compositeOver rounds to a
        // 1/255 step (0.72 is stored as 184/255, the grey lands on 80/255 = 0.3137): the tolerance
        // is that one step. A real token change (the floor 0.01 lower) moves it by more than two.
        val grey = GeodeGlass.WorstBackdrop
        assertEquals(0.316f, grey.red, 1f / 255f)
        assertEquals(grey.red, grey.green, 0.0001f)
        assertEquals(grey.red, grey.blue, 0.0001f)
        assertEquals(1f, grey.alpha, 0.0001f)
    }

    @Test
    fun theDefaultGlassIsNoLighterThanTheFloor() {
        assertTrue(GeodeGlass.MATTE_FILL >= GeodeGlass.MATTE_FILL_MIN)
        assertTrue(GeodeGlass.MATTE_FILL <= GeodeGlass.MATTE_FILL_MAX)
    }

    @Test
    fun textRolesClearTheSmallTextMinimumOnTheWorstGlass() {
        val roles =
            mapOf(
                "OnSurface" to GeodeColors.OnSurface,
                "OnSurfaceVariant" to GeodeColors.OnSurfaceVariant,
                "OnCanvas" to GeodeColors.OnCanvas,
                "Secondary" to GeodeColors.Secondary,
                "Primary" to GeodeColors.Primary,
            )
        roles.forEach { (name, color) ->
            val ratio = contrastOnGlass(color)
            assertTrue("$name reads $ratio:1 on the worst glass", ratio >= GeodeContrast.BODY_MIN)
        }
    }

    @Test
    fun outlineClearsTheNonTextMinimumOnTheWorstGlass() {
        val ratio = contrastOnGlass(GeodeColors.Outline)
        assertTrue("Outline reads $ratio:1 on the worst glass", ratio >= GeodeContrast.LARGE_MIN)
    }

    @Test
    fun theOldTokensFailedWhichIsWhyTheyChanged() {
        // #A6A8B2 over the old 0.62 tab bar on white: about 2.6:1, under the minimum.
        val oldTabBar = GeodeColors.Canvas.copy(alpha = 0.62f).compositeOverWhite()
        assertTrue(contrastRatio(Color(0xFFA6A8B2), oldTabBar) < GeodeContrast.BODY_MIN)
        // OnSurface over the old 0.35 floor on white: about 2:1.
        val oldFloor = GeodeColors.Canvas.copy(alpha = 0.35f).compositeOverWhite()
        assertTrue(contrastRatio(GeodeColors.OnSurface, oldFloor) < GeodeContrast.BODY_MIN)
    }

    @Test
    fun accentsFromDarkVisualsAreLiftedUntilTheyReadOnTheGlass() {
        val sources =
            listOf(
                Color(0xFF0000FF), // pure blue, the darkest hue
                Color(0xFF00008B), // dark blue
                Color(0xFF8B0000), // dark red
                Color(0xFF6A0DAD), // purple
                Color(0xFF006400), // dark green
                Color(0xFFFF00FF), // magenta
                Color(0xFFFFA500), // orange, already light
            )
        sources.forEach { source ->
            val accent = VisualAccent.from(source)
            val onGlass = contrastOnGlass(accent)
            assertTrue("accent from $source reads $onGlass:1 on the worst glass", onGlass >= GeodeContrast.BODY_MIN)
            assertTrue(contrastRatio(accent, GeodeColors.Canvas) >= VisualAccent.MIN_CONTRAST)
            // The lifted accent still leaves room for the dark label a filled button puts on it.
            assertTrue(contrastRatio(VisualAccent.onAccent(accent), accent) >= GeodeContrast.BODY_MIN)
        }
    }

    @Test
    fun anAccentAtOnlySevenToOneOnBlackIsNotEnoughOnGlass() {
        // #6E9BFF is about 7.8:1 on black, so the old black-only rule kept it as it was, but only
        // about 3:1 on the worst glass. The rule must now lift it.
        val source = Color(0xFF6E9BFF)
        assertTrue(contrastRatio(source, GeodeColors.Canvas) >= VisualAccent.MIN_CONTRAST)
        assertTrue(contrastOnGlass(source) < GeodeContrast.BODY_MIN)
        val accent = VisualAccent.from(source)
        assertNotEquals(source, accent)
        assertTrue(VisualAccent.readable(accent))
    }

    private fun Color.compositeOverWhite(): Color = compositeOver(Color.White)
}
