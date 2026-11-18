package dev.geode.data

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.currentTime
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** [WriteCoalescer]: a burst of requests is one write, a long burst still writes now and then, a flush writes at once. */
@OptIn(ExperimentalCoroutinesApi::class)
class WriteCoalescerTest {
    private fun TestScope.coalescer(writes: MutableList<Long>) =
        WriteCoalescer(this, quietMs = 400L, maxWaitMs = 2_000L, clock = { currentTime }) { writes += currentTime }

    @Test
    fun aBurstIsOneWriteAfterTheQuiet() =
        runTest {
            val writes = mutableListOf<Long>()
            val c = coalescer(writes)
            repeat(10) {
                c.request()
                advanceTimeBy(100L)
            }
            assertEquals(emptyList<Long>(), writes)
            advanceTimeBy(400L)
            assertEquals(listOf(1_300L), writes)
        }

    @Test
    fun aDragThatNeverPausesStillWritesEveryMaxWait() =
        runTest {
            val writes = mutableListOf<Long>()
            val c = coalescer(writes)
            repeat(50) {
                c.request()
                advanceTimeBy(100L)
            }
            advanceTimeBy(500L)
            assertEquals(2_000L, writes.first())
            assertTrue("writes: $writes", writes.size >= 2)
            assertTrue("writes: $writes", writes.zipWithNext().all { (a, b) -> b - a <= 2_100L })
        }

    @Test
    fun flushWritesAtOnceAndOnlyWhenSomethingIsPending() =
        runTest {
            val writes = mutableListOf<Long>()
            val c = coalescer(writes)
            c.flush()
            assertEquals(emptyList<Long>(), writes)
            c.request()
            advanceTimeBy(50L)
            c.flush()
            assertEquals(listOf(50L), writes)
            advanceTimeBy(1_000L)
            assertEquals(listOf(50L), writes)
        }
}
