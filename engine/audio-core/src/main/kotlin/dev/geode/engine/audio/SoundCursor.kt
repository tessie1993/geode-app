package dev.geode.engine.audio

import dev.geode.engine.bridge.GeodeNative
import java.lang.ref.Reference

/**
 * A Kotlin reader's place in a [SampleRing] (the native `Cursor` of `core/audio/ingest/PcmRing`): each
 * frame once, in order, with the rate it was written at. Only for a reader that needs the samples on the
 * Kotlin side (Cast's encoder feed); a renderer takes from the ring natively and needs none.
 *
 * One reader thread, which also closes it. The native cursor shares the ring, so it stays readable after
 * the [SampleRing] is gone.
 */
class SoundCursor(
    ring: SampleRing,
) : AutoCloseable {
    private var handle = GeodeNative.pcmCursorCreate(ring.handle, 0)
    val channels: Int = ring.channelCount
    private val info = IntArray(2)

    init {
        // The ring's handle was only valid while the ring was held: hold it past the create call.
        Reference.reachabilityFence(ring)
    }

    /** Interleaved 16-bit output of the last [take], its frames times [channels] long. */
    val pcm = ShortArray(GeodeNative.pcmCursorMaxFrames(handle) * channels)

    /** The rate the last [take]'s frames were written at. */
    val sampleRateHz: Int get() = info[1]

    /** What the ring gained since the last call, into [pcm]; returns the frames. */
    fun take(): Int = if (handle == 0L) 0 else GeodeNative.pcmCursorTakeS16(handle, pcm, info)

    override fun close() {
        if (handle != 0L) GeodeNative.pcmCursorDestroy(handle)
        handle = 0L
    }
}
