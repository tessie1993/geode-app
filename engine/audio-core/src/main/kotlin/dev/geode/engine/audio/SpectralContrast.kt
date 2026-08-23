package dev.geode.engine.audio

import java.util.Arrays
import kotlin.math.log10
import kotlin.math.max

class SpectralContrast(
    fftSize: Int,
    sampleRateHz: Int,
    val bands: Int = 6,
    fminHz: Float = 200f,
    private val alpha: Float = 0.02f,
) {
    init {
        require(bands > 0) { "bands must be positive, was $bands" }
        require(alpha > 0f && alpha < 1f) { "alpha must be in (0,1), was $alpha" }
    }

    private val firstBin = IntArray(bands)
    private val binCount = IntArray(bands)
    private val scratch: DoubleArray

    init {
        val binHz = sampleRateHz.toDouble() / fftSize
        val binsTotal = fftSize / 2 + 1
        var widest = 0
        for (b in 0 until bands) {
            val lo = fminHz * (1 shl b)
            val hi = fminHz * (1 shl (b + 1))
            var start = -1
            var count = 0
            for (k in 0 until binsTotal) {
                val f = k * binHz
                if (f >= lo && f < hi) {
                    if (start < 0) start = k
                    count++
                }
            }
            require(count > 0) {
                "contrast band $b (${lo.toInt()}..${hi.toInt()} Hz) holds no bin at fftSize $fftSize, $sampleRateHz Hz"
            }
            firstBin[b] = start
            binCount[b] = count
            if (count > widest) widest = count
        }
        scratch = DoubleArray(widest)
    }

    fun compute(
        magnitudes: FloatArray,
        out: FloatArray,
    ) {
        require(out.size == bands) { "expected $bands bands, got ${out.size}" }
        for (b in 0 until bands) {
            val n = binCount[b]
            val base = firstBin[b]
            for (i in 0 until n) {
                val mag = magnitudes[base + i].toDouble()
                scratch[i] = mag * mag
            }
            Arrays.sort(scratch, 0, n)
            val k = max(1, (alpha * n).toInt())
            var valley = 0.0
            var peak = 0.0
            for (i in 0 until k) {
                valley += scratch[i]
                peak += scratch[n - 1 - i]
            }
            valley = max(valley / k, Mfcc.LOG_POWER_FLOOR)
            peak = max(peak / k, Mfcc.LOG_POWER_FLOOR)
            out[b] = (10.0 * (log10(peak) - log10(valley))).toFloat()
        }
    }
}
