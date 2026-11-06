package dev.geode.data

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Reduced motion follows Android alone (#336): on exactly when the animator duration scale is 0. */
class SystemMotionTest {
    @Test
    fun removeAnimationsTurnsReducedMotionOn() {
        // "Remove animations" (and the developer option "Animation off") set the scale to 0.
        assertTrue(SystemMotion.reducedAt(0f))
    }

    @Test
    fun anyOtherScaleLeavesMotionAsIs() {
        assertFalse(SystemMotion.reducedAt(SystemMotion.DEFAULT_SCALE))
        assertFalse(SystemMotion.reducedAt(0.5f))
        assertFalse(SystemMotion.reducedAt(10f))
    }
}
