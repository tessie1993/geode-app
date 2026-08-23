package dev.geode.engine.audio

import kotlin.math.exp
import kotlin.math.max

object PulseReplay {
    class Result(
        val beat: BooleanArray,
        val strength: FloatArray,
        val transient: FloatArray,
        val phase: FloatArray,
        val confidence: FloatArray,
        val energy: FloatArray,
    )

    fun decide(
        flux: FloatArray,
        rms: FloatArray,
        hopRateHz: Float,
        sensitivity: Float,
        refractoryMs: Float,
    ): Result {
        val picker = OnsetPeakPicker(hopRateHz, sensitivity = sensitivity, refractorySeconds = refractoryMs / 1000f)
        val tempo = TempoTracker(hopRateHz)
        val grid = BeatGrid()

        val beat = BooleanArray(flux.size)
        val strength = FloatArray(flux.size)
        val transient = FloatArray(flux.size)
        val phase = FloatArray(flux.size)
        val confidence = FloatArray(flux.size)
        val energy = FloatArray(flux.size)

        val decay = exp(-1f / (hopRateHz * MACRO_PEAK_SECONDS))
        var peak = 0f

        for (i in flux.indices) {
            val isOnset = picker.accept(flux[i])
            transient[i] = picker.strength
            tempo.step(flux[i])
            beat[i] = grid.step(tempo.periodFrames, tempo.confidence, isOnset)
            strength[i] = if (beat[i]) picker.strength else 0f
            phase[i] = grid.phase
            confidence[i] = tempo.confidence

            val level = if (i < rms.size) rms[i] else 0f
            peak = max(level, peak * decay)
            energy[i] = if (peak <= 1e-6f) 0f else (level / peak).coerceIn(0f, 1f)
        }
        return Result(beat, strength, transient, phase, confidence, energy)
    }

    private const val MACRO_PEAK_SECONDS = 20f
}
