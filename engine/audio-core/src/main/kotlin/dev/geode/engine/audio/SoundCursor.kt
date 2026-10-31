package dev.geode.engine.audio

/**
 * Reads what a [SampleRing] gained since the last frame, interleaved, for the sound engine's write side.
 *
 * The old analysis took a snapshot of the newest window each hop and never needed to know what it had
 * seen. The sound engine runs a filter bank over every sample as it is written (TouchDesigner's half), so
 * it must get each sample exactly once: a skipped stretch reads as a hole in the drums, a repeated one as
 * a double hit. The cursor remembers the last frame it handed out and only moves forward.
 *
 * A new epoch (track change, seek, source switch) restarts it; [take] then reports [Take.restarted] so the
 * caller resets the engine before writing. After a stall longer than the ring holds, it skips to the
 * oldest frame still safe to read rather than reading slots the writer is overwriting.
 *
 * Pure Kotlin, one reader thread; the ring's writer runs concurrently, which is why [take] reads
 * [SampleRing.writtenFrames] once and copies up to that.
 */
class SoundCursor(
    val ring: SampleRing,
    /** The most frames one [take] hands out, so a long stall costs one bounded catch-up, not seconds of DSP. */
    private val maxFrames: Int = ring.maxWriteFrames,
) {
    private var read: Long = -1
    private var epoch: Int = -1
    private val planar = Array(ring.channelCount) { FloatArray(maxFrames) }

    /** Interleaved output of the last [take], [Take.frames] × [channels] long. */
    val interleaved = FloatArray(maxFrames * ring.channelCount)

    val channels: Int get() = ring.channelCount

    data class Take(
        val frames: Int,
        val restarted: Boolean,
    )

    fun take(): Take {
        val written = ring.writtenFrames
        val ringEpoch = ring.epoch
        var restarted = false
        if (ringEpoch != epoch || read > written) {
            epoch = ringEpoch
            read = maxOf(ring.oldestAvailable, written - maxFrames)
            restarted = true
        }
        if (read < ring.oldestAvailable) read = ring.oldestAvailable
        // A stall longer than one take: keep the newest audio, which is what is playing now.
        if (written - read > maxFrames) read = written - maxFrames
        val count = (written - read).toInt()
        if (count <= 0) return Take(0, restarted)
        ring.copyInto(read, count, planar)
        // A mono source leaves the ring's second channel silent; hand the engine the mono twice so it
        // reads centred and narrow instead of hard left.
        val mono = ring.sourceChannelCount == 1
        val ch = ring.channelCount
        var o = 0
        for (i in 0 until count) {
            for (c in 0 until ch) interleaved[o++] = planar[if (mono) 0 else c][i]
        }
        read += count
        return Take(count, restarted)
    }

    /** Forget the position: the next [take] starts from the newest audio and reports a restart. */
    fun reset() {
        epoch = -1
    }
}
