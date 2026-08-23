package dev.geode.export

data class ExportRange(
    val startMs: Long,
    val durationMs: Long,
) {
    init {
        require(startMs >= 0) { "startMs must not be negative, was $startMs" }
        require(durationMs > 0) { "durationMs must be positive, was $durationMs" }
    }

    val endMs: Long get() = startMs + durationMs

    companion object {
        const val MIN_DURATION_MS: Long = 1_000

        @Suppress("ReturnCount")
        fun of(
            startMs: Long,
            endMs: Long,
            trackDurationMs: Long,
        ): ExportRange? {
            if (trackDurationMs <= 0) return null
            val start = startMs.coerceIn(0, trackDurationMs)
            val end = endMs.coerceIn(0, trackDurationMs)
            if (end - start < MIN_DURATION_MS) return null
            if (start == 0L && end >= trackDurationMs) return null
            return ExportRange(start, end - start)
        }
    }
}
