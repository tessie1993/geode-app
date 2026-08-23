package dev.geode.engine.audio

import kotlin.math.exp
import kotlin.math.max

class AdaptiveRange(
    val bandCount: Int,
    private val floorRiseSeconds: Float = 6f,
    private val floorFallSeconds: Float = 0.5f,
    private val ceilingRiseSeconds: Float = 0.15f,
    private val ceilingFallSeconds: Float = 2.5f,
    private val minSpanDb: Float = 15f,
    private val warmupSeconds: Float = 1.5f,
) {
    init {
        require(bandCount > 0) { "bandCount must be positive, was $bandCount" }
        require(minSpanDb > 0f) { "minSpanDb must be positive, was $minSpanDb" }
        require(warmupSeconds > 0f) { "warmupSeconds must be positive, was $warmupSeconds" }
    }

    private val floorDb = FloatArray(bandCount)
    private val ceilingDb = FloatArray(bandCount)
    private var primed = false
    private var adaptedSeconds = 0f

    val warmup: Float get() = (adaptedSeconds / warmupSeconds).coerceIn(0f, 1f)

    fun normalize(
        inputDb: FloatArray,
        dtSeconds: Float,
        out: FloatArray,
    ) {
        require(inputDb.size == bandCount) { "expected $bandCount bands, got ${inputDb.size}" }
        require(out.size == bandCount) { "expected $bandCount outputs, got ${out.size}" }

        if (!primed) {
            for (b in 0 until bandCount) {
                val x = inputDb[b]
                floorDb[b] = x - minSpanDb * 0.5f
                ceilingDb[b] = x + minSpanDb * 0.5f
            }
            primed = true
        }

        var adapted = false
        for (b in 0 until bandCount) {
            val x = inputDb[b]
            if (x <= SILENCE_DB) {
                out[b] = 0f
                continue
            }
            adapted = true
            floorDb[b] = follow(floorDb[b], x, if (x > floorDb[b]) floorRiseSeconds else floorFallSeconds, dtSeconds)
            ceilingDb[b] =
                follow(ceilingDb[b], x, if (x > ceilingDb[b]) ceilingRiseSeconds else ceilingFallSeconds, dtSeconds)
            val span = max(ceilingDb[b] - floorDb[b], minSpanDb)
            out[b] = ((x - floorDb[b]) / span).coerceIn(0f, 1f)
        }
        if (adapted && dtSeconds > 0f) adaptedSeconds += dtSeconds
    }

    fun reset() {
        floorDb.fill(0f)
        ceilingDb.fill(0f)
        primed = false
        adaptedSeconds = 0f
    }

    private fun follow(
        current: Float,
        target: Float,
        tauSeconds: Float,
        dtSeconds: Float,
    ): Float {
        if (dtSeconds <= 0f) return current
        if (tauSeconds <= 0f) return target
        val k = (1f - exp(-dtSeconds / tauSeconds)).coerceIn(0f, 1f)
        return current + (target - current) * k
    }

    companion object {
        const val SILENCE_DB: Float = -120f
    }
}
