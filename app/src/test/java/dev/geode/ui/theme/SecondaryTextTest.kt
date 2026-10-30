package dev.geode.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SecondaryTextTest {
    private fun onCanvas(color: Color) = contrastRatio(color.compositeOver(GeodeColors.Canvas), GeodeColors.Canvas)

    @Test
    fun brightFontKeepsTheUsualDimming() {
        val secondary = secondaryTextFor(Color.White)
        assertEquals(0.78f, secondary.alpha, 0.001f)
    }

    @Test
    fun borderlineFontIsDimmedLessSoItStaysReadable() {
        // About 4.7:1 on black on its own; dimmed to 78% it would fall to about 3.2:1.
        val grey = Color(0xFF777777)
        assertTrue(isReadableOnCanvas(grey))
        assertTrue(onCanvas(grey.copy(alpha = 0.78f)) < GeodeContrast.BODY_MIN)

        val secondary = secondaryTextFor(grey)
        assertTrue(onCanvas(secondary) >= GeodeContrast.BODY_MIN)
        assertTrue(secondary.alpha > 0.78f)
    }

    @Test
    fun outlineClearsTheNonTextMinimum() {
        assertTrue(contrastRatio(GeodeColors.Outline, GeodeColors.Canvas) >= 3f)
    }
}
