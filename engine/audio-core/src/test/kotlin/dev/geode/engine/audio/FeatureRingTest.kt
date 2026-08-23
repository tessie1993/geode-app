package dev.geode.engine.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/**
 * The ring is read by the renderer at a sample index the writer has not
 * necessarily reached, so which of the five Acquire answers comes back is the
 * contract - not the float that happens to land in the frame.
 */
class FeatureRingTest {
    private fun ring() = FeatureRing(continuousSlots = 1, eventSlots = 1, capacityFrames = 8)

    private fun frame() = FeatureFrame(continuousSlots = 1, eventSlots = 1)

    @Test
    fun `a ring nothing has been published to is EMPTY`() {
        assertEquals(FeatureRing.Acquire.EMPTY, ring().acquireAt(0L, 0L, frame()))
    }

    @Test
    fun `asking past the newest frame is NOT_YET_AVAILABLE`() {
        val r = ring()
        r.publish(0L, floatArrayOf(0f), floatArrayOf(0f))
        r.publish(100L, floatArrayOf(10f), floatArrayOf(0f))

        assertEquals(FeatureRing.Acquire.NOT_YET_AVAILABLE, r.acquireAt(500L, 0L, frame()))
    }

    @Test
    fun `a sample between two frames interpolates the continuous slot`() {
        val r = ring()
        r.publish(0L, floatArrayOf(0f), floatArrayOf(0f))
        r.publish(100L, floatArrayOf(10f), floatArrayOf(0f))

        val out = frame()
        assertEquals(FeatureRing.Acquire.OK, r.acquireAt(50L, 0L, out))
        // Halfway between sample 0 and sample 100, so halfway between 0f and 10f.
        assertEquals(5f, out.continuous[0], 1e-6f)
    }

    @Test
    fun `an event slot reports the loudest hit across the span, not the one at its edge`() {
        val r = ring()
        r.publish(0L, floatArrayOf(0f), floatArrayOf(0.2f))
        r.publish(100L, floatArrayOf(10f), floatArrayOf(0.9f))
        r.publish(200L, floatArrayOf(20f), floatArrayOf(0.1f))

        val out = frame()
        assertEquals(FeatureRing.Acquire.OK, r.acquireAt(0L, 200L, out))
        // A transient must survive being sampled at a frame boundary that missed it.
        assertEquals(0.9f, out.events[0], 1e-6f)
    }

    @Test
    fun `an acquired frame carries the epoch it was read in`() {
        val r = ring()
        r.publish(0L, floatArrayOf(1f), floatArrayOf(0f))

        val out = frame()
        assertEquals(FeatureRing.Acquire.OK, r.acquireAt(0L, 0L, out))
        assertEquals(r.epoch, out.epoch)
    }

    @Test
    fun `beginEpoch advances the epoch and empties the ring`() {
        val r = ring()
        r.publish(0L, floatArrayOf(1f), floatArrayOf(0f))
        val before = r.epoch

        r.beginEpoch()

        assertEquals(before + 1, r.epoch)
        assertEquals(FeatureRing.Acquire.EMPTY, r.acquireAt(0L, 0L, frame()))
    }

    @Test
    fun `capacity that is not a power of two is rejected at construction`() {
        assertThrows(IllegalArgumentException::class.java) {
            FeatureRing(continuousSlots = 1, eventSlots = 1, capacityFrames = 6)
        }
    }

    @Test
    fun `a ring with no slots at all is rejected at construction`() {
        assertThrows(IllegalArgumentException::class.java) {
            FeatureRing(continuousSlots = 0, eventSlots = 0, capacityFrames = 8)
        }
    }

    @Test
    fun `publishing a sample index that went backwards is rejected`() {
        val r = ring()
        r.publish(100L, floatArrayOf(0f), floatArrayOf(0f))

        assertThrows(IllegalArgumentException::class.java) {
            r.publish(50L, floatArrayOf(0f), floatArrayOf(0f))
        }
    }

    @Test
    fun `publishing the wrong number of slots is rejected`() {
        val r = ring()

        assertThrows(IllegalArgumentException::class.java) {
            r.publish(0L, floatArrayOf(0f, 0f), floatArrayOf(0f))
        }
    }
}
