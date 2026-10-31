package dev.geode.engine.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SoundCursorTest {
    private fun stereo(
        from: Int,
        frames: Int,
    ) = FloatArray(frames * 2) { i -> if (i % 2 == 0) (from + i / 2).toFloat() else -(from + i / 2).toFloat() }

    @Test
    fun handsOutEachFrameExactlyOnce() {
        val ring = SampleRing(capacityFrames = 1024, channelCount = 2)
        val cursor = SoundCursor(ring)
        ring.write(stereo(0, 100), 100, 2)
        val first = cursor.take()
        assertTrue(first.restarted)
        assertEquals(100, first.frames)
        ring.write(stereo(100, 50), 50, 2)
        val second = cursor.take()
        assertFalse(second.restarted)
        assertEquals(50, second.frames)
        assertEquals(100f, cursor.interleaved[0])
        assertEquals(-100f, cursor.interleaved[1])
        assertEquals(149f, cursor.interleaved[98])
        assertEquals(0, cursor.take().frames)
    }

    @Test
    fun newEpochRestarts() {
        val ring = SampleRing(capacityFrames = 1024, channelCount = 2)
        val cursor = SoundCursor(ring)
        ring.write(stereo(0, 100), 100, 2)
        cursor.take()
        ring.beginEpoch()
        ring.write(stereo(0, 10), 10, 2)
        val take = cursor.take()
        assertTrue(take.restarted)
        assertEquals(10, take.frames)
        assertEquals(0f, cursor.interleaved[0])
    }

    @Test
    fun stallKeepsTheNewestAudio() {
        val ring = SampleRing(capacityFrames = 1024, channelCount = 2, maxWriteFrames = 256)
        val cursor = SoundCursor(ring, maxFrames = 256)
        ring.write(stereo(0, 10), 10, 2)
        cursor.take()
        repeat(4) { ring.write(stereo(10 + it * 200, 200), 200, 2) }
        val take = cursor.take()
        assertEquals(256, take.frames)
        assertEquals((810 - 256).toFloat(), cursor.interleaved[0])
    }

    @Test
    fun monoSourceFeedsBothChannels() {
        val ring = SampleRing(capacityFrames = 1024, channelCount = 2)
        val cursor = SoundCursor(ring)
        ring.write(FloatArray(4) { it + 1f }, 4, 1)
        cursor.take()
        assertEquals(1f, cursor.interleaved[0])
        assertEquals(1f, cursor.interleaved[1])
        assertEquals(4f, cursor.interleaved[7])
    }
}
