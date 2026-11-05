package dev.geode.model

import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Every Customize slider and scene keyframe range for a scene param is [ControlRange.of] (#346). These
 * read the JVM tests' copy of the native params schema (core/tests/params_schema.txt), so a slider
 * range that leaves the schema's, or a cap that does nothing, fails here.
 */
class ControlRangeTest {
    @Test
    fun aControlRangeIsTheSchemaRangeUnlessAUiCapStopsItShort() {
        assertEquals(0.05f..4f, ControlRange.of(SceneParams::speed))
        assertEquals(0f..50f, ControlRange.of("flowCurl"))
        assertEquals(1f..8f, ControlRange.of(SceneParams::cymaticsModes))
        // Both Dive speed sliders read this; the Style page's used to stop at 0.05.
        assertEquals(0f..1.2f, ControlRange.of(SceneParams::endlessZoomSpeed))
        // The caps: the flash clamp's limits and Fly's old stop; the LFO still reaches the schema's max.
        assertEquals(0.2f..1.25f, ControlRange.of(SceneParams::brightness))
        assertEquals(0.2f..1.25f, ControlRange.of(SceneParams::intensity))
        assertEquals(0f..0.25f, ControlRange.of(SceneParams::bloom))
        assertEquals(0f..2f, ControlRange.of(SceneParams::camFly))
        assertThrows(IllegalArgumentException::class.java) { ControlRange.of("noSuchParam") }
    }

    @Test
    fun everyControlRangeLiesInsideItsSchemaRange() {
        ParamSchema.params.forEach { spec ->
            val control = ControlRange.of(spec.name)
            assertEquals(spec.name, spec.range.start, control.start)
            assertTrue(spec.name, control.endInclusive in spec.range)
        }
    }

    @Test
    fun everyCapNamesAParamAndReallyStopsShort() {
        // A cap at or past the max would be dead weight; one below the min would empty the slider.
        ControlRange.CAPS.forEach { (name, cap) ->
            val range = requireNotNull(ParamSchema[name]) { "cap names no param: $name" }.range
            assertTrue(name, cap > range.start && cap < range.endInclusive)
        }
    }
}
