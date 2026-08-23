package dev.geode.analysis

object BarTrim {
    const val BEATS_PER_BAR = 4

    const val MIN_BPM = 50f
    const val MAX_BPM = 220f

    fun barDurationUs(bpm: Float): Long? {
        if (!bpm.isFinite() || bpm < MIN_BPM || bpm > MAX_BPM) return null
        return (60_000_000.0 * BEATS_PER_BAR / bpm).toLong()
    }

    fun trimToBars(
        durationUs: Long,
        bpm: Float,
    ): Long {
        if (durationUs <= 0L) return durationUs
        val bar = barDurationUs(bpm) ?: return durationUs
        val bars = durationUs / bar
        if (bars < 1) return durationUs
        return bars * bar
    }

    fun trimmedAwayUs(
        durationUs: Long,
        bpm: Float,
    ): Long = durationUs - trimToBars(durationUs, bpm)
}
