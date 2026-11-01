package dev.geode.render

import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundInput
import org.junit.Assert.assertEquals
import org.junit.Test

class AmbientPacingTest {
    private val ring = SampleRing(capacityFrames = 8192, channelCount = 2)
    private val input = SoundInput(ring) { 44_100 }

    private fun play(
        level: Float,
        frames: Int = 1024,
    ) {
        ring.write(FloatArray(frames * 2) { level }, frames, 2)
    }

    @Test
    fun theDefaultIsBatterySaverAndUnknownKeysFallBackToIt() {
        assertEquals(AmbientFrameRate.BATTERY_SAVER, AmbientFrameRate.DEFAULT)
        assertEquals(AmbientFrameRate.DEFAULT, AmbientFrameRate.fromKey(null))
        assertEquals(AmbientFrameRate.DEFAULT, AmbientFrameRate.fromKey("bogus"))
        AmbientFrameRate.entries.forEach { assertEquals(it, AmbientFrameRate.fromKey(it.key)) }
    }

    @Test
    fun noSessionMeansIdle() {
        val pacing = AmbientPacing(AmbientFrameRate.BATTERY_SAVER, capFps = 60) { null }
        assertEquals(15, pacing.targetFps)
        assertEquals(15, pacing.onFrame(0L))
        assertEquals(15, pacing.onFrame(5_000L))
    }

    @Test
    fun musicRampsUpOnTheVeryNextFrame() {
        val pacing = AmbientPacing(AmbientFrameRate.BATTERY_SAVER, capFps = 60) { input }
        assertEquals(15, pacing.onFrame(0L))
        play(0.5f)
        assertEquals(30, pacing.onFrame(66L))
    }

    @Test
    fun smoothRunsAtSixtyWhilePlayingAndFifteenWhenStopped() {
        val pacing = AmbientPacing(AmbientFrameRate.SMOOTH, capFps = 60) { input }
        pacing.onFrame(-16L)
        play(0.5f)
        assertEquals(60, pacing.onFrame(0L))
        // Paused: the ring stops moving. Short gaps keep the rate, two seconds drop it.
        assertEquals(60, pacing.onFrame(1_000L))
        assertEquals(60, pacing.onFrame(1_999L))
        assertEquals(15, pacing.onFrame(2_000L))
        play(0.5f)
        assertEquals(60, pacing.onFrame(2_016L))
    }

    @Test
    fun aRingStillWritingSilenceCountsAsIdle() {
        val pacing = AmbientPacing(AmbientFrameRate.BATTERY_SAVER, capFps = 60) { input }
        pacing.onFrame(-16L)
        play(0.5f)
        assertEquals(30, pacing.onFrame(0L))
        var now = 0L
        while (now < 2_500L) {
            now += 33L
            play(0f, frames = 1470)
            pacing.onFrame(now)
        }
        assertEquals(15, pacing.targetFps)
    }

    @Test
    fun audioAlreadyInTheRingWhenTheSurfaceStartsIsNotMusic() {
        play(0.5f)
        val pacing = AmbientPacing(AmbientFrameRate.BATTERY_SAVER, capFps = 60) { input }
        assertEquals(15, pacing.onFrame(0L))
        assertEquals(15, pacing.onFrame(16L))
    }

    @Test
    fun theFrameRateCapIsACeiling() {
        val pacing = AmbientPacing(AmbientFrameRate.SMOOTH, capFps = 30) { input }
        pacing.onFrame(-16L)
        play(0.5f)
        assertEquals(30, pacing.onFrame(0L))
        assertEquals(15, pacing.onFrame(3_000L))
    }

    @Test
    fun aNewEpochWithSoundIsHeard() {
        val pacing = AmbientPacing(AmbientFrameRate.BATTERY_SAVER, capFps = 60) { input }
        pacing.onFrame(-16L)
        play(0.5f)
        assertEquals(30, pacing.onFrame(0L))
        assertEquals(15, pacing.onFrame(3_000L))
        // A seek restarts the ring's count; the same written total must not read as "no new audio".
        ring.beginEpoch()
        play(0.5f)
        assertEquals(30, pacing.onFrame(3_016L))
    }
}
