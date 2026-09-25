package dev.geode.ui.opaline

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class OpalineMotionTest {
    @Test fun springSettlesOnTarget() {
        val spring = OpalineSpring(0f, OpalineMotion.VALUE_FREQUENCY, OpalineMotion.VALUE_DAMPING)
        spring.target = 1f
        repeat(240) { spring.step(1f / 60f) }
        assertEquals(1f, spring.value, 1e-3f)
        assertTrue(spring.settled)
    }

    @Test fun pressSpringOvershootsOnRelease() {
        // MotionController's pressure spring is underdamped (0.68): a released gel bounces past rest.
        val spring = OpalineSpring(1f, OpalineMotion.PRESS_FREQUENCY, OpalineMotion.PRESS_DAMPING)
        spring.target = 0f
        var lowest = 1f
        repeat(120) { lowest = minOf(lowest, spring.step(1f / 60f)) }
        assertTrue("expected overshoot below rest, lowest=$lowest", lowest < 0f)
    }

    @Test fun longFramesAreCappedLikeTheLibrary() {
        // Spring.step clamps a frame to 1/20 s, so one 1 s hitch moves no further than 1/20 s would.
        val capped = OpalineSpring(0f, 4f, 0.8f).apply { target = 1f }
        val reference = OpalineSpring(0f, 4f, 0.8f).apply { target = 1f }
        capped.step(1f)
        reference.step(1f / 20f)
        assertEquals(reference.value, capped.value, 1e-6f)
    }

    @Test fun tileFbmTilesAcrossItsPeriod() {
        for (i in 0 until 16) {
            val x = i * 0.37f
            val y = i * 0.61f
            assertEquals(OpalineNoise.tileFbm(x, y, 0f, 4), OpalineNoise.tileFbm(x + 4f, y, 0f, 4), 1e-5f)
            assertEquals(OpalineNoise.tileFbm(x, y, 0f, 4), OpalineNoise.tileFbm(x, y + 4f, 0f, 4), 1e-5f)
        }
    }

    @Test fun noiseStaysInUnitRange() {
        for (i in 0 until 500) {
            val v = OpalineNoise.fbm(i * 0.113f, i * 0.071f, i * 0.017f)
            assertTrue(v in 0f..1f)
            assertTrue(OpalineNoise.hash(i.toFloat(), i * 2f, i * 3f) in 0f..1f)
        }
    }
}
