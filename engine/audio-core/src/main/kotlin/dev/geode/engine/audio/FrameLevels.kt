package dev.geode.engine.audio

import kotlin.math.abs
import kotlin.math.sqrt

object FrameLevels {
    fun rms(
        frame: FloatArray,
        count: Int = frame.size,
    ): Double {
        if (count <= 0) return 0.0
        var sum = 0.0
        for (i in 0 until count) {
            val x = frame[i].toDouble()
            sum += x * x
        }
        return sqrt(sum / count)
    }

    fun peak(
        frame: FloatArray,
        count: Int = frame.size,
    ): Float {
        var top = 0f
        for (i in 0 until count) {
            val magnitude = abs(frame[i])
            if (magnitude > top) top = magnitude
        }
        return top
    }

    fun zeroCrossingRate(
        frame: FloatArray,
        count: Int = frame.size,
    ): Double {
        if (count < 2) return 0.0
        var crossings = 0
        var previousNegative = frame[0] < 0f
        for (i in 1 until count) {
            val negative = frame[i] < 0f
            if (negative != previousNegative) crossings++
            previousNegative = negative
        }
        return crossings.toDouble() / (count - 1)
    }
}
