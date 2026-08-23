package dev.geode.audio

import dev.geode.analysis.AudioFeatures

object AudioBus {
    private const val STALE_MS = 1_500L

    @Volatile
    private var latest: AudioFeatures? = null

    @Volatile
    private var latestAtMs: Long = 0L

    fun publish(features: AudioFeatures) {
        latest = features
        latestAtMs = android.os.SystemClock.elapsedRealtime()
    }

    fun features(): AudioFeatures? {
        val f = latest ?: return null
        return if (android.os.SystemClock.elapsedRealtime() - latestAtMs > STALE_MS) null else f
    }

    val isLive: Boolean get() = features() != null

    fun clear() {
        latest = null
        latestAtMs = 0L
    }

    private val consumers = java.util.concurrent.atomic.AtomicInteger(0)

    @Volatile
    var onInterestChanged: (() -> Unit)? = null

    val hasConsumers: Boolean get() = consumers.get() > 0

    fun addConsumer() {
        if (consumers.incrementAndGet() == 1) onInterestChanged?.invoke()
    }

    fun removeConsumer() {
        if (consumers.decrementAndGet() == 0) onInterestChanged?.invoke()
    }
}
