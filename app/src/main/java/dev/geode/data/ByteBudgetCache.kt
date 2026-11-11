package dev.geode.data

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import java.util.concurrent.ConcurrentHashMap

/**
 * A memory cache bounded by bytes, not by item count, that decodes each key once.
 *
 * Why: the library list used a 40-entry count cache, so one screen of rows plus a fling evicted
 * everything and every row decoded again, and a "no art" answer took a slot like a real image.
 * Here real values are charged by [sizeOf] against [maxBytes] and evicted least recently used,
 * while "decoded to nothing" answers live in their own bounded key set ([maxMisses]), so a long
 * list of artless tracks can't push covers out and is never decoded twice.
 *
 * Concurrent requests for one key share a single decode: the first caller decodes, the rest wait
 * for its result. The decode runs [NonCancellable] so a row scrolling off screen still leaves its
 * result in the cache for the next visit instead of throwing the work away.
 *
 * Pure Kotlin, no Android types, so the accounting is testable on the JVM.
 */
class ByteBudgetCache<K : Any, V : Any>(
    private val maxBytes: Long,
    private val maxMisses: Int,
    private val sizeOf: (V) -> Long,
) {
    private val lock = Any()
    private val values = LinkedHashMap<K, V>(16, 0.75f, true)
    private val misses =
        object : LinkedHashMap<K, Unit>(16, 0.75f, true) {
            override fun removeEldestEntry(eldest: MutableMap.MutableEntry<K, Unit>?): Boolean = size > maxMisses
        }
    private val inFlight = ConcurrentHashMap<K, CompletableDeferred<V?>>()
    private var bytes = 0L

    /** Bytes currently charged to real values. */
    val sizeBytes: Long get() = synchronized(lock) { bytes }

    /** Number of real values held. */
    val count: Int get() = synchronized(lock) { values.size }

    /** The cached value, or null when absent or known to have none. Never decodes. */
    fun peek(key: K): V? = synchronized(lock) { values[key] }

    /** True when [key] is already known to decode to nothing. */
    fun isKnownMissing(key: K): Boolean = synchronized(lock) { misses.containsKey(key) }

    /** Cached value, else the single shared [decode] of [key]. */
    suspend fun get(
        key: K,
        decode: suspend (K) -> V?,
    ): V? {
        synchronized(lock) {
            values[key]?.let { return it }
            if (misses.containsKey(key)) return null
        }
        val mine = CompletableDeferred<V?>()
        val owner = inFlight.putIfAbsent(key, mine)
        if (owner != null) return owner.await()
        return withContext(NonCancellable) {
            try {
                val result = decode(key)
                store(key, result)
                mine.complete(result)
                result
            } catch (e: Throwable) {
                mine.completeExceptionally(e)
                throw e
            } finally {
                inFlight.remove(key)
            }
        }
    }

    private fun store(
        key: K,
        value: V?,
    ) = synchronized(lock) {
        if (value == null) {
            misses[key] = Unit
            return@synchronized
        }
        val cost = sizeOf(value)
        if (cost > maxBytes) return@synchronized
        values.put(key, value)?.let { bytes -= sizeOf(it) }
        bytes += cost
        val it = values.entries.iterator()
        while (bytes > maxBytes && it.hasNext()) {
            val eldest = it.next()
            if (eldest.key == key) continue
            bytes -= sizeOf(eldest.value)
            it.remove()
        }
    }
}
