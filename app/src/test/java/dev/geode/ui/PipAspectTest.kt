package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** The canvas-to-PiP aspect rule (#863): the platform's accepted range, orientation kept. */
class PipAspectTest {
    @Test
    fun anInRangeCanvasKeepsItsShape() {
        val (num, den) = pipAspectFraction(1920, 1080)
        assertEquals(1920f / 1080f, num.toFloat() / den, 0.01f)
    }

    @Test
    fun aTooWideCanvasIsClampedToTheWideLimit() {
        val (num, den) = pipAspectFraction(3000, 500)
        assertEquals(3000, num)
        assertTrue(num.toFloat() / den <= PIP_MAX_ASPECT + 0.01f)
        assertTrue(num > den)
    }

    @Test
    fun aTooTallCanvasIsClampedToTheTallLimit() {
        val (num, den) = pipAspectFraction(500, 3000)
        assertEquals(3000, den)
        assertTrue(num.toFloat() / den >= PIP_MIN_ASPECT - 0.01f)
        assertTrue(den > num)
    }

    @Test
    fun aRotationGivesTheTurnedRatio() {
        val portrait = pipAspectFraction(1080, 2400)
        val landscape = pipAspectFraction(2400, 1080)
        assertTrue(portrait.first < portrait.second)
        assertTrue(landscape.first > landscape.second)
    }
}
