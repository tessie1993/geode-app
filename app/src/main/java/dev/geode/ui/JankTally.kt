package dev.geode.ui

import android.os.Trace
import androidx.metrics.performance.FrameData
import androidx.metrics.performance.JankStats
import dev.geode.stage.Stage
import java.util.concurrent.atomic.AtomicLong

internal object LiveFrameBudget {
    @Volatile
    var stage: Stage? = null

    fun ns(): Long = stage?.frameBudgetNs ?: 0L
}

internal class JankTally(
    private val budgetNs: () -> Long,
    private val periodNs: () -> Long,
) : JankStats.OnFrameListener {
    @Volatile
    var stats: JankStats? = null

    private val frames = AtomicLong()
    private val janky = AtomicLong()
    private val worstNanos = AtomicLong()
    private var seenBudget = 0L
    private var seenPeriod = 0L

    override fun onFrame(volatileFrameData: FrameData) {
        val budget = budgetNs()
        val period = periodNs()
        if (budget != seenBudget || period != seenPeriod) {
            seenBudget = budget
            seenPeriod = period
            stats?.jankHeuristicMultiplier = jankMultiplier(budget, period)
        }
        frames.incrementAndGet()
        if (!volatileFrameData.isJank) return
        worstNanos.accumulateAndGet(volatileFrameData.frameDurationUiNanos) { a, b -> maxOf(a, b) }
        Trace.setCounter(COUNTER, janky.incrementAndGet())
    }

    fun summary(): String = summaryOf(frames.get(), janky.get(), worstNanos.get())

    companion object {
        const val COUNTER = "geode.jankyFrames"

        const val DEFAULT_MULTIPLIER = 2f

        fun jankMultiplier(
            budgetNs: Long,
            periodNs: Long,
        ): Float = if (budgetNs <= 0L || periodNs <= 0L) DEFAULT_MULTIPLIER else DEFAULT_MULTIPLIER * budgetNs / periodNs

        fun summaryOf(
            frames: Long,
            janky: Long,
            worstNanos: Long,
        ): String = "janky frames $janky of $frames, worst ${worstNanos / NANOS_PER_MS} ms"

        private const val NANOS_PER_MS = 1_000_000L
    }
}
