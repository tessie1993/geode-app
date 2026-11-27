package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class JankTallyTest {
    @Test
    fun theSummaryNamesTheJankyFramesAndTheWorstInMilliseconds() {
        assertEquals("janky frames 3 of 1800, worst 42 ms", JankTally.summaryOf(1800, 3, 42_500_000L))
    }

    @Test
    fun noFramesIsAnEmptyTally() {
        assertEquals("janky frames 0 of 0, worst 0 ms", JankTally.summaryOf(0, 0, 0))
    }
}
