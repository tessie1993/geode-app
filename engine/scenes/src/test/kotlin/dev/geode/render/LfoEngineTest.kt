package dev.geode.render

import dev.geode.analysis.AudioFeatures
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import kotlin.random.Random

/**
 * Export re-renders a track frame-exact from the analysis timeline, so the same track has to
 * produce the same frames. The sample-and-hold LFO draws a fresh value on every cycle boundary,
 * which used to come from `Math.random()` - a global source no caller could pin, and the one
 * thing on the export path that made a re-render differ from the render before it.
 */
class LfoEngineTest {
    private fun sampleHoldConfig() =
        LfoConfig(
            enabled = true,
            source = ModSource.LFO,
            target = LfoTarget.ZOOM,
            wave = LfoWave.RANDOM,
            // 5 Hz against the 60 fps tick below, so the run below crosses ~10 cycle
            // boundaries: enough draws that a shared sequence cannot be coincidence.
            rateSeconds = 0.2f,
            depth = 1f,
        )

    private fun run(seed: Long): List<FloatArray> {
        val engine = LfoEngine(Random(seed))
        engine.configs = listOf(sampleHoldConfig())
        val features = AudioFeatures(FloatArray(32), FloatArray(256))
        // tick() hands back the engine's own reusable array, so each frame is copied out.
        return (0 until FRAMES).map { engine.tick(DT, features).copyOf() }
    }

    @Test
    fun `same seed renders the same sample-and-hold sequence`() {
        val first = run(SEED)
        val second = run(SEED)

        first.forEachIndexed { frame, expected ->
            assertArrayEquals("frame $frame diverged", expected, second[frame], 0f)
        }
    }

    @Test
    fun `a different seed renders a different sequence`() {
        val a = run(SEED)
        val b = run(SEED + 1)

        // Guards the assertion above against passing for the wrong reason: if the seed were
        // ignored, or the wave emitted a constant, both runs would match here too.
        assertFalse(
            "the seed is not reaching the sample-and-hold draw",
            a.indices.all { a[it].contentEquals(b[it]) },
        )
    }

    private companion object {
        const val SEED = 20260824L
        const val DT = 1f / 60f
        const val FRAMES = 120
    }
}
