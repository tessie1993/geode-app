package dev.geode.render.bridge

import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.LfoTarget
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.ParamSpec
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.lang.reflect.Field
import java.lang.reflect.Modifier

/**
 * The wire frames as Kotlin packs them. The scene params frame is the native schema's (core/viz/ParamTable.inc,
 * read here from core/tests/params_schema.txt, which params_schema_test proves equal to what C++ emits), so
 * these hold the Kotlin data class to that schema: one field per row, the same kinds and defaults, and a frame
 * that survives a round trip bit for bit.
 */
class WireLayoutTest {
    // The data class's wire fields: every instance field but the Kotlin-only ones.
    private val kotlinFields: Map<String, Field> =
        SceneParams::class.java.declaredFields
            .filter { !Modifier.isStatic(it.modifiers) && it.name !in SceneParams.NOT_RENDERED }
            .associateBy { it.name }

    private fun slot(name: String): Int = checkNotNull(ParamSchema[name]) { "no schema row $name" }.slot

    @Test
    fun everyKotlinFieldHasASchemaRowAndEveryLiveRowAField() {
        assertEquals(ParamSchema.params.size, SceneParamsCodec.FIELDS)
        val live = ParamSchema.params.filterNot { it.retired }
        assertEquals(kotlinFields.keys, live.map { it.name }.toSet())
        SceneParams.NOT_RENDERED.keys.forEach { assertNull(it, ParamSchema[it]) }
    }

    @Test
    fun kindsAndDefaultsAgree() {
        val defaults = SceneParamsCodec.pack(SceneParams.DEFAULT)
        ParamSchema.params.forEach { spec ->
            val type =
                when (spec.kind) {
                    ParamSpec.Kind.FLOAT, ParamSpec.Kind.CIRCULAR -> Float::class.javaPrimitiveType
                    ParamSpec.Kind.INT -> Int::class.javaPrimitiveType
                    ParamSpec.Kind.BOOL -> Boolean::class.javaPrimitiveType
                }
            if (!spec.retired) assertEquals(spec.name, type, kotlinFields.getValue(spec.name).type)
            assertEquals(spec.name, spec.default, defaults[spec.slot])
        }
    }

    @Test
    fun theWireRoundTripsAtEveryRangeEnd() {
        for (top in listOf(false, true)) {
            // A retired slot carries its default, whatever came in.
            val frame =
                FloatArray(SceneParamsCodec.FIELDS) {
                    val spec = ParamSchema[it]
                    when {
                        spec.retired -> spec.default
                        top -> spec.range.endInclusive
                        else -> spec.range.start
                    }
                }
            val stale = FloatArray(SceneParamsCodec.FIELDS) { ParamSchema[it].range.let { r -> if (top) r.endInclusive else r.start } }
            assertEquals(SceneParamsCodec.unpack(frame, SceneParams.DEFAULT), SceneParamsCodec.unpack(stale, SceneParams.DEFAULT))
            val p = SceneParamsCodec.unpack(frame, SceneParams.DEFAULT)
            assertArrayEquals(frame, SceneParamsCodec.pack(p), 0f)
            assertEquals(p, SceneParamsCodec.unpack(SceneParamsCodec.pack(p), SceneParams.DEFAULT))
        }
    }

    @Test
    fun fieldsLandInTheirSchemaSlots() {
        val p =
            SceneParams(
                camFly = 1.25f,
                camPath = 2,
                camGyro = true,
                reactSeed = 9,
                noteDrums = 1.75f,
                liquidSurface = 1,
                camFlat = 0.5f,
                paletteStop4 = 0xFFFFFF,
                diveTwist = -0.5f,
                diveFollow = 1.5f,
                paletteAmount3 = 0.25f,
                paletteShift = 0.75f,
            )
        val out = SceneParamsCodec.pack(p)
        assertEquals(1.25f, out[slot("camFly")])
        assertEquals(2f, out[slot("camPath")])
        assertEquals(1f, out[slot("camGyro")])
        assertEquals(9f, out[slot("reactSeed")])
        assertEquals(1.75f, out[slot("noteDrums")])
        assertEquals(1f, out[slot("liquidSurface")])
        assertEquals(0.5f, out[slot("camFlat")])
        assertEquals(16_777_215f, out[slot("paletteStop4")])
        assertEquals(-0.5f, out[slot("diveTwist")])
        assertEquals(1.5f, out[slot("diveFollow")])
        assertEquals(0.25f, out[slot("paletteAmount3")])
        assertEquals(0.75f, out[slot("paletteShift")])
        assertEquals(p, SceneParamsCodec.unpack(out, SceneParams.DEFAULT))
    }

