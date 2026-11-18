package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class ListenBatchTest {
    private val added = mutableListOf<Pair<String, Long>>()
    private var disk = 0
    private val batch =
        ListenBatch(
            memoryIntervalMs = 1_000L,
            diskIntervalMs = 5_000L,
            addToHistory = { uri, ms -> added += uri to ms },
            writeToDisk = { disk++ },
        )

    @Test
    fun `half-second ticks reach the history once a second has built up, and the disk is not written`() {
        batch.add("a", 500)
        assertEquals(emptyList<Pair<String, Long>>(), added)
        batch.add("a", 500)
        assertEquals(listOf("a" to 1_000L), added)
        assertEquals(0, disk)
    }

    @Test
    fun `a track change adds what the old track gathered, in memory only`() {
        batch.add("a", 400)
        batch.add("b", 300)
        assertEquals(listOf("a" to 400L), added)
        assertEquals(0, disk)
    }

    @Test
    fun `commit adds the remainder once and writes the disk`() {
        batch.add("a", 200)
        batch.commit()
        batch.commit()
        assertEquals(listOf("a" to 200L), added)
        assertEquals(2, disk)
    }

    @Test
    fun `uninterrupted listening writes the disk once per safety interval`() {
        repeat(9) { batch.add("a", 500) }
        assertEquals(0, disk)
        batch.add("a", 500)
        assertEquals(1, disk)
        assertEquals(10 * 500L, added.sumOf { it.second })
        repeat(9) { batch.add("a", 500) }
        assertEquals(1, disk)
        batch.add("a", 500)
        assertEquals(2, disk)
    }

    @Test
    fun `a commit restarts the safety interval`() {
        repeat(8) { batch.add("a", 500) }
        batch.commit()
        repeat(9) { batch.add("a", 500) }
        assertEquals(1, disk)
    }
}
