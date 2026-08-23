package dev.geode.engine.audio

import kotlin.math.max
import kotlin.math.min

class SuperFlux(
    val bandCount: Int,
    maxFilterBands: Int = 3,
    private val lagFrames: Int = 1,
) {
    init {
        require(bandCount > 0) { "bandCount must be positive, was $bandCount" }
        require(maxFilterBands >= 1 && maxFilterBands % 2 == 1) {
            "maxFilterBands must be odd and at least 1, was $maxFilterBands"
        }
        require(lagFrames >= 1) { "lagFrames must be at least 1, was $lagFrames" }
    }

    private val radius = (maxFilterBands - 1) / 2

    private val history = Array(lagFrames) { FloatArray(bandCount) }
    private val filtered = FloatArray(bandCount)
    private var cursor = 0
    private var filled = 0

    fun next(bands: FloatArray): Float {
        require(bands.size == bandCount) { "expected $bandCount bands, got ${bands.size}" }

        for (k in 0 until bandCount) {
            var peak = bands[k]
            val from = max(0, k - radius)
            val to = min(bandCount - 1, k + radius)
            for (j in from..to) if (bands[j] > peak) peak = bands[j]
            filtered[k] = peak
        }

        var rise = 0f
        if (filled >= lagFrames) {
            val earlier = history[cursor]
            for (k in 0 until bandCount) {
                val delta = bands[k] - earlier[k]
                if (delta > 0f) rise += delta
            }
        }

        filtered.copyInto(history[cursor])
        cursor = (cursor + 1) % lagFrames
        if (filled < lagFrames) filled++
        return rise / bandCount
    }

    fun reset() {
        for (frame in history) frame.fill(0f)
        cursor = 0
        filled = 0
    }
}
