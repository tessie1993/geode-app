package dev.geode.engine.audio

/**
 * Analysis FFT size for a given stream rate.
 *
 * The bands, the bass/mid/treble split, chroma and key are all defined in Hz, but a fixed
 * 2048-point FFT gives 23 Hz bins at 48 kHz and 94 Hz bins at 192 kHz, and the log-band map
 * gives every band at least one bin, so the low bands slid upward as the rate rose (the kick
 * band read 94-1406 Hz at 192 kHz). Scaling the FFT with the rate keeps the bin width, and so
 * every band edge, where it is at 48 kHz.
 *
 * The hop is the native session's (a quarter of the window, see `AnalysisSession.hpp`), read
 * back through [ReactiveAnalyzer.hopSamples]; nothing here picks a frame rate.
 */
object AnalysisFft {
    /** The FFT the bands and every tuning constant were authored against. */
    const val REFERENCE_FFT_SIZE = 2048

    /** The rate [REFERENCE_FFT_SIZE] is calibrated for: 23.4 Hz bins. */
    const val REFERENCE_SAMPLE_RATE_HZ = 48_000

    /** 768 kHz worth; past this the input is not audio this app plays. */
    const val MAX_FFT_SIZE = 32_768

    /**
     * The smallest power of two whose bin width is no wider than the reference's: 2048 up to
     * 48 kHz, 4096 at 88.2/96 kHz, 8192 at 176.4/192 kHz. Lower rates keep 2048 (finer bins,
     * a longer window), which is what they always had.
     */
    fun sizeFor(sampleRateHz: Int): Int {
        var size = REFERENCE_FFT_SIZE
        while (size < MAX_FFT_SIZE && size.toLong() * REFERENCE_SAMPLE_RATE_HZ < REFERENCE_FFT_SIZE.toLong() * sampleRateHz) {
            size *= 2
        }
        return size
    }

    /** Milliseconds from the start of the stream to sample [sample] at [sampleRateHz]. */
    fun sampleToMs(
        sample: Long,
        sampleRateHz: Int,
    ): Long = sample * MS_PER_SECOND / sampleRateHz

    private const val MS_PER_SECOND = 1000L
}
