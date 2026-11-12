package dev.geode.export

import dev.geode.engine.bridge.GeodeNative

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
        /** The shortest segment worth rendering (core/output/ExportRange). */
        val MIN_DURATION_MS: Long get() = GeodeNative.exportRangeMinMs()

        /** The picked span held inside the track, or null when native finds no segment to render in it. */
        fun of(
            startMs: Long,
            endMs: Long,
            trackDurationMs: Long,
        ): ExportRange? {
            val span = LongArray(2)
            if (!GeodeNative.exportRange(startMs, endMs, trackDurationMs, span)) return null
            return ExportRange(span[0], span[1])
        }
    }
}
