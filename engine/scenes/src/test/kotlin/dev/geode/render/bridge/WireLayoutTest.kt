package dev.geode.render.bridge

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
                kaleidoFolds = 8f,
                layerMix = -0.25f,
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
        assertEquals(8f, out[slot("kaleidoFolds")])
        assertEquals(-0.25f, out[slot("layerMix")])
        assertEquals(p, SceneParamsCodec.unpack(out, SceneParams.DEFAULT))
    }

    @Test
    fun appendedFieldsKeepTheirSlots() {
        // core/viz/ParamTable.inc only appends: a slot that moved would misread every frame and saved look.
        assertEquals(189, SceneParamsCodec.FIELDS)
        assertEquals(0, slot("speed"))
        assertEquals(108, slot("flowCurl"))
        assertEquals(153, slot("reactSeed"))
        assertEquals(163, slot("camFlat"))
        assertEquals(168, slot("paletteStop4"))
        assertEquals(177, slot("diveFollow"))
        val tail = ParamSchema.params.takeLast(3).map { it.name }
        assertEquals(listOf("paletteShift", "kaleidoFolds", "layerMix"), tail)
    }
}
