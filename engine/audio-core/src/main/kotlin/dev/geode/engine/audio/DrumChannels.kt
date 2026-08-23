package dev.geode.engine.audio

class DrumChannels(
    private val bandCount: Int,
    hopRateHz: Float,
    sampleRateHz: Int,
    minHz: Float = LogBands.DEFAULT_MIN_HZ,
    maxHz: Float = LogBands.DEFAULT_MAX_HZ,
) {
    private val pickers = Array(CHANNELS) { OnsetPeakPicker(hopRateHz, refractorySeconds = 0.05f) }
    private val fluxes = Array(CHANNELS) { SuperFlux(bandCount) }
    private val slices = Array(CHANNELS) { FloatArray(bandCount) }
    private val from = IntArray(CHANNELS)
    private val to = IntArray(CHANNELS)

    var kick: Float = 0f
        private set

    var snare: Float = 0f
        private set

    var hat: Float = 0f
        private set

    var sensitivity: Float
        get() = pickers[0].sensitivity
        set(value) {
            for (p in pickers) p.sensitivity = value
        }

    init {
        for (c in 0 until CHANNELS) {
            from[c] = LogBands.bandForHz(EDGES[c * 2], bandCount, sampleRateHz, minHz, maxHz)
            to[c] = LogBands.bandForHz(EDGES[c * 2 + 1], bandCount, sampleRateHz, minHz, maxHz)
        }
    }

    fun step(bands: FloatArray) {
        require(bands.size == bandCount) { "expected $bandCount bands, got ${bands.size}" }
        for (c in 0 until CHANNELS) {
            val slice = slices[c]
            for (b in 0 until bandCount) slice[b] = if (b >= from[c] && b <= to[c]) bands[b] else 0f
            val fired = pickers[c].accept(fluxes[c].next(slice))
            val impulse = if (fired) pickers[c].strength else 0f
            when (c) {
                0 -> kick = impulse
                1 -> snare = impulse
                else -> hat = impulse
            }
        }
    }

    fun reset() {
        for (p in pickers) p.reset()
        for (f in fluxes) f.reset()
        kick = 0f
        snare = 0f
        hat = 0f
    }

    private companion object {
        const val CHANNELS = 3

        val EDGES = floatArrayOf(30f, 120f, 120f, 900f, 4_000f, 16_000f)
    }
}
