package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The stage's style order. The walk through it (wrap, locked styles, MilkDrop outside the order) and
 * what the step walks per tab are native now, pinned by core/tests/stage_step_test.cpp.
 */
class StageStyleTest {
    @Test
    fun theRealOrderHasNoDuplicates() {
        val real = stageStyleOrder()
        assertEquals(real.distinct(), real)
    }
}
