package dev.geode.analysis

import dev.geode.engine.audio.DrumChannels
import dev.geode.engine.audio.PulseReplay
import kotlin.math.sqrt

data class TimelineFrame(
    val timeMs: Long,
    val features: AudioFeatures,
)

class FeatureTimeline(
    val frames: List<TimelineFrame>,
    val hopMs: Long,
    val key: String = "",
    val hopRateHz: Float = 60f,
) {
    val durationMs: Long = frames.lastOrNull()?.timeMs ?: 0L
    val averageEnergy: Float = if (frames.isEmpty()) 0f else frames.map { it.features.rms }.average().toFloat()
    val averageCentroid: Float = if (frames.isEmpty()) 0f else frames.map { it.features.centroid }.average().toFloat()
    val bpm: Float = frames.lastOrNull()?.features?.bpm ?: 0f
    val beatDensity: Float =
        if (frames.isEmpty()) 0f else frames.count { it.features.beat } / (frames.size / 60f + 1e-6f)

    private val frameSpacingMs: Double =
        if (frames.size > 1) durationMs.toDouble() / (frames.size - 1) else hopMs.toDouble()

    fun withBeatSensitivity(
        beatSensitivity: Float,
        beatMinIntervalMs: Float,
    ): FeatureTimeline {
        if (frames.isEmpty()) return this
        val flux = FloatArray(frames.size) { frames[it].features.flux }
        if (flux.none { it > 0f }) return this
        val rms = FloatArray(frames.size) { frames[it].features.rms }
        val pulse = PulseReplay.decide(flux, rms, hopRateHz, beatSensitivity, beatMinIntervalMs)
        val out = ArrayList<TimelineFrame>(frames.size)
        var anyChanged = false
        for (i in frames.indices) {
            val fr = frames[i]
            val f = fr.features
            val unchanged =
                f.beat == pulse.beat[i] &&
                    f.beatStrength == pulse.strength[i] &&
                    f.transient == pulse.transient[i] &&
                    f.beatPhase == pulse.phase[i] &&
                    f.pulseConfidence == pulse.confidence[i] &&
                    f.macroEnergy == pulse.energy[i]
            out +=
                if (unchanged) {
                    fr
                } else {
                    anyChanged = true
                    fr.copy(
                        features =
                            f.copy(
                                beat = pulse.beat[i],
                                beatStrength = pulse.strength[i],
                                transient = pulse.transient[i],
                                beatPhase = pulse.phase[i],
                                pulseConfidence = pulse.confidence[i],
                                macroEnergy = pulse.energy[i],
                            ),
                    )
                }
        }
        if (!anyChanged) return this
        return FeatureTimeline(out, hopMs, key, hopRateHz)
    }

    fun withDrumChannels(sampleRateHz: Int = 48_000): FeatureTimeline {
        if (frames.isEmpty()) return this
        val bandCount = frames[0].features.bands.size
        if (bandCount == 0) return this
        val channels = DrumChannels(bandCount, hopRateHz, sampleRateHz)
        val out = ArrayList<TimelineFrame>(frames.size)
        var anyChanged = false
        for (fr in frames) {
            val f = fr.features
            if (f.bands.size != bandCount) {
                out += fr
                continue
            }
            channels.step(f.bands)
            if (channels.kick == f.kick && channels.snare == f.snare && channels.hat == f.hat) {
                out += fr
                continue
            }
            anyChanged = true
            out +=
                TimelineFrame(
                    fr.timeMs,
                    f.copy(kick = channels.kick, snare = channels.snare, hat = channels.hat),
                )
        }
        return if (anyChanged) FeatureTimeline(out, hopMs, key, hopRateHz) else this
    }

    private fun indexAt(timeMs: Long): Int =
        if (frameSpacingMs > 0.0) {
            Math.round(timeMs / frameSpacingMs).toInt().coerceIn(0, frames.size - 1)
        } else {
            0
        }

    fun featuresAt(
        timeMs: Long,
        spanMs: Long = 0L,
    ): AudioFeatures {
        if (frames.isEmpty()) return AudioFeatures.empty()
        val first = indexAt(timeMs)
        val f = frames[first].features
        if (spanMs <= 0L || frameSpacingMs <= 0.0) return f
        val last = (indexAt(timeMs + spanMs) - 1).coerceIn(first, frames.size - 1)
        if (last <= first) return f
        var beat = f.beat
        var onset = f.onset
        var flux = f.flux
        var strength = f.beatStrength
        var transient = f.transient
        for (i in first + 1..last) {
            val g = frames[i].features
            beat = beat || g.beat
            onset = maxOf(onset, g.onset)
            flux = maxOf(flux, g.flux)
            strength = maxOf(strength, g.beatStrength)
            transient = maxOf(transient, g.transient)
        }
        if (beat == f.beat && onset == f.onset && flux == f.flux && strength == f.beatStrength && transient == f.transient) return f
        return f.copy(beat = beat, onset = onset, flux = flux, beatStrength = strength, transient = transient)
    }

    fun progressionAt(
        timeMs: Long,
        sections: List<Long>,
        spanMs: Long = 0L,
    ): AudioFeatures {
        val f = featuresAt(timeMs, spanMs)
        if (durationMs <= 0L) return f
        var idx = 0
        for (s in sections) {
            if (s <= timeMs) idx++ else break
        }
        return f.copy(
            progress = (timeMs.toFloat() / durationMs).coerceIn(0f, 1f),
            sectionIndex = idx,
            sectionCount = sections.size + 1,
        )
    }

    fun detectSections(
        windowFrames: Int = 90,
        minGapFrames: Int = 300,
    ): List<Long> {
        if (frames.size < windowFrames * 2) return emptyList()
        val novelty = FloatArray(frames.size)
        val bandCount = frames[0].features.bands.size
        val before = FloatArray(bandCount)
        val after = FloatArray(bandCount)
        for (i in windowFrames until frames.size - windowFrames) {
            java.util.Arrays.fill(before, 0f)
            java.util.Arrays.fill(after, 0f)
            for (w in 0 until windowFrames) {
                val fb = frames[i - 1 - w].features.bands
                val fa = frames[i + w].features.bands
                for (b in 0 until bandCount) {
                    before[b] += fb[b]
                    after[b] += fa[b]
                }
            }
            var dist = 0f
            for (b in 0 until bandCount) {
                val d = (after[b] - before[b]) / windowFrames
                dist += d * d
            }
            novelty[i] = sqrt(dist)
        }
        val mean = novelty.average().toFloat()
        val threshold = mean * 2f
        val boundaries = mutableListOf<Long>()
        var lastPeak = -minGapFrames
        for (i in 1 until novelty.size - 1) {
            val isPeak = novelty[i] > threshold && novelty[i] >= novelty[i - 1] && novelty[i] >= novelty[i + 1]
            if (isPeak && i - lastPeak >= minGapFrames) {
                boundaries += frames[i].timeMs
                lastPeak = i
            }
        }
        return boundaries
    }
}
