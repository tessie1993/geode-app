package dev.geode.widget

import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import org.junit.Assert.assertEquals
import org.junit.Test

class NowPlayingWidgetTest {
    @Test
    fun `each breakpoint maps to its own layout`() {
        assertEquals(WidgetLayout.SMALL, layoutFor(SMALL_SIZE))
        assertEquals(WidgetLayout.MEDIUM, layoutFor(MEDIUM_SIZE))
        assertEquals(WidgetLayout.LARGE, layoutFor(LARGE_SIZE))
    }

    @Test
    fun `a narrow but tall widget stays small`() {
        assertEquals(WidgetLayout.SMALL, layoutFor(DpSize(180.dp, 220.dp)))
    }

    @Test
    fun `artwork is scaled down to fit, keeping its aspect ratio`() {
        assertEquals(344 to 344, fitWithin(700, 700, 344))
        assertEquals(344 to 172, fitWithin(700, 350, 344))
        assertEquals(172 to 344, fitWithin(350, 700, 344))
    }

    @Test
    fun `artwork already small enough is left alone`() {
        assertEquals(200 to 120, fitWithin(200, 120, 344))
    }
}
