package dev.geode.editor

import dev.geode.model.ControlRange
import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A keyframe track's range is the param's Customize range, read from the native params schema (#346):
 * so a scene track never leaves what the slider offers, nor the range the engine clamps to. These read
 * the JVM tests' copy of the schema (core/tests/params_schema.txt).
 */
class AnimatableParamsRangeTest {
    @Test
    fun everySceneScalarTrackSpansItsControlRange() {
        val scalars = AnimatableParams.scene.filter { it.kind == ParamKind.SCALAR }
        assertTrue(scalars.isNotEmpty())
        scalars.forEach { track ->
            val name = track.id.value.removePrefix("scene.")
            val schema = requireNotNull(ParamSchema[name]) { "no schema row for $name" }.range
            assertEquals(name, ControlRange.of(name), track.min..track.max)
            assertTrue(name, track.min in schema && track.max in schema)
        }
    }

    @Test
    fun theCapsAndTheOneSettledDisagreementHoldForKeyframesToo() {
        assertEquals(0.2f..1.25f, range("brightness"))
        assertEquals(0f..0.25f, range("bloom"))
        assertEquals(-2f..2f, range("camFly"))
        // The Style page's Dive speed and its keyframes stopped at 0.05; Liquid's slider and the schema at 0.
        assertEquals(0f..1.2f, range("endlessZoomSpeed"))
    }

    @Test
    fun aKeyframeOutsideTheRangeIsClampedToIt() {
        val track = requireNotNull(AnimatableParams.find(ParamId("scene.bloom")))
        val written = AnimatableParams.applyToScene(SceneParams.DEFAULT, mapOf(track.id to ParamValue.Scalar(0.9f)))
        assertEquals(0.25f, written.bloom)
    }

    private fun range(name: String): ClosedFloatingPointRange<Float> {
        val track = requireNotNull(AnimatableParams.find(ParamId("scene.$name"))) { "no keyframe track for $name" }
        return track.min..track.max
    }
}
