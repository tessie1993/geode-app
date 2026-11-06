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
        /** The shortest part worth rendering; the rule is native (`core/output/ExportRange`). */
        val minDurationMs: Long get() = GeodeNative.exportRangeMinMs()

        /** A part of the track, clamped natively; null renders the whole track. */
        fun of(
            startMs: Long,
            endMs: Long,
            trackDurationMs: Long,
        ): ExportRange? {
            val out = LongArray(2)
            return if (GeodeNative.exportRangeClamp(startMs, endMs, trackDurationMs, out)) ExportRange(out[0], out[1]) else null
        }
    }
}
