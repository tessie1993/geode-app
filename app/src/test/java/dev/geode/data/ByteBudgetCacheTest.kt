package dev.geode.data

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.atomic.AtomicInteger

class ByteBudgetCacheTest {
    private fun cache(
        maxBytes: Long = 100,
        maxMisses: Int = 3,
    ) = ByteBudgetCache<String, ByteArray>(maxBytes, maxMisses) { it.size.toLong() }

    @Test
    fun chargesBytesNotCount() =
        runTest {
            val c = cache()
            c.get("a") { ByteArray(40) }
            c.get("b") { ByteArray(40) }
            assertEquals(80L, c.sizeBytes)
            assertEquals(2, c.count)
        }

    @Test
    fun evictsLeastRecentlyUsedOverBudget() =
        runTest {
            val c = cache()
            c.get("a") { ByteArray(40) }
            c.get("b") { ByteArray(40) }
            c.get("a") { error("cached") }
            c.get("c") { ByteArray(40) }
            assertNotNull(c.peek("a"))
            assertNull(c.peek("b"))
            assertNotNull(c.peek("c"))
            assertEquals(80L, c.sizeBytes)
        }

    @Test
    fun oversizedValueIsReturnedButNotKept() =
        runTest {
            val c = cache()
            assertEquals(200, c.get("big") { ByteArray(200) }?.size)
            assertEquals(0L, c.sizeBytes)
            assertNull(c.peek("big"))
        }

    @Test
    fun missesAreRememberedAndNeverEvictValues() =
        runTest {
            val c = cache(maxMisses = 2)
            c.get("img") { ByteArray(50) }
            val calls = AtomicInteger()
            repeat(2) {
                c.get("none") {
                    calls.incrementAndGet()
                    null
                }
            }
            assertEquals(1, calls.get())
            assertTrue(c.isKnownMissing("none"))
            for (i in 0 until 10) c.get("m$i") { null }
            assertNotNull(c.peek("img"))
            assertFalse(c.isKnownMissing("none"))
        }

    @Test
    fun nullDecodeRunsOnceAcrossRepeatedRequests() =
        runTest {
            val c = cache()
            val calls = AtomicInteger()
            repeat(50) {
                assertNull(
                    c.get("noart") {
                        calls.incrementAndGet()
                        null
                    },
                )
            }
            assertEquals(1, calls.get())
            assertEquals(0L, c.sizeBytes)
        }

    @Test
    fun clearDropsValuesButRemembersMisses() =
        runTest {
            val c = cache()
            c.get("art") { ByteArray(40) }
            c.get("artless") { null }
            c.clear()
            assertNull(c.peek("art"))
            assertEquals(0L, c.sizeBytes)
            assertEquals(0, c.count)
            assertTrue(c.isKnownMissing("artless"))
            assertNull(c.get("artless") { error("a known miss is not decoded again") })
            assertNotNull(c.get("art") { ByteArray(40) })
            assertEquals(40L, c.sizeBytes)
        }

    @Test
    fun concurrentRequestsDecodeOnce() =
        runTest {
            val c = cache()
            val calls = AtomicInteger()
            val gate = CompletableDeferred<Unit>()
            val all =
                (1..8).map {
                    async {
                        c.get("a") {
                            calls.incrementAndGet()
                            gate.await()
                            ByteArray(10)
                        }
                    }
                }
            gate.complete(Unit)
            val results = all.awaitAll()
            assertEquals(1, calls.get())
            assertTrue(results.all { it != null && it.size == 10 })
        }
}
