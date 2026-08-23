package dev.geode.engine.audio

class FrameGrid(
    val branch: AnalysisBranch,
) {
    private val half = branch.windowFrames / 2

    fun centerSample(index: Long): Long = index * branch.hopFrames

    fun firstSample(index: Long): Long = centerSample(index) - half

    fun endSample(index: Long): Long = firstSample(index) + branch.windowFrames

    fun frameAtOrBefore(sample: Long): Long = Math.floorDiv(sample, branch.hopFrames.toLong())

    fun hasFrameCenteredAt(sample: Long): Boolean = sample >= 0 && sample % branch.hopFrames == 0L

    val firstCompleteFrame: Long
        get() = (half + branch.hopFrames - 1L) / branch.hopFrames

    fun latestCompleteFrame(writtenFrames: Long): Long? {
        val candidate = Math.floorDiv(writtenFrames - half, branch.hopFrames.toLong())
        return if (candidate >= firstCompleteFrame) candidate else null
    }

    fun centerMicros(
        index: Long,
        sampleRateHz: Int,
    ): Long {
        require(sampleRateHz > 0) { "sampleRateHz must be positive" }
        return centerSample(index) * MICROS_PER_SECOND / sampleRateHz
    }

    private companion object {
        const val MICROS_PER_SECOND = 1_000_000L
    }
}
