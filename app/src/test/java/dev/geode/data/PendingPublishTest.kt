package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PendingPublishTest {
    private class FakeRows(
        private val counts: List<Int>,
    ) : PendingRows<String> {
        var calls = 0
        val deleted = mutableListOf<String>()

        override fun clearPending(row: String): Int = counts.getOrElse(calls++) { 0 }

        override fun delete(row: String) {
            deleted += row
        }
    }

    @Test
    fun zeroRowsReportsFailureAndDeletesTheRow() {
        val rows = FakeRows(listOf(0, 0))
        assertFalse(publishPending(rows, "row"))
        assertEquals(2, rows.calls)
        assertEquals(listOf("row"), rows.deleted)
    }

    @Test
    fun oneRowPublishesWithoutDeleting() {
        val rows = FakeRows(listOf(1))
        assertTrue(publishPending(rows, "row"))
        assertTrue(rows.deleted.isEmpty())
    }

    @Test
    fun aRetryThatWorksKeepsTheRow() {
        val rows = FakeRows(listOf(0, 1))
        assertTrue(publishPending(rows, "row"))
        assertTrue(rows.deleted.isEmpty())
    }
}
