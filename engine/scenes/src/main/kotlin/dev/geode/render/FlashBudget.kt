package dev.geode.render

class FlashBudget(
    private val maxPerSecond: Float = VisualSafety.WCAG_FLASHES_PER_SECOND,
) {
    private val edges = FloatArray(CAPACITY)
    private var head = 0
    private var count = 0
    private var above = false
    private var lastTime = Float.NEGATIVE_INFINITY

    fun reset() {
        head = 0
        count = 0
        above = false
        lastTime = Float.NEGATIVE_INFINITY
    }

    fun gainFor(
        timeSeconds: Float,
        impulse: Float,
    ): Float {
        if (timeSeconds < lastTime) reset()
        lastTime = timeSeconds

        val risky = impulse > RISK_THRESHOLD
        val rising = risky && !above
        above = risky
        dropOlderThan(timeSeconds - WINDOW_SECONDS)
        return when {
            !rising -> 1f
            count < maxPerSecond -> 1f.also { record(timeSeconds) }
            else -> (RISK_THRESHOLD * SUPPRESSED_SCALE / impulse).coerceIn(MIN_GAIN, 1f)
        }
    }

    private fun dropOlderThan(cutoff: Float) {
        while (count > 0 && edges[(head - count + CAPACITY) % CAPACITY] <= cutoff) count--
    }

    private fun record(timeSeconds: Float) {
        edges[head] = timeSeconds
        head = (head + 1) % CAPACITY
        if (count < CAPACITY) count++
    }

    companion object {
        const val RISK_THRESHOLD = 0.08f

        private const val SUPPRESSED_SCALE = 0.6f

        private const val MIN_GAIN = 0.05f

        private const val WINDOW_SECONDS = 1f

        private const val CAPACITY = 16
    }
}
