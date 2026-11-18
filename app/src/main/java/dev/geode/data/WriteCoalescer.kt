package dev.geode.data

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.coroutines.CoroutineContext
import kotlin.coroutines.EmptyCoroutineContext

/**
 * Turns a burst of "this changed" calls (a slider drag, a pinch) into one disk write: [write] runs
 * [quietMs] after the last [request], and never more than [maxWaitMs] after the first one of a
 * burst, so a gesture that never ends still saves now and then. [flush] writes at once when
 * something is pending, for a teardown. [write] reads the latest value itself.
 */
internal class WriteCoalescer(
    private val scope: CoroutineScope,
    private val quietMs: Long,
    private val maxWaitMs: Long,
    private val context: CoroutineContext = EmptyCoroutineContext,
    private val clock: () -> Long = { System.nanoTime() / NANOS_PER_MS },
    private val write: () -> Unit,
) {
    private val lock = Any()
    private var dirty = false
    private var burstStartMs = 0L
    private var pending: Job? = null

    fun request() {
        synchronized(lock) {
            val now = clock()
            if (!dirty) burstStartMs = now
            dirty = true
            pending?.cancel()
            val wait = minOf(quietMs, burstStartMs + maxWaitMs - now).coerceAtLeast(0L)
            pending =
                scope.launch(context) {
                    delay(wait)
                    flush()
                }
        }
    }

    /** Writes now if a request is waiting; no-op otherwise. */
    fun flush() {
        synchronized(lock) {
            if (!dirty) return
            dirty = false
            pending = null
        }
        write()
    }

    private companion object {
        const val NANOS_PER_MS = 1_000_000L
    }
}
