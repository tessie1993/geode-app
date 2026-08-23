package dev.geode.engine.audio

import java.util.Arrays
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

class OnsetPeakPicker(
    private val hopRateHz: Float,
    windowSeconds: Float = 1.5f,
    @Volatile var sensitivity: Float = 3f,
    @Volatile var refractorySeconds: Float = 0.06f,
    localMaxSeconds: Float = 0.03f,
) {
    init {
        require(hopRateHz > 0f) { "hopRateHz must be positive, was $hopRateHz" }
        require(windowSeconds > 0f) { "windowSeconds must be positive, was $windowSeconds" }
    }

    private val windowSize = (hopRateHz * windowSeconds).roundToInt().coerceAtLeast(3)
    private val window = FloatArray(windowSize)
    private val scratch = FloatArray(windowSize)
    private var writeIndex = 0
    private var filled = 0

    private val localMaxSize = (hopRateHz * localMaxSeconds).roundToInt().coerceAtLeast(1)
    private val recent = FloatArray(localMaxSize)
    private var recentIndex = 0

    private var framesSinceOnset = Int.MAX_VALUE / 2

    var threshold: Float = 0f
        private set

    var strength: Float = 0f
        private set

    private var peakEnvelope = 0f
    private val peakDecayPerFrame = exp(-1f / (hopRateHz * PEAK_MEMORY_SECONDS))

    fun accept(onset: Float): Boolean {
        val precedingMax = precedingMax()

        window[writeIndex] = onset
        writeIndex = (writeIndex + 1) % windowSize
        if (filled < windowSize) filled++

        val median = median()
        val spread = max(max(deviation(median), median * SPREAD_FLOOR_FRACTION), MIN_SPREAD)
        threshold = median + sensitivity * spread

        val refractoryFrames = (hopRateHz * refractorySeconds).roundToInt().coerceAtLeast(1)
        val isOnset =
            onset > threshold &&
                onset > NUMERIC_FLOOR &&
                onset > precedingMax &&
                framesSinceOnset > refractoryFrames

        val decayedPeak = peakEnvelope * peakDecayPerFrame
        strength = if (isOnset) (onset / max(decayedPeak, NUMERIC_FLOOR)).coerceIn(0f, 1f) else 0f
        peakEnvelope = max(onset, decayedPeak)

        framesSinceOnset = if (isOnset) 0 else min(framesSinceOnset + 1, 1_000_000)

        recent[recentIndex] = onset
        recentIndex = (recentIndex + 1) % localMaxSize
        return isOnset
    }

    fun reset() {
        window.fill(0f)
        recent.fill(0f)
        writeIndex = 0
        filled = 0
        recentIndex = 0
        framesSinceOnset = Int.MAX_VALUE / 2
        threshold = 0f
        strength = 0f
        peakEnvelope = 0f
    }

    private fun precedingMax(): Float {
        var peak = 0f
        for (i in 0 until localMaxSize) if (recent[i] > peak) peak = recent[i]
        return peak
    }

    private fun median(): Float {
        System.arraycopy(window, 0, scratch, 0, filled)
        Arrays.sort(scratch, 0, filled)
        val mid = filled / 2
        return if (filled % 2 == 1) scratch[mid] else (scratch[mid - 1] + scratch[mid]) * 0.5f
    }

    private fun deviation(median: Float): Float {
        var acc = 0f
        for (i in 0 until filled) acc += abs(window[i] - median)
        return acc / filled
    }

    companion object {
        const val SPREAD_FLOOR_FRACTION: Float = 0.05f

        const val MIN_SPREAD: Float = 1e-6f

        const val NUMERIC_FLOOR: Float = 1e-6f

        const val PEAK_MEMORY_SECONDS: Float = 8f
    }
}
