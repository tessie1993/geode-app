package dev.geode.engine.audio

import dev.geode.engine.bridge.GeodeNative
import java.lang.ref.Cleaner
import java.lang.ref.Reference
import java.nio.ByteBuffer

/**
 * A handle on a native PCM ring (`core/audio/ingest/PcmRing`): what one audio source writes and every
 * surface's sound engine reads. The samples, the int16 and 8-bit conversions, the rate each block was
 * written at and the readers' cursors are all native; this only passes blocks and values across.
 *
 * Lifetime: the native ring lives as long as this object is reachable and is freed once it is collected,
 * so a writer thread or a renderer that holds the ring can never reach a freed one, and nobody has to
 * close it at the right moment. Native readers (a renderer's feed, a [SoundCursor]) share the ring and
 * keep it alive on their own. One writer at a time; [beginEpoch] and the getters from any thread.
 */
class SampleRing(
    capacityFrames: Int,
    val channelCount: Int,
    val maxWriteFrames: Int = capacityFrames / DEFAULT_RUNWAY_DIVISOR,
) {
    /** The native handle, for calls that take a ring; only valid while this object is held. */
    val handle: Long = GeodeNative.pcmRingCreate(capacityFrames, channelCount, maxWriteFrames)

    init {
        require(handle != 0L) { "no ring of $capacityFrames frames x $channelCount, runway $maxWriteFrames" }
        val owned = handle
        cleaner.register(this) { GeodeNative.pcmRingDestroy(owned) }
    }

    /** Frames written since the last [beginEpoch]. */
    val writtenFrames: Long get() = held { GeodeNative.pcmRingWritten(handle) }

    /** A new stream (track change, seek, source switch): every reader restarts at its next take. */
    fun beginEpoch() = held { GeodeNative.pcmRingBeginEpoch(handle) }

    /** Writes [frames] interleaved frames recorded at [sampleRateHz]; returns the block's peak magnitude. */
    fun write(
        samples: FloatArray,
        frames: Int,
        sourceChannels: Int,
        sampleRateHz: Int,
    ): Float = held { GeodeNative.pcmRingWriteFloats(handle, samples, frames, sourceChannels, sampleRateHz) }

    fun write(
        samples: ShortArray,
        frames: Int,
        sourceChannels: Int,
        sampleRateHz: Int,
    ): Float = held { GeodeNative.pcmRingWriteShorts(handle, samples, frames, sourceChannels, sampleRateHz) }

    /** Writes the first [count] bytes of [samples] in an `AudioFormat.ENCODING_PCM_*` [encoding]; returns the frames. */
    fun write(
        samples: ByteArray,
        count: Int,
        encoding: Int,
        sourceChannels: Int,
        sampleRateHz: Int,
    ): Int = held { GeodeNative.pcmRingWriteBytes(handle, samples, count, encoding, sourceChannels, sampleRateHz) }

    /** Writes what a direct [buffer] holds between its position and limit, leaving both alone; returns the frames. */
    fun write(
        buffer: ByteBuffer,
        encoding: Int,
        sourceChannels: Int,
        sampleRateHz: Int,
    ): Int =
        held {
            val bytes = buffer.remaining()
            GeodeNative.pcmRingWriteDirect(handle, buffer, buffer.position(), bytes, encoding, sourceChannels, sampleRateHz)
        }

    /** The rest-when-silent rule over the newest [frames] frames: [nowMs] when any is audible, else [heardAtMs]. */
    fun heardAt(
        frames: Int,
        nowMs: Long,
        heardAtMs: Long,
    ): Long = held { GeodeNative.pcmRingHeardAt(handle, frames, nowMs, heardAtMs) }

    /** Runs a native call with this object held past it, so the collector cannot free the ring under the call. */
    private inline fun <T> held(call: () -> T): T =
        try {
            call()
        } finally {
            Reference.reachabilityFence(this)
        }

    companion object {
        const val DEFAULT_RUNWAY_DIVISOR = 4

        /** Frees a ring once its holder is collected; one daemon thread for every ring. */
        private val cleaner: Cleaner = Cleaner.create()
    }
}
