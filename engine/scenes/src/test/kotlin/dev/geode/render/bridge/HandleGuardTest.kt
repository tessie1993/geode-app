package dev.geode.render.bridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/** #853: a destroy never frees the native renderer under a call in flight on another thread. */
class HandleGuardTest {
    @Test
    fun beforeCreateAndAfterDestroyACallReturnsItsDefault() {
        val guard = HandleGuard()
        assertEquals(-1, guard.use(-1) { 1 })
        assertTrue(guard.create({ 7L }) {})
        assertEquals(7L, guard.use(0L) { it })
        guard.destroy {}
        assertEquals(-1, guard.use(-1) { 1 })
    }

    @Test
    fun createRunsInitOncePerHandleAndFailureStaysZero() {
        val guard = HandleGuard()
        assertFalse(guard.create({ 0L }) { error("no init for a failed create") })
        val inits = AtomicInteger()
        assertTrue(guard.create({ 5L }) { inits.incrementAndGet() })
        assertTrue(guard.create({ 6L }) { inits.incrementAndGet() })
        assertEquals(1, inits.get())
        assertEquals(5L, guard.use(0L) { it })
    }

    @Test
    fun initMayUseTheHandleItIsGiven() {
        val guard = HandleGuard()
        var seen = 0L
        guard.create({ 9L }) { seen = guard.use(0L) { h -> h } }
        assertEquals(9L, seen)
    }

    @Test
    fun destroyWaitsForACallInFlight() {
        val guard = HandleGuard()
        guard.create({ 3L }) {}
        val inside = CountDownLatch(1)
        val release = CountDownLatch(1)
        val freed = AtomicInteger()
        val pool = Executors.newFixedThreadPool(2)
        try {
            pool.submit {
                guard.use(Unit) {
                    inside.countDown()
                    release.await(5, TimeUnit.SECONDS)
                    assertEquals("freed under a live call", 0, freed.get())
                }
            }
            assertTrue(inside.await(5, TimeUnit.SECONDS))
            val destroyed = pool.submit { guard.destroy { freed.incrementAndGet() } }
            Thread.sleep(100)
            assertEquals(0, freed.get())
            release.countDown()
            destroyed.get(5, TimeUnit.SECONDS)
            assertEquals(1, freed.get())
        } finally {
            pool.shutdownNow()
        }
    }

    @Test
    fun concurrentDestroyNeverLetsACallSeeADeadHandle() {
        val guard = HandleGuard()
        val live = ConcurrentHashSetOfHandles()
        val bad = AtomicInteger()
        val pool = Executors.newFixedThreadPool(4)
        try {
            val stop = java.util.concurrent.atomic.AtomicBoolean(false)
            val users =
                List(3) {
                    pool.submit {
                        while (!stop.get()) guard.use(Unit) { h -> if (!live.contains(h)) bad.incrementAndGet() }
                    }
                }
            var next = 1L
            repeat(2000) {
                val h = next++
                live.add(h)
                guard.create({ h }) {}
                guard.destroy { dead -> live.remove(dead) }
            }
            stop.set(true)
            users.forEach { it.get(5, TimeUnit.SECONDS) }
            assertEquals(0, bad.get())
        } finally {
            pool.shutdownNow()
        }
    }

    private class ConcurrentHashSetOfHandles {
        private val set = java.util.concurrent.ConcurrentHashMap.newKeySet<Long>()

        fun add(h: Long) = set.add(h)

        fun remove(h: Long) = set.remove(h)

        fun contains(h: Long) = set.contains(h)
    }
}
