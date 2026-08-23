package dev.geode.engine.audio

import kotlin.math.sqrt

class KeyDetector {
    private val chroma = DoubleArray(12)
    private val frame = DoubleArray(12)
    private var frames = 0L

    fun accumulate(
        magnitudes: FloatArray,
        sampleRateHz: Int,
        fftSize: Int,
    ) {
        Chromagram.foldPeaks(magnitudes, sampleRateHz, fftSize, 60f, 5000f, frame)
        for (i in 0 until 12) chroma[i] += frame[i]
        frames++
    }

    fun finish(): String {
        if (frames == 0L || chroma.all { it == 0.0 }) return ""
        var bestScore = Double.NEGATIVE_INFINITY
        var bestPc = 0
        var bestMinor = false
        for (minor in booleanArrayOf(false, true)) {
            val profile = if (minor) MINOR else MAJOR
            for (root in 0 until 12) {
                var score = 0.0
                for (i in 0 until 12) score += chroma[(root + i) % 12] * profile[i]
                score /= norm(chroma) * norm(profile)
                if (score > bestScore) {
                    bestScore = score
                    bestPc = root
                    bestMinor = minor
                }
            }
        }
        return NAMES[bestPc] + if (bestMinor) " minor" else " major"
    }

    private fun norm(v: DoubleArray): Double = sqrt(v.sumOf { it * it }).coerceAtLeast(1e-9)

    companion object {
        private val NAMES = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

        private val MAJOR = doubleArrayOf(6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88)
        private val MINOR = doubleArrayOf(6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17)

        fun compact(key: String): String =
            when {
                key.isBlank() -> ""
                key.endsWith(" minor") -> key.removeSuffix(" minor") + "m"
                else -> key.removeSuffix(" major")
            }
    }
}
