package dev.geode.analysis

import org.junit.Assert.assertEquals
import org.junit.Test

class FeatureTimelineTest {
    private fun frame(
        timeMs: Long,
        bpm: Float,
        confidence: Float,
    ) = TimelineFrame(timeMs, AudioFeatures.empty(4).copy(bpm = bpm, pulseConfidence = confidence))

    @Test
    fun trackBpmIgnoresATrailingFadeOut() {
        val body = (0 until 100).map { frame(it * 16L, 128f, 0.9f) }
        val fade = (100 until 110).map { frame(it * 16L, 96f, 0.2f) }
        assertEquals(128f, FeatureTimeline(body + fade, hopMs = 16).bpm)
    }

    @Test
    fun trackBpmWeighsByConfidence() {
        // More frames say 140, but the tracker was sure only of the 120 ones.
        val sure = (0 until 10).map { frame(it * 16L, 120f, 1f) }
        val unsure = (10 until 25).map { frame(it * 16L, 140f, 0.1f) }
        assertEquals(120f, FeatureTimeline.trackBpm(sure + unsure))
    }

    @Test
    fun trackBpmWithoutConfidenceIsThePlainMedianAndSkipsUntrackedFrames() {
        val frames =
            listOf(
                frame(0, 0f, 0f),
                frame(16, 100f, 0f),
                frame(32, 110f, 0f),
                frame(48, 0f, 0f),
                frame(64, 120f, 0f),
            )
        assertEquals(110f, FeatureTimeline.trackBpm(frames))
        assertEquals(0f, FeatureTimeline.trackBpm(emptyList()))
        assertEquals(0f, FeatureTimeline.trackBpm(listOf(frame(0, 0f, 1f))))
    }

    @Test
    fun timelineCarriesItsSampleRateThroughCopies() {
        val t = FeatureTimeline(listOf(frame(0, 120f, 1f)), hopMs = 16, sampleRateHz = 96_000)
        assertEquals(96_000, t.withBeatSensitivity(1f, 100f).sampleRateHz)
        assertEquals(FeatureTimeline.DEFAULT_SAMPLE_RATE_HZ, FeatureTimeline(emptyList(), hopMs = 16).sampleRateHz)
    }
}
