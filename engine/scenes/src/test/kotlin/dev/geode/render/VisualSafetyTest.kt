package dev.geode.render

import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The flash clamp is the one number a preset must never be able to talk its way past, so the
 * interesting cases are the values that defeat a naive clamp rather than the ones in range.
 *
 * `coerceIn` and `coerceAtMost` return NaN unchanged — every IEEE comparison with a NaN is false,
 * so both branches fall through. A preset decoded from a `geode://preset/` link can carry one.
 */
class VisualSafetyTest {
    private val hostile =
        listOf(
            "NaN" to Float.NaN,
            "+Inf" to Float.POSITIVE_INFINITY,
            "-Inf" to Float.NEGATIVE_INFINITY,
            "large negative" to -1e30f,
            "large positive" to 1e30f,
        )

    @Test
    fun `no hostile value survives the flash clamp in any guarded field`() {
        for ((label, v) in hostile) {
            val out =
                VisualSafety.apply(
                    SceneParams.DEFAULT.copy(
                        strobe = v,
                        flash = v,
                        glitch = v,
                        bloom = v,
                        brightness = v,
                        intensity = v,
                        contrast = v,
                    ),
                )
            val guarded =
                mapOf(
                    "strobe" to out.strobe,
                    "flash" to out.flash,
                    "glitch" to out.glitch,
                    "bloom" to out.bloom,
                    "brightness" to out.brightness,
                    "intensity" to out.intensity,
                    "contrast" to out.contrast,
                )
            for ((field, got) in guarded) {
                assertTrue("$field from $label should be finite, was $got", got.isFinite())
                assertTrue("$field from $label should not be negative, was $got", got >= 0f)
            }
            assertTrue(
                "strobe from $label exceeded the flash ceiling: ${out.strobe}",
                out.strobe <= VisualSafety.MAX_FLASH_DEPTH / VisualSafety.STROBE_SHADER_DEPTH,
            )
            assertTrue(
                "flash from $label exceeded the flash ceiling: ${out.flash}",
                out.flash <= VisualSafety.MAX_FLASH_DEPTH / VisualSafety.FLASH_SHADER_DEPTH,
            )
            assertTrue("glitch from $label exceeded the ceiling", out.glitch <= VisualSafety.MAX_FLASH_DEPTH)
            assertTrue("bloom from $label exceeded the ceiling", out.bloom <= VisualSafety.MAX_FLASH_DEPTH)
        }
    }

    @Test
    fun `an in-range preset passes through untouched`() {
        val p = SceneParams.DEFAULT.copy(strobe = 0.1f, flash = 0.2f, brightness = 1.0f)
        val out = VisualSafety.apply(p)
        assertEquals(0.1f, out.strobe, 1e-6f)
        assertEquals(0.2f, out.flash, 1e-6f)
        assertEquals(1.0f, out.brightness, 1e-6f)
    }

    @Test
    fun `reduced motion still yields finite rates when the input was not`() {
        val out = VisualSafety.apply(SceneParams.DEFAULT.copy(strobe = Float.NaN), reducedMotion = true)
        assertTrue(out.strobe.isFinite())
    }
}
