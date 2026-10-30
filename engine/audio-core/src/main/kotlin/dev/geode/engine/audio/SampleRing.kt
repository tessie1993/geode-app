package dev.geode.engine.audio

class SampleRing(
    val capacityFrames: Int,
    val channelCount: Int,
    val maxWriteFrames: Int = capacityFrames / DEFAULT_RUNWAY_DIVISOR,
) : PcmSink {
    init {
        require(capacityFrames > 0 && capacityFrames and (capacityFrames - 1) == 0) {
            "capacityFrames must be a power of two, was $capacityFrames"
        }
        require(channelCount > 0) { "channelCount must be positive, was $channelCount" }
        require(maxWriteFrames in 1..capacityFrames) {
            "maxWriteFrames must be between 1 and $capacityFrames, was $maxWriteFrames"
        }
    }

    private val channels: Array<FloatArray> = Array(channelCount) { FloatArray(capacityFrames) }
    private val mask: Long = (capacityFrames - 1).toLong()

    @Volatile
    private var written: Long = 0

    @Volatile
    private var epochValue: Int = 0

    val writtenFrames: Long get() = written

    val epoch: Int get() = epochValue

    @Volatile
    private var sourceChannels: Int = 0

    val sourceChannelCount: Int get() = sourceChannels

    val oldestAvailable: Long get() = maxOf(0L, written + maxWriteFrames - capacityFrames)

    fun beginEpoch() {
        written = 0
        epochValue += 1
    }

    override fun write(
        interleaved: FloatArray,
        frameCount: Int,
        sourceChannelCount: Int,
    ) {
        require(sourceChannelCount > 0) { "sourceChannelCount must be positive" }
        require(frameCount.toLong() * sourceChannelCount <= interleaved.size) {
            "$frameCount frames x $sourceChannelCount channels exceeds ${interleaved.size}"
        }
        require(frameCount <= maxWriteFrames) {
            "$frameCount frames exceeds maxWriteFrames of $maxWriteFrames"
        }
        sourceChannels = sourceChannelCount
        var w = written
        var read = 0
        repeat(frameCount) {
            val slot = (w and mask).toInt()
            for (c in 0 until channelCount) {
                channels[c][slot] = if (c < sourceChannelCount) interleaved[read + c] else 0f
            }
            read += sourceChannelCount
            w++
        }
        written = w
    }

    /**
     * Copies [count] frames from [firstFrame] into [out], interleaved as [outChannels] channels
     * (the ring's first [outChannels]). False if any of them is not in the ring: not written yet,
     * or already overwritten.
     */
    fun readInterleaved(
        firstFrame: Long,
        count: Int,
        outChannels: Int,
        out: FloatArray,
    ): Boolean {
        require(outChannels in 1..channelCount) { "outChannels must be in 1..$channelCount, was $outChannels" }
        require(count.toLong() * outChannels <= out.size) { "$count frames x $outChannels channels exceeds ${out.size}" }
        if (firstFrame < oldestAvailable || firstFrame + count > written) return false
        for (c in 0 until outChannels) {
            val src = channels[c]
            var r = firstFrame
            var w = c
            repeat(count) {
                out[w] = src[(r and mask).toInt()]
                r++
                w += outChannels
            }
        }
        return true
    }

    companion object {
        const val DEFAULT_RUNWAY_DIVISOR = 4
    }
}
