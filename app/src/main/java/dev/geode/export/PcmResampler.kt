package dev.geode.export

import dev.geode.engine.bridge.GeodeNative

/**
 * Streaming rate converter for interleaved 16-bit PCM on its way into the AAC encoder.
 *
 * The maths is native (`core/audio/dsp/PcmResampler`, tested by `core/tests/resampler_test.cpp`):
 * a box filter down (exact for the 2x and 4x steps hi-res sources take), linear interpolation up.
 * This class only owns the native handle, so [close] it when the transcode ends. At equal rates it
 * never allocates one and [process] is a copy.
 *
 * State carries across [process] calls, so a decoder's buffers can be fed as they arrive.
 */
internal class PcmResampler(
    inRateHz: Int,
    val outRateHz: Int,
    private val channels: Int,
) : AutoCloseable {
    init {
        require(inRateHz > 0 && outRateHz > 0) { "rates must be positive, were $inRateHz -> $outRateHz" }
        require(channels > 0) { "channels must be positive, was $channels" }
    }

    val isPassthrough: Boolean = inRateHz == outRateHz

    private var handle: Long =
        if (isPassthrough) {
            0L
        } else {
            GeodeNative.resamplerCreate(inRateHz, outRateHz, channels).also {
                check(it != 0L) { "native resampler refused $inRateHz -> $outRateHz Hz, $channels channels" }
            }
        }

    /** Converts [frames] interleaved frames of [input]; returns the interleaved output. */
    fun process(
        input: ShortArray,
        frames: Int,
    ): ShortArray {
        if (isPassthrough) return input.copyOf(frames * channels)
        check(handle != 0L) { "PcmResampler used after close()" }
        return GeodeNative.resamplerProcess(handle, input, frames)
    }

    /** Idempotent. */
    override fun close() {
        val owned = handle
        handle = 0L
        if (owned != 0L) GeodeNative.resamplerDestroy(owned)
    }

    companion object {
        /**
         * A mix's clips are all brought to this, so they can share one AAC track. Which rate a lone
         * source gets, and how long its bytes last, is native (`GeodeNative.aacRateFor`,
         * `pcmS16BytesToUs`; core/audio/dsp/PcmTiming, tested by core/tests/clip_media_test.cpp).
         */
        const val MIX_RATE_HZ = 48_000
    }
}
