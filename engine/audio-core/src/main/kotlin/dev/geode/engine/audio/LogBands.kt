package dev.geode.engine.audio

import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

class LogBands(
    val bandCount: Int,
    val fftSize: Int,
    sampleRateHz: Int,
    private val minHz: Float = DEFAULT_MIN_HZ,
    private val maxHz: Float = DEFAULT_MAX_HZ,
    private val tiltDbPerOctave: Float = PINK_TILT_DB_PER_OCTAVE,
) {
    init {
        require(bandCount > 0) { "bandCount must be positive, was $bandCount" }
        require(fftSize >= 2 && fftSize and (fftSize - 1) == 0) {
            "fftSize must be a power of two of at least 2, was $fftSize"
        }
        require(minHz > 0f && maxHz > minHz) { "need 0 < minHz < maxHz, got $minHz..$maxHz" }
    }

    var sampleRateHz: Int = sampleRateHz
        set(value) {
            if (value != field) {
                field = value
                rebuild()
            }
        }

    private val binCount = fftSize / 2 + 1

    private val firstBin = IntArray(bandCount)
    private val lastBin = IntArray(bandCount)
    private val lowerEdgeHz = FloatArray(bandCount)
    private val upperEdgeHz = FloatArray(bandCount)

    private val tiltWeight = FloatArray(binCount)

    private val magnitudeScale = 2f / fftSize

    init {
        rebuild()
    }

    fun lowerHz(band: Int): Float = lowerEdgeHz[band]

    fun upperHz(band: Int): Float = upperEdgeHz[band]

    fun energyDb(
        magnitudes: FloatArray,
        out: FloatArray,
    ) {
        energy(magnitudes, out)
        for (b in 0 until bandCount) {
            val mean = out[b]
            out[b] = if (mean <= 0f) AdaptiveRange.SILENCE_DB else max(10f * log10(mean), AdaptiveRange.SILENCE_DB)
        }
    }

    fun energy(
        magnitudes: FloatArray,
        out: FloatArray,
    ) {
        require(magnitudes.size == binCount) { "expected $binCount bins, got ${magnitudes.size}" }
        require(out.size == bandCount) { "expected $bandCount bands, got ${out.size}" }
        for (b in 0 until bandCount) {
            var power = 0.0
            val from = firstBin[b]
            val to = lastBin[b]
            for (k in from..to) {
                val m = magnitudes[k] * magnitudeScale
                power += m.toDouble() * m * tiltWeight[k]
            }
            out[b] = (power / (to - from + 1)).toFloat()
        }
    }

    private fun rebuild() {
        val nyquist = sampleRateHz / 2f
        val top = min(maxHz, nyquist)
        val bottom = min(minHz, top * 0.5f)
        val binHz = sampleRateHz.toFloat() / fftSize

        val logBottom = ln(bottom.toDouble())
        val logTop = ln(top.toDouble())
        var cursor = 1
        for (b in 0 until bandCount) {
            val lo = exp(logBottom + (logTop - logBottom) * b / bandCount).toFloat()
            val hi = exp(logBottom + (logTop - logBottom) * (b + 1) / bandCount).toFloat()
            lowerEdgeHz[b] = lo
            upperEdgeHz[b] = hi

            val wantFirst = max(cursor, (lo / binHz).toInt())
            val first = min(wantFirst, binCount - 1)
            val last = min(max(first, (hi / binHz).toInt()), binCount - 1)
            firstBin[b] = first
            lastBin[b] = last
            cursor = min(last + 1, binCount - 1)
        }

        val exponent = tiltDbPerOctave / PINK_TILT_DB_PER_OCTAVE
        for (k in 0 until binCount) {
            val hz = k * binHz
            tiltWeight[k] =
                if (exponent == 0f || hz <= 0f) 1f else (hz / TILT_REFERENCE_HZ).toDouble().pow(exponent.toDouble()).toFloat()
        }
    }

    companion object {
        const val DEFAULT_MIN_HZ: Float = 30f

        const val DEFAULT_MAX_HZ: Float = 16_000f

        const val PINK_TILT_DB_PER_OCTAVE: Float = 3.0103f

        const val TILT_REFERENCE_HZ: Float = 1_000f

        fun bandForHz(
            hz: Float,
            bandCount: Int,
            sampleRateHz: Int,
            minHz: Float = DEFAULT_MIN_HZ,
            maxHz: Float = DEFAULT_MAX_HZ,
        ): Int {
            val top = min(maxHz, sampleRateHz / 2f)
            val bottom = min(minHz, top * 0.5f)
            if (hz <= bottom) return 0
            if (hz >= top) return bandCount - 1
            val fraction = ln(hz / bottom) / ln(top / bottom)
            return (fraction * bandCount).toInt().coerceIn(0, bandCount - 1)
        }
    }
}
