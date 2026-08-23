package dev.geode.engine.audio

class RingReader(
    private val ring: SampleRing,
) {
    var nextSample: Long = 0
        private set

    private var cursorEpoch: Int = ring.epoch

    fun rewindToEpoch() {
        cursorEpoch = ring.epoch
        nextSample = ring.oldestAvailable
    }

    fun read(out: Array<FloatArray>): RingReadResult {
        val epoch = ring.epoch
        if (epoch != cursorEpoch) return RingReadResult.Discontinuity(cursorEpoch, epoch)

        val oldest = ring.oldestAvailable
        if (nextSample < oldest) return RingReadResult.Gap(nextSample, oldest)

        val available = ring.writtenFrames - nextSample
        if (available <= 0L) return RingReadResult.NotYetAvailable

        require(out.size == ring.channelCount) {
            "out has ${out.size} channels, ring has ${ring.channelCount}"
        }
        val count = minOf(available, out.minOf { it.size }.toLong()).toInt()
        val first = nextSample
        ring.copyInto(first, count, out)

        val oldestNow = ring.oldestAvailable
        if (oldestNow > first) return RingReadResult.Gap(first, oldestNow)

        nextSample = first + count
        return RingReadResult.Ok(first, count, epoch)
    }

    fun skipToOldest() {
        nextSample = ring.oldestAvailable
    }
}
