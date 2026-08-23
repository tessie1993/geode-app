package dev.geode.engine.audio

class SpectralFlux(
    binCount: Int,
) {
    init {
        require(binCount > 0) { "binCount must be positive, was $binCount" }
    }

    private val previous = FloatArray(binCount)
    private var primed = false

    fun next(magnitudes: FloatArray): Double {
        require(magnitudes.size == previous.size) {
            "expected ${previous.size} bins, got ${magnitudes.size}"
        }
        var rise = 0.0
        if (primed) {
            for (k in magnitudes.indices) {
                val delta = magnitudes[k] - previous[k]
                if (delta > 0f) rise += delta.toDouble()
            }
        }
        magnitudes.copyInto(previous)
        primed = true
        return rise / previous.size
    }

    fun reset() {
        primed = false
        previous.fill(0f)
    }
}
