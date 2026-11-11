package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class TimeLeftTest {
    @Test
    fun showsWhatIsLeftAfterAMinus() {
        assertEquals("−2:30", formatTimeLeft(positionMs = 30_000, durationMs = 180_000))
        assertEquals("−1:00:00", formatTimeLeft(positionMs = 0, durationMs = 3_600_000))
    }

    @Test
    fun neverGoesBelowZero() {
        assertEquals("−0:00", formatTimeLeft(positionMs = 200_000, durationMs = 180_000))
    }

    @Test
    fun anUnknownLengthReadsZero() {
        assertEquals("0:00", formatTimeLeft(positionMs = 5_000, durationMs = 0))
    }
}
