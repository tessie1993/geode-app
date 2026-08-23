package dev.geode.engine.audio

import kotlin.math.exp
import kotlin.math.max

class AdaptiveWhitening(
    val bandCount: Int,
    private val peakDecaySeconds: Float = 2f,
    private val floor: Float = DEFAULT_FLOOR,
) {
    init {
        require(bandCount > 0) { "bandCount must be positive, was $bandCount" }
        require(peakDecaySeconds > 0f) { "peakDecaySeconds must be positive, was $peakDecaySeconds" }
        require(floor > 0f) { "floor must be positive, was $floor" }
    }

    private val profile = FloatArray(bandCount)

    fun whiten(
        input: FloatArray,
        dtSeconds: Float,
        out: FloatArray,
    ) {
        require(input.size == bandCount) { "expected $bandCount bands, got ${input.size}" }
        require(out.size == bandCount) { "expected $bandCount outputs, got ${out.size}" }
        val decay = if (dtSeconds <= 0f) 1f else exp(-dtSeconds / peakDecaySeconds)
        for (b in 0 until bandCount) {
            val x = input[b]
            val peak = max(max(x, floor), profile[b] * decay)
            profile[b] = peak
            out[b] = (x / peak).coerceIn(0f, 1f)
        }
    }

    fun reset() {
        profile.fill(0f)
    }

    companion object {
        const val DEFAULT_FLOOR: Float = 1e-8f
    }
}
