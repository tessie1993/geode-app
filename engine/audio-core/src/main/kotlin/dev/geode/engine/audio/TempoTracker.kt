package dev.geode.engine.audio

import java.util.Arrays
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToInt

class TempoTracker(
    private val hopRateHz: Float,
    private val minBpm: Float = 60f,
    private val maxBpm: Float = 200f,
    resonatorCount: Int = 64,
    private val preferredBpm: Float = 120f,
    private val preferenceOctaves: Float = 1.1f,
    halfLifeSeconds: Float = 4f,
    energyAverageSeconds: Float = 2f,
) {
    init {
        require(hopRateHz > 0f) { "hopRateHz must be positive, was $hopRateHz" }
        require(minBpm > 0f && maxBpm > minBpm) { "need 0 < minBpm < maxBpm, got $minBpm..$maxBpm" }
        require(resonatorCount > 1) { "resonatorCount must exceed 1, was $resonatorCount" }
    }

    private val periods: IntArray = buildPeriods(resonatorCount)

    private val delayLines: Array<FloatArray> = Array(periods.size) { FloatArray(periods[it]) }
    private val delayIndex = IntArray(periods.size)

    private val feedback =
        FloatArray(periods.size) { i ->
            0.5f.pow(periods[i] / (halfLifeSeconds * hopRateHz))
        }

    private val preference =
        FloatArray(periods.size) { i ->
            val octaves = ln(bpmOf(periods[i]) / preferredBpm) / LN_2
            exp(-0.5 * (octaves / preferenceOctaves) * (octaves / preferenceOctaves)).toFloat()
        }

    private val energy = FloatArray(periods.size)
    private val scores = FloatArray(periods.size)
    private val scoreScratch = FloatArray(periods.size)
    private val energyPole = 1f - exp(-1f / (energyAverageSeconds * hopRateHz))

    var periodFrames: Float = 0f
        private set

    var bpm: Float = 0f
        private set

    var confidence: Float = 0f
        private set

    fun step(onset: Float) {
        var best = -1
        var bestScore = 0f
        for (i in periods.indices) {
            val line = delayLines[i]
            val at = delayIndex[i]
            val a = feedback[i]
            val y = a * line[at] + (1f - a) * onset
            line[at] = y
            delayIndex[i] = if (at + 1 == line.size) 0 else at + 1

            energy[i] += (y * y - energy[i]) * energyPole
            val score = energy[i] * preference[i]
            scores[i] = score
            if (score > bestScore) {
                bestScore = score
                best = i
            }
        }

        if (best < 0 || bestScore <= 0f) {
            periodFrames = 0f
            bpm = 0f
            confidence = 0f
            return
        }

        periodFrames = periods[best].toFloat()
        bpm = bpmOf(periods[best])
        confidence = clarity(bestScore)
    }

    fun reset() {
        for (line in delayLines) line.fill(0f)
        delayIndex.fill(0)
        energy.fill(0f)
        scores.fill(0f)
        periodFrames = 0f
        bpm = 0f
        confidence = 0f
    }

    private fun clarity(bestScore: Float): Float {
        System.arraycopy(scores, 0, scoreScratch, 0, scores.size)
        Arrays.sort(scoreScratch)
        val median = scoreScratch[scoreScratch.size / 2]
        return ((bestScore - median) / bestScore).coerceIn(0f, 1f)
    }

    private fun bpmOf(periodFrames: Int): Float = 60f * hopRateHz / periodFrames

    private fun buildPeriods(count: Int): IntArray {
        val logLow = ln(minBpm.toDouble())
        val logHigh = ln(maxBpm.toDouble())
        val seen = sortedSetOf<Int>()
        for (i in 0 until count) {
            val candidateBpm = exp(logLow + (logHigh - logLow) * i / (count - 1))
            val period = (60.0 * hopRateHz / candidateBpm).roundToInt()
            if (period >= 2) seen.add(period)
        }
        require(seen.size > 1) {
            "hopRateHz $hopRateHz is too low to resolve $minBpm..$maxBpm BPM"
        }
        return seen.toIntArray()
    }

    companion object {
        private val LN_2 = ln(2.0).toFloat()
    }
}
