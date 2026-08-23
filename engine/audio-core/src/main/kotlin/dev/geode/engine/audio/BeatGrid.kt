package dev.geode.engine.audio

import kotlin.math.abs

class BeatGrid {
    var phase: Float = 0f
        private set

    var beat: Boolean = false
        private set

    var locked: Boolean = false
        private set

    fun step(
        periodFrames: Float,
        confidence: Float,
        onset: Boolean,
    ): Boolean {
        locked = confidence >= LOCK_CONFIDENCE && periodFrames > 0f

        if (periodFrames > 0f) {
            phase += 1f / periodFrames
            while (phase >= 1f) phase -= 1f
        }

        beat =
            when {
                !onset -> false
                !locked -> {
                    phase = 0f
                    true
                }
                else -> {
                    val error = if (phase > 0.5f) phase - 1f else phase
                    if (abs(error) <= ON_GRID_TOLERANCE) {
                        phase -= error * PHASE_CORRECTION
                        if (phase < 0f) phase += 1f
                        true
                    } else {
                        false
                    }
                }
            }
        return beat
    }

    fun reset() {
        phase = 0f
        beat = false
        locked = false
    }

    companion object {
        const val LOCK_CONFIDENCE: Float = 0.4f

        const val ON_GRID_TOLERANCE: Float = 0.12f

        const val PHASE_CORRECTION: Float = 0.25f
    }
}
