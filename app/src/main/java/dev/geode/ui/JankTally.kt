package dev.geode.ui

import android.os.Trace
import androidx.metrics.performance.FrameData
import androidx.metrics.performance.JankStats
import java.util.concurrent.atomic.AtomicLong

internal class JankTally : JankStats.OnFrameListener {
    private val frames = AtomicLong()
    private val janky = AtomicLong()
    private val worstNanos = AtomicLong()

    override fun onFrame(volatileFrameData: FrameData) {
        frames.incrementAndGet()
        if (!volatileFrameData.isJank) return
        worstNanos.accumulateAndGet(volatileFrameData.frameDurationUiNanos) { a, b -> maxOf(a, b) }
        Trace.setCounter(COUNTER, janky.incrementAndGet())
    }

    fun summary(): String = summaryOf(frames.get(), janky.get(), worstNanos.get())

    companion object {
        const val COUNTER = "geode.jankyFrames"

        fun summaryOf(
            frames: Long,
            janky: Long,
            worstNanos: Long,
        ): String = "janky frames $janky of $frames, worst ${worstNanos / NANOS_PER_MS} ms"

        private const val NANOS_PER_MS = 1_000_000L
    }
}
