package dev.geode.render.bridge

import dev.geode.render.LfoTarget
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Test

class WireLayoutTest {
    @Test
    fun sceneParamsNamesMatchTheFrameSize() {
        assertEquals(SceneParamsCodec.FIELDS, SceneParamsCodec.NAMES.size)
        assertEquals(SceneParamsCodec.NAMES.size, SceneParamsCodec.NAMES.toSet().size)
    }

    @Test
    fun cameraFieldsLandInTheirWireSlots() {
        val out = FloatArray(SceneParamsCodec.FIELDS)
        SceneParamsCodec.pack(SceneParams(camFly = 1.25f, camPath = 2, camGyro = true, reactSeed = 9), out)
        assertEquals(1.25f, out[SceneParamsCodec.NAMES.indexOf("camFly")])
        assertEquals(2f, out[SceneParamsCodec.NAMES.indexOf("camPath")])
        assertEquals(1f, out[SceneParamsCodec.NAMES.indexOf("camGyro")])
        assertEquals(9f, out[SceneParamsCodec.NAMES.indexOf("reactSeed")])
    }

    @Test
    fun reactionsPackEverySlotAndDisableTheRest() {
        val route =
            ReactionRoute(true, ReactSource.DROP, LfoTarget.CAM_FLY, ReactMode.STEP, 0.8f, attackSeconds = 0.2f, releaseSeconds = 2f)
        val out = ModConfigCodec.packReactions(listOf(route))
        assertEquals(ReactionRoute.MAX_ROUTES * ModConfigCodec.REACT_ROUTE_FLOATS, out.size)
        assertEquals(1f, out[0])
        assertEquals(ReactSource.DROP.ordinal.toFloat(), out[1])
        assertEquals(LfoTarget.CAM_FLY.ordinal.toFloat(), out[2])
        assertEquals(ReactMode.STEP.ordinal.toFloat(), out[3])
        assertEquals(0.8f, out[4])
        assertEquals(2f, out[7])
        // The second slot was never written: native must see it disabled.
        assertEquals(0f, out[ModConfigCodec.REACT_ROUTE_FLOATS])
    }

    @Test
    fun enumOrdinalsMatchTheNativeEnums() {
        // core/viz/Reactions.hpp: Arrival is last, Spin is last; core/viz/Lfo.hpp: CamShake is last.
        assertEquals(30, ReactSource.entries.size)
        assertEquals(28, ReactSource.MACRO_ENERGY.ordinal)
        assertEquals(3, ReactMode.entries.size)
        assertEquals(LfoTarget.entries.last(), LfoTarget.CAM_SHAKE)
        assertEquals(52, LfoTarget.CAM_SHAKE.ordinal)
    }
}
