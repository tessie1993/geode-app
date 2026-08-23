package dev.geode.engine.audio

import kotlin.math.ceil
import kotlin.math.exp
import kotlin.math.log2
import kotlin.math.roundToInt

class Chromagram(
    private val hopRateHz: Float = 60f,
    attackSeconds: Float = ATTACK_SECONDS,
    releaseSeconds: Float = RELEASE_SECONDS,
) {
    val bins: FloatArray = FloatArray(12)

    var confidence: Float = 0f
        private set

    var dominantPitchClass: Int = 0
        private set

    private val raw = DoubleArray(12)
    private val scratch = FloatArray(12)

    private val attack = poleFor(attackSeconds)
    private val release = poleFor(releaseSeconds)

    private fun poleFor(seconds: Float): Float = if (seconds <= 0f) 1f else 1f - exp(-1f / (seconds * hopRateHz).coerceAtLeast(1e-3f))

    fun reset() {
        java.util.Arrays.fill(bins, 0f)
        java.util.Arrays.fill(raw, 0.0)
        confidence = 0f
        dominantPitchClass = 0
    }

    fun step(
        magnitudes: FloatArray,
        sampleRateHz: Int,
        fftSize: Int,
    ) {
        val total = foldPeaks(magnitudes, sampleRateHz, fftSize, MIN_HZ, MAX_HZ, raw)

        if (total <= SILENCE) {
            for (i in 0 until 12) bins[i] += (0f - bins[i]) * release
            confidence = 0f
            return
        }

        var peak = 0.0
        for (i in 0 until 12) peak = maxOf(peak, raw[i])
        for (i in 0 until 12) {
            val target = (raw[i] / peak).toFloat()
            val k = if (target > bins[i]) attack else release
            bins[i] += (target - bins[i]) * k
        }

        var best = 0
        for (i in 1 until 12) if (bins[i] > bins[best]) best = i
        dominantPitchClass = best
        confidence = peakToMedian()
    }

    private fun peakToMedian(): Float {
        System.arraycopy(bins, 0, scratch, 0, 12)
        java.util.Arrays.sort(scratch)
        val median = (scratch[5] + scratch[6]) * 0.5f
        val peak = scratch[11]
        if (peak <= 1e-6f) return 0f
        if (median <= 1e-6f) return 1f
        val ratio = peak / median
        return ((ratio - 1f) / (CONFIDENT_RATIO - 1f)).coerceIn(0f, 1f)
    }

    fun top(
        n: Int,
        out: IntArray,
    ): Int {
        val count = n.coerceIn(0, 12)
        System.arraycopy(bins, 0, scratch, 0, 12)
        for (slot in 0 until count) {
            var best = 0
            for (i in 1 until 12) if (scratch[i] > scratch[best]) best = i
            out[slot] = best
            scratch[best] = -1f
        }
        return count
    }

    companion object {
        val NAMES = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

        fun foldPeaks(
            magnitudes: FloatArray,
            sampleRateHz: Int,
            fftSize: Int,
            minHz: Float,
            maxHz: Float,
            out: DoubleArray,
        ): Double {
            java.util.Arrays.fill(out, 0.0)
            val minBin = ceil(minHz * fftSize / sampleRateHz).toInt().coerceAtLeast(1)
            val maxBin = (maxHz * fftSize / sampleRateHz).toInt().coerceAtMost(magnitudes.size - 2)
            var frameMax = 0f
            for (k in minBin..maxBin) if (magnitudes[k] > frameMax) frameMax = magnitudes[k]
            if (frameMax <= 0f) return 0.0
            val floor = frameMax * PEAK_FLOOR
            var total = 0.0
            for (k in minBin..maxBin) {
                val mid = magnitudes[k]
                if (mid < floor || mid <= magnitudes[k - 1] || mid < magnitudes[k + 1]) continue
                val left = magnitudes[k - 1].toDouble()
                val right = magnitudes[k + 1].toDouble()
                val denominator = left - 2.0 * mid + right
                val offset =
                    if (denominator < -1e-12) {
                        (0.5 * (left - right) / denominator).coerceIn(-0.5, 0.5)
                    } else {
                        0.0
                    }
                val f = (k + offset) * sampleRateHz.toDouble() / fftSize
                val midi = 69.0 + 12.0 * log2(f / 440.0)
                val pc = ((midi.roundToInt() % 12) + 12) % 12
                out[pc] += mid.toDouble()
                total += mid.toDouble()
            }
            return total
        }

        const val PEAK_FLOOR = 0.05f

        const val MIN_HZ = 200f

        const val MAX_HZ = 5_000f

        const val ATTACK_SECONDS = 0.06f

        const val RELEASE_SECONDS = 0.35f

        const val CONFIDENT_RATIO = 6f

        const val SILENCE = 1e-7
    }
}
