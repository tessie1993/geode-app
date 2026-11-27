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

    @Test
    fun aFrameBudgetOfOnePeriodKeepsTheDefaultThreshold() {
        assertEquals(2f, JankTally.jankMultiplier(16_666_667L, 16_666_667L), 0.001f)
    }

    @Test
    fun aBudgetOfTwoPeriodsDoublesTheThreshold() {
        assertEquals(4f, JankTally.jankMultiplier(33_333_334L, 16_666_667L), 0.001f)
    }

    @Test
    fun noBudgetYetOrNoPeriodKeepsTheDefault() {
        assertEquals(JankTally.DEFAULT_MULTIPLIER, JankTally.jankMultiplier(0L, 16_666_667L), 0f)
        assertEquals(JankTally.DEFAULT_MULTIPLIER, JankTally.jankMultiplier(16_666_667L, 0L), 0f)
    }
}
