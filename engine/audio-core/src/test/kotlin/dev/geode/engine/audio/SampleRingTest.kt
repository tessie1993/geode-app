package dev.geode.engine.audio

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SampleRingTest {
    @Test
    fun readsBackExactlyTheFramesWrittenInterleaved() {
        val ring = SampleRing(capacityFrames = 16, channelCount = 2, maxWriteFrames = 4)
        ring.write(floatArrayOf(1f, -1f, 2f, -2f, 3f, -3f), frameCount = 3, sourceChannelCount = 2)
        ring.write(floatArrayOf(4f, -4f), frameCount = 1, sourceChannelCount = 2)
        val out = FloatArray(4)
        assertTrue(ring.readInterleaved(firstFrame = 1, count = 2, outChannels = 2, out = out))
        assertArrayEquals(floatArrayOf(2f, -2f, 3f, -3f), out, 0f)
        val mono = FloatArray(4)
        assertTrue(ring.readInterleaved(firstFrame = 0, count = 4, outChannels = 1, out = mono))
        assertArrayEquals(floatArrayOf(1f, 2f, 3f, 4f), mono, 0f)
    }

    @Test
    fun refusesFramesNotWrittenYetOrAlreadyOverwritten() {
        val ring = SampleRing(capacityFrames = 8, channelCount = 2, maxWriteFrames = 2)
        repeat(10) { ring.write(floatArrayOf(it.toFloat(), 0f, it.toFloat(), 0f), frameCount = 2, sourceChannelCount = 2) }
        val out = FloatArray(4)
        assertFalse("not written yet", ring.readInterleaved(19, 2, 2, out))
        assertFalse("overwritten", ring.readInterleaved(0, 2, 2, out))
        assertTrue(ring.readInterleaved(18, 2, 2, out))
    }
}
