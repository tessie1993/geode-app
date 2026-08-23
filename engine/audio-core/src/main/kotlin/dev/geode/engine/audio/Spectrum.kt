package dev.geode.engine.audio

import org.jtransforms.fft.FloatFFT_1D
import kotlin.math.sqrt

class Spectrum(
    val fftSize: Int,
) {
    init {
        require(fftSize >= 2 && fftSize and (fftSize - 1) == 0) {
            "fftSize must be a power of two of at least 2, was $fftSize"
        }
    }

    private val fft = FloatFFT_1D(fftSize.toLong())
    private val work = FloatArray(fftSize)

    val magnitudes = FloatArray(fftSize / 2 + 1)

    fun binHz(sampleRateHz: Int): Double = sampleRateHz.toDouble() / fftSize

    fun compute(windowed: FloatArray) {
        require(windowed.size >= fftSize) { "need $fftSize samples, got ${windowed.size}" }
        windowed.copyInto(work, 0, 0, fftSize)
        fft.realForward(work)
        magnitudes[0] = kotlin.math.abs(work[0])
        magnitudes[fftSize / 2] = kotlin.math.abs(work[1])
        for (k in 1 until fftSize / 2) {
            val re = work[2 * k]
            val im = work[2 * k + 1]
            magnitudes[k] = sqrt(re * re + im * im)
        }
    }

    fun peakBin(): Int {
        var best = 1
        for (k in 2 until magnitudes.size) if (magnitudes[k] > magnitudes[best]) best = k
        return best
    }
}
