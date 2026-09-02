package dev.geode.engine.audio

import dev.geode.engine.bridge.GeodeNative

object PulseReplay {
    class Result(
        val beat: BooleanArray,
        val strength: FloatArray,
        val transient: FloatArray,
        val phase: FloatArray,
        val confidence: FloatArray,
        val energy: FloatArray,
    )

    private const val STRIDE = 6

    fun decide(
        flux: FloatArray,
        rms: FloatArray,
        hopRateHz: Float,
        sensitivity: Float,
        refractoryMs: Float,
    ): Result {
        val n = flux.size
        val packed = FloatArray(n * STRIDE)
        GeodeNative.pulseReplay(flux, rms, hopRateHz, sensitivity, refractoryMs, packed)
        return Result(
            beat = BooleanArray(n) { packed[it * STRIDE] > 0f },
            strength = FloatArray(n) { packed[it * STRIDE + 1] },
            transient = FloatArray(n) { packed[it * STRIDE + 2] },
            phase = FloatArray(n) { packed[it * STRIDE + 3] },
            confidence = FloatArray(n) { packed[it * STRIDE + 4] },
            energy = FloatArray(n) { packed[it * STRIDE + 5] },
        )
    }
}
