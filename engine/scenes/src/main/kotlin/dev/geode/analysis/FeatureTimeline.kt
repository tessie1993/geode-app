package dev.geode.analysis

import dev.geode.engine.audio.DrumChannels
import dev.geode.engine.audio.PulseReplay
import kotlin.math.roundToInt
import kotlin.math.sqrt

data class TimelineFrame(
    val timeMs: Long,
    val features: AudioFeatures,
)

class FeatureTimeline(
    val frames: List<TimelineFrame>,
    val hopMs: Long,
    val key: String = "",
    /** Frames per second of audio: the stream's rate over the analysis hop, after any decimation. */
    val hopRateHz: Float,
    /** The decoded stream's rate. The drum channels are re-derived from it on a cache load. */
    val sampleRateHz: Int = DEFAULT_SAMPLE_RATE_HZ,
) {
    val durationMs: Long = frames.lastOrNull()?.timeMs ?: 0L
    val averageEnergy: Float = if (frames.isEmpty()) 0f else frames.map { it.features.rms }.average().toFloat()
    val averageCentroid: Float = if (frames.isEmpty()) 0f else frames.map { it.features.centroid }.average().toFloat()
    val bpm: Float = trackBpm(frames)

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
        return FeatureTimeline(out, hopMs, key, hopRateHz, this.sampleRateHz)
    }

    fun withDrumChannels(sampleRateHz: Int = this.sampleRateHz): FeatureTimeline {
        if (frames.isEmpty()) return this
        val bandCount = frames[0].features.bands.size
        if (bandCount == 0) return this
        val out = ArrayList<TimelineFrame>(frames.size)
        var anyChanged = false
        DrumChannels(bandCount, hopRateHz, sampleRateHz).use { channels ->
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
        }
        return if (anyChanged) FeatureTimeline(out, hopMs, key, hopRateHz, this.sampleRateHz) else this
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
        // Every one-hop pulse live holds (AnalysisEngine.PulseHold) is folded over the span here,
        // so a 30 fps export sees the drop, section, downbeat and drum hits a single hop flagged.
        var out = f
        for (i in first + 1..last) {
            val g = frames[i].features
            out =
                out.copy(
                    beat = out.beat || g.beat,
                    onset = maxOf(out.onset, g.onset),
                    flux = maxOf(out.flux, g.flux),
                    beatStrength = maxOf(out.beatStrength, g.beatStrength),
                    transient = maxOf(out.transient, g.transient),
                    kick = maxOf(out.kick, g.kick),
                    snare = maxOf(out.snare, g.snare),
                    hat = maxOf(out.hat, g.hat),
                    downbeat = out.downbeat || g.downbeat,
                    sectionBoundary = out.sectionBoundary || g.sectionBoundary,
                    drop = out.drop || g.drop,
                    arrival = out.arrival || g.arrival,
                )
        }
        return out
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
        windowFrames: Int = (SECTION_WINDOW_SECONDS * hopRateHz).roundToInt(),
        minGapFrames: Int = (SECTION_MIN_GAP_SECONDS * hopRateHz).roundToInt(),
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

    companion object {
        /** What a timeline built before the rate was recorded assumed. */
        const val DEFAULT_SAMPLE_RATE_HZ = 48_000

        /** Each side of the novelty comparison. */
        private const val SECTION_WINDOW_SECONDS = 1.5f

        /** No two section boundaries closer than this. */
        private const val SECTION_MIN_GAP_SECONDS = 5f

        /**
         * The track's tempo: the median of every frame's BPM, weighted by the tracker's
         * confidence in it.
         *
         * Taking the last frame's value, as this did, reported whatever the tracker believed
         * during the fade-out or the closing silence, and that one number seeds bar trims, loop
         * lengths and the library's BPM column. Frames with no tempo (warm-up, silence) carry no
         * weight; if no frame has any confidence, every frame with a tempo counts equally.
         */
        fun trackBpm(frames: List<TimelineFrame>): Float {
            val rated = frames.map { it.features }.filter { it.bpm > 0f }.sortedBy { it.bpm }
            if (rated.isEmpty()) return 0f
            val anyConfidence = rated.any { it.pulseConfidence > 0f }
            val weights = rated.map { if (anyConfidence) it.pulseConfidence.coerceAtLeast(0f) else 1f }
            val half = weights.sum() / 2f
            var acc = 0f
            var median = rated.last().bpm
            for (i in rated.indices) {
                acc += weights[i]
                if (acc >= half) {
                    median = rated[i].bpm
                    break
                }
            }
            return median
        }
    }
}
