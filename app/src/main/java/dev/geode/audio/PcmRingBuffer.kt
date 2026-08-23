package dev.geode.audio

class PcmRingBuffer(
    capacity: Int = 1 shl 16,
) : dev.geode.engine.audio.PcmSink {
    override fun write(
        interleaved: FloatArray,
        frameCount: Int,
        sourceChannelCount: Int,
    ) = writeInterleaved(interleaved, frameCount, sourceChannelCount)

    private val data: FloatArray = FloatArray(capacity)

    private val sideData: FloatArray = FloatArray(capacity)
    private val mask: Long = (capacity - 1).toLong()

    @Volatile
    private var writeIndex: Long = 0

    init {
        require(capacity and (capacity - 1) == 0) { "capacity must be a power of two" }
    }

    fun writeInterleaved(
        samples: FloatArray,
        frameCount: Int,
        channelCount: Int,
    ) {
        require(channelCount > 0) { "channelCount must be positive, was $channelCount" }
        require(frameCount * channelCount <= samples.size) {
            "$frameCount frames x $channelCount channels exceeds buffer of ${samples.size}"
        }
        var w = writeIndex
        var s = 0
        val stereo = channelCount >= 2
        repeat(frameCount) {
            var acc = 0f
            val base = s
            repeat(channelCount) {
                acc += samples[s]
                s++
            }
            val slot = (w and mask).toInt()
            data[slot] = acc / channelCount
            sideData[slot] = if (stereo) (samples[base] - samples[base + 1]) * 0.5f else 0f
            w++
        }
        writeIndex = w
    }

    fun currentWriteIndex(): Long = writeIndex

    var lastCopyEndIndex: Long = 0L
        private set

    fun copyNewSince(
        fromIndex: Long,
        out: FloatArray,
    ): Int {
        val w = writeIndex
        lastCopyEndIndex = w
        var available = w - fromIndex
        if (available <= 0L) return 0
        if (available > out.size) available = out.size.toLong()
        val maxRun = data.size - (data.size shr SNAPSHOT_HEADROOM_SHIFT)
        if (available > maxRun) available = maxRun.toLong()
        val start = w - available
        for (i in 0 until available.toInt()) {
            out[i] = data[((start + i) and mask).toInt()]
        }
        return available.toInt()
    }

    fun snapshotLatest(out: FloatArray): Boolean = snapshotFrom(data, out)

    fun snapshotLatestSide(out: FloatArray): Boolean = snapshotFrom(sideData, out)

    private fun snapshotFrom(
        src: FloatArray,
        out: FloatArray,
    ): Boolean {
        if (out.size > src.size) return false
        val w = writeIndex
        if (w < out.size) return false
        var r = w - out.size
        for (i in out.indices) {
            out[i] = src[(r and mask).toInt()]
            r++
        }
        return true
    }

    private companion object {
        const val SNAPSHOT_HEADROOM_SHIFT = 2
    }
}
