package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Issue #240: the options sheet's edge snaps to peek (tabs only), half and tall. A drag or a fling
 * never hides it (owner: it rests at its tabs at the lowest; hiding is ×, the double tap and
 * Fullscreen), which [VisualsShellTest] covers on the nav side.
 */
class SheetSettleTest {
    private val anchors = sheetAnchors(roomPx = 2000f, peekPx = 200f)

    @Test
    fun anchorsArePeekHalfAndTall() {
        assertEquals(0f, anchors.getValue(SheetHeight.HIDDEN))
        assertEquals(200f, anchors.getValue(SheetHeight.PEEK))
        assertEquals(1000f, anchors.getValue(SheetHeight.HALF))
        assertEquals(2000f, anchors.getValue(SheetHeight.TALL))
    }

    @Test
    fun aSlowReleaseSnapsToTheNearestHeight() {
        assertEquals(SheetHeight.PEEK, settleHeight(350f, 0f, anchors))
        assertEquals(SheetHeight.HALF, settleHeight(1100f, 0f, anchors))
        assertEquals(SheetHeight.TALL, settleHeight(1700f, 0f, anchors))
    }

    @Test
    fun aFlingStepsToTheNextHeightInItsDirection() {
        assertEquals(SheetHeight.HALF, settleHeight(400f, FAST, anchors))
        assertEquals(SheetHeight.HALF, settleHeight(1800f, -FAST, anchors))
    }

    @Test
    fun noDragOrFlingEverHidesTheSheet() {
        listOf(0f, 50f, 200f).forEach { px ->
            listOf(-FAST, 0f, FAST).forEach { v ->
                assertTrue(settleHeight(px, v, anchors) != SheetHeight.HIDDEN)
            }
        }
    }

    private companion object {
        const val FAST = 5000f
    }
}