    @Test
    fun appendedFieldsKeepTheirSlots() {
        // core/viz/ParamTable.inc only appends: a slot that moved would misread every frame and saved look.
        assertEquals(187, SceneParamsCodec.FIELDS)
        assertEquals(0, slot("speed"))
        assertEquals(108, slot("flowCurl"))
        assertEquals(153, slot("reactSeed"))
        assertEquals(163, slot("camFlat"))
        assertEquals(168, slot("paletteStop4"))
        assertEquals(177, slot("diveFollow"))
        val tail = ParamSchema.params.takeLast(3).map { it.name }
        assertEquals(listOf("paletteAmount6", "paletteAmount7", "paletteShift"), tail)
    }

    @Test
    fun reactionsPackEverySlotAndDisableTheRest() {
        val route =
            ReactionRoute(true, ReactSource.HARD_CUT, LfoTarget.CAM_FLY, ReactMode.STEP, 0.8f, attackSeconds = 0.2f, releaseSeconds = 2f)
        val out = ModConfigCodec.packReactions(listOf(route))
        assertEquals(ReactionRoute.MAX_ROUTES * ModConfigCodec.REACT_ROUTE_FLOATS, out.size)
        assertEquals(1f, out[0])
        assertEquals(ReactSource.HARD_CUT.ordinal.toFloat(), out[1])
        assertEquals(LfoTarget.CAM_FLY.ordinal.toFloat(), out[2])
        assertEquals(ReactMode.STEP.ordinal.toFloat(), out[3])
        assertEquals(0.8f, out[4])
        assertEquals(2f, out[7])
        // The second slot was never written: native must see it disabled.
        assertEquals(0f, out[ModConfigCodec.REACT_ROUTE_FLOATS])
    }

    @Test
    fun styleDnaUnpacksWhatPackWrote() {
        val routes =
            listOf(
                ReactionRoute(true, ReactSource.KICK, LfoTarget.COLOR_SHIFT, ReactMode.JUMP, 0.4f, attackSeconds = 0.08f),
                ReactionRoute(true, ReactSource.HARD_CUT, LfoTarget.CAM_FLY, ReactMode.FOLLOW, 1.2f, releaseSeconds = 2.2f),
            )
        val packed = ModConfigCodec.packReactions(routes).copyOf(routes.size * ModConfigCodec.REACT_ROUTE_FLOATS)
        assertEquals(routes, ModConfigCodec.unpackReactions(packed))
    }

    @Test
    fun unknownOrdinalsFromNativeAreDropped() {
        val packed = ModConfigCodec.packReactions(listOf(ReactionRoute(target = LfoTarget.ZOOM))).copyOf(ModConfigCodec.REACT_ROUTE_FLOATS)
        packed[1] = 999f
        assertEquals(emptyList<ReactionRoute>(), ModConfigCodec.unpackReactions(packed))
    }

    @Test
    fun enumOrdinalsMatchTheNativeEnums() {
        // core/viz/Reactions.hpp: Eq22k is last, Toggle is last; core/viz/Lfo.hpp: PaletteShift is last.
        assertEquals(118, ReactSource.entries.size)
        assertEquals(72, ReactSource.EQ_25.ordinal)
        assertEquals(46, ReactSource.KICK.ordinal)
        assertEquals(55, ReactSource.HARD_CUT.ordinal)
        assertEquals(5, ReactMode.entries.size)
        assertEquals(3, ReactMode.JUMP.ordinal)
        assertEquals(4, ReactMode.TOGGLE.ordinal)
        assertEquals(LfoTarget.entries.last(), LfoTarget.PALETTE_SHIFT)
        assertEquals(67, LfoTarget.PALETTE_SHIFT.ordinal)
        assertEquals(60, LfoTarget.PALETTE_AMOUNT_1.ordinal)
        assertEquals(59, LfoTarget.MACRO.ordinal)
        assertEquals(52, LfoTarget.CAM_SHAKE.ordinal)
        assertEquals(55, LfoTarget.DIVE_SPEED.ordinal)
        assertEquals(58, LfoTarget.DIVE_AIM_Y.ordinal)
    }

    @Test
    fun aWaveAndAnEnvelopeCarryTheirKnobAndMoment() {
        // geode_viz_set_lfo floats 9 and 10, geode_viz_set_adsr floats 10 and 11 (GEODE_*_CONFIG_FLOATS).
        val lfo = ModConfigCodec.packLfo(LfoConfig(target = LfoTarget.MACRO, knob = 5, moment = 4))
        assertEquals(11, lfo.size)
        assertEquals(listOf(59f, 5f, 4f), listOf(lfo[2], lfo[9], lfo[10]))
        assertEquals(-1f, ModConfigCodec.packLfo(LfoConfig())[10])
        val env = ModConfigCodec.packAdsr(AdsrConfig(targets = listOf(LfoTarget.MACRO), knob = 3, moment = 0))
        assertEquals(listOf(3f, 0f, 59f), listOf(env[10], env[11], env[12]))
    }
}
