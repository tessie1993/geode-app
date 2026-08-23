package dev.geode.wallpaper

import dev.geode.analysis.AudioFeatures
import kotlin.math.sin

class IdleFeatures(
    private val bandCount: Int = 64,
    private val waveformSize: Int = 128,
) {
    private var phase = 0f

    fun tick(dt: Float): AudioFeatures {
        phase += dt
        val t = phase
        val bands =
            FloatArray(bandCount) { i ->
                val rate = 0.07f + i * 0.0131f
                0.10f + 0.06f * sin(t * rate * TAU + i * 0.7f)
            }
        val waveform = FloatArray(waveformSize) { i -> 0.12f * sin(t * 0.9f + i * 0.19f) }
        return AudioFeatures(
            bands = bands,
            waveform = waveform,
            rms = 0.16f + 0.04f * sin(t * 0.11f * TAU),
            bass = 0.18f + 0.08f * sin(t * 0.09f * TAU),
            mid = 0.14f + 0.06f * sin(t * 0.13f * TAU + 1.7f),
            treble = 0.06f + 0.03f * sin(t * 0.17f * TAU + 3.1f),
            beat = false,
        )
    }

    private companion object {
        const val TAU = 6.2831855f
    }
}
