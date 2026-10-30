package dev.geode.export

import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

/**
 * QA #24: a fast lead-in (analysis, audio transcode) made the old estimate start low, climb,
 * then count down. The lead-in is now flagged not steady and must not bend the estimate.
 */
class RenderEtaTest {
    @Test
    fun `a fast lead-in neither shows nor skews the estimate, which only counts down`() {
        val eta = RenderEta()
        var now = 0L
        // Analysis takes 0..0.2 of the bar in 3 s, the AAC transcode 0.2..0.28 in 2 s.
        for (step in 0..30) {
            assertNull(eta.sample(0.2f * step / 30, now, steady = false))
            now += 100
        }
        for (step in 0..20) {
            assertNull(eta.sample(0.2f + 0.08f * step / 20, now, steady = false))
            now += 100
        }
        // The render takes the rest of the bar at an even pace over four minutes.
        val renderMs = 240_000L
        val renderStart = now
        var shown = 0
        var previous: Long? = null
        while (now - renderStart <= renderMs) {
            val done = (now - renderStart).toFloat() / renderMs
            val estimate = eta.sample(0.28f + 0.72f * done, now)
            if (now - renderStart < 10_000L) assertNull(estimate)
            if (estimate != null) {
                val truth = (renderMs - (now - renderStart)) / 1000L
                assertTrue("estimate $estimate vs $truth s left", abs(estimate - truth) <= 3)
                previous?.let { assertTrue("climbed from $it to $estimate", estimate <= it + 1) }
                previous = estimate
                shown++
            }
            now += 100
        }
        assertTrue(shown > 2_000)
    }
}
