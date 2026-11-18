package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class ListenBatchTest {
    private val written = mutableListOf<Pair<String, Long>>()
    private val batch = ListenBatch(intervalMs = 1_000L) { uri, ms -> written += uri to ms }

    @Test
    fun `half-second ticks reach the writer once a second has built up`() {
        batch.add("a", 500)
        assertEquals(emptyList<Pair<String, Long>>(), written)
        batch.add("a", 500)
        assertEquals(listOf("a" to 1_000L), written)
    }

    @Test
    fun `a track change writes what the old track gathered`() {
        batch.add("a", 400)
        batch.add("b", 300)
        assertEquals(listOf("a" to 400L), written)
        batch.drain()
        assertEquals(listOf("a" to 400L, "b" to 300L), written)
    }

    @Test
    fun `drain with nothing gathered writes nothing, and writes a remainder once`() {
        batch.drain()
        batch.add("a", 200)
        batch.drain()
        batch.drain()
        assertEquals(listOf("a" to 200L), written)
    }
}
