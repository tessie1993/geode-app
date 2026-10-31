package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class StageStyleTest {
    private val order = listOf("a", "b", "c", "d")

    @Test
    fun swipesWrapAndSkipLockedStyles() {
        assertEquals("b", nextStageStyle(order, "a", 1) { false })
        assertEquals("a", nextStageStyle(order, "d", 1) { false })
        assertEquals("d", nextStageStyle(order, "a", -1) { false })
        assertEquals("d", nextStageStyle(order, "b", 1) { it == "c" })
    }

    @Test
    fun aStyleOutsideTheOrderStartsAtAnEnd() {
        assertEquals("a", nextStageStyle(order, "milkdrop", 1) { false })
        assertEquals("d", nextStageStyle(order, "milkdrop", -1) { false })
    }

    @Test
    fun nothingWhenEveryOtherStyleIsLocked() {
        assertNull(nextStageStyle(order, "a", 1) { it != "a" })
    }

    @Test
    fun theRealOrderHasNoDuplicates() {
        val real = stageStyleOrder()
        assertEquals(real.distinct(), real)
    }
}
