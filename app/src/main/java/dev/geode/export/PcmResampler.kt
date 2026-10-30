package dev.geode.export

/**
 * Streaming rate converter for interleaved 16-bit PCM on its way into the AAC encoder.
 *
 * Downsampling averages every input frame that falls inside an output frame (a box filter:
 * exact for the 2x and 4x steps hi-res sources take, and enough of a low-pass that 96 kHz
 * content does not alias audibly). Upsampling interpolates linearly. Neither is a mastering
 * resampler; both are far better than the alternatives they replace, an encoder that refuses
 * to configure or a soundtrack that plays at the wrong speed.
 *
 * State carries across [process] calls, so a decoder's buffers can be fed as they arrive.
 */
internal class PcmResampler(
    private val inRateHz: Int,
    val outRateHz: Int,
    private val channels: Int,
) {
    init {
        require(inRateHz > 0 && outRateHz > 0) { "rates must be positive, were $inRateHz -> $outRateHz" }
        require(channels > 0) { "channels must be positive, was $channels" }
    }

    val isPassthrough: Boolean get() = inRateHz == outRateHz

    private var inFrames = 0L
    private var outFrames = 0L

    // Downsampling: running sums for the output frame being built.
    private val sums = LongArray(channels)
    private var summed = 0

    // Upsampling: the last input frame of the previous call, so output frames that fall between
    // two calls' buffers can still interpolate.
    private val last = ShortArray(channels)

    /** Converts [frames] interleaved frames of [input]; returns the interleaved output. */
    fun process(
        input: ShortArray,
        frames: Int,
    ): ShortArray =
        when {
            isPassthrough -> input.copyOf(frames * channels)
            outRateHz < inRateHz -> down(input, frames)
            else -> up(input, frames)
        }

    private fun down(
        input: ShortArray,
        frames: Int,
    ): ShortArray {
        val out = ShortArray((frames.toLong() * outRateHz / inRateHz + 2).toInt() * channels)
        var written = 0
        for (f in 0 until frames) {
            for (c in 0 until channels) sums[c] += input[f * channels + c]
            summed++
            inFrames++
            // Output frame k covers input frames [k * in / out, (k + 1) * in / out).
            if (inFrames * outRateHz >= (outFrames + 1) * inRateHz) {
                for (c in 0 until channels) {
                    out[written++] = (sums[c] / summed).toInt().toShort()
                    sums[c] = 0L
                }
                summed = 0
                outFrames++
            }
        }
        return out.copyOf(written)
    }

    private fun up(
        input: ShortArray,
        frames: Int,
    ): ShortArray {
        if (frames == 0) return ShortArray(0)
        val first = inFrames - if (inFrames > 0) 1 else 0
        val end = inFrames + frames
        val out = ShortArray((frames.toLong() * outRateHz / inRateHz + 2).toInt() * channels)
        var written = 0
        while (true) {
            // Output frame k sits at input position k * in / out.
            val num = outFrames * inRateHz
            val i0 = num / outRateHz
            if (i0 + 1 >= end) break
            if (i0 < first) {
                outFrames++
                continue
            }
            val frac = (num % outRateHz).toFloat() / outRateHz
            for (c in 0 until channels) {
                val a = sampleAt(input, i0, c).toFloat()
                val b = sampleAt(input, i0 + 1, c).toFloat()
                out[written++] = (a + (b - a) * frac).toInt().toShort()
            }
            outFrames++
        }
        for (c in 0 until channels) last[c] = input[(frames - 1) * channels + c]
        inFrames = end
        return out.copyOf(written)
    }

    private fun sampleAt(
        input: ShortArray,
        absFrame: Long,
        channel: Int,
    ): Short = if (absFrame < inFrames) last[channel] else input[(absFrame - inFrames).toInt() * channels + channel]

    companion object {
        /** The highest rate every Android AAC-LC encoder accepts. */
        const val AAC_MAX_RATE_HZ = 48_000

        /** A mix's clips are all brought to this, so they can share one AAC track. */
        const val MIX_RATE_HZ = 48_000

        /**
         * The AAC rate for a source: its own rate up to [AAC_MAX_RATE_HZ]; above it, 44.1 kHz for
         * the 44.1k family (88.2, 176.4 kHz) and 48 kHz for the rest, so the step down is a whole
         * number and the box filter is exact.
         */
        fun aacRateFor(sourceRateHz: Int): Int =
            when {
                sourceRateHz <= AAC_MAX_RATE_HZ -> sourceRateHz
                sourceRateHz % CD_RATE_HZ == 0 -> CD_RATE_HZ
                else -> AAC_MAX_RATE_HZ
            }

        private const val CD_RATE_HZ = 44_100
    }
}
