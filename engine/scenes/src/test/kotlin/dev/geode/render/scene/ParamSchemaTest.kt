package dev.geode.render.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The shell's reading of the native params schema. These run on the checked-in copy of the table
 * (core/tests/params_schema.txt, which the native params_schema_test proves equal to what C++ emits),
 * so a failure here is the parser misreading the engine's table.
 */
class ParamSchemaTest {
    @Test
    fun everySlotHoldsItsRowInOrder() {
        assertEquals(164, ParamSchema.params.size)
        ParamSchema.params.forEachIndexed { slot, spec ->
            assertEquals(slot, spec.slot)
            assertSame(spec, ParamSchema[slot])
            assertSame(spec, ParamSchema[spec.name])
        }
        val names = ParamSchema.params.map { it.name }
        assertEquals(names.size, names.toSet().size)
        assertNull(ParamSchema["noSuchParam"])
        assertNull(ParamSchema["customPaletteId"])
    }

    @Test
    fun rowsCarryKindDefaultRangeAndFade() {
        assertEquals(ParamSpec(0, "speed", ParamSpec.Kind.FLOAT, 1f, 0.05f..4f, fades = true), ParamSchema["speed"])
        assertEquals(ParamSpec(108, "flowCurl", ParamSpec.Kind.FLOAT, 25f, 0f..50f, fades = true), ParamSchema["flowCurl"])
        assertEquals(ParamSpec(110, "waterDamping", ParamSpec.Kind.FLOAT, 0.985f, 0.9f..0.999f, fades = true), ParamSchema["waterDamping"])
        assertEquals(ParamSpec(153, "reactSeed", ParamSpec.Kind.INT, 0f, 0f..16777215f, fades = false), ParamSchema["reactSeed"])
        assertEquals(ParamSpec.Kind.CIRCULAR, ParamSchema["colorShift"]?.kind)
        assertEquals(ParamSpec.Kind.BOOL, ParamSchema["fluidAutoQuality"]?.kind)
        assertEquals(1f, ParamSchema["fluidAutoQuality"]?.default)
        // The custom palette sentinels never glide: a fade through -1 would flash the palette's own colours.
        assertEquals(-1f, ParamSchema["paletteBaseOverride"]?.default)
        assertFalse(ParamSchema["paletteBaseOverride"]?.fades ?: true)
    }

    @Test
    fun retiredRowsKeepTheirSlotAndHaveNoField() {
        val retired = ParamSchema.params.filter { it.retired }.map { it.name }
        assertEquals(listOf("pulse", "shake", "flash", "glitch", "formDrive", "motionOrbit"), retired)
        val fields = SceneParams::class.java.declaredFields.map { it.name }.toSet()
        ParamSchema.params.forEach { assertEquals(it.name, !it.retired, it.name in fields) }
    }

    @Test
    fun everyCountSnapsAndEveryDefaultLiesInItsRange() {
        ParamSchema.params.forEach { spec ->
            assertTrue(spec.name, spec.default in spec.range)
            if (spec.kind == ParamSpec.Kind.INT || spec.kind == ParamSpec.Kind.BOOL) assertFalse(spec.name, spec.fades)
            if (spec.kind == ParamSpec.Kind.BOOL) assertEquals(spec.name, 0f..1f, spec.range)
        }
    }

    @Test
    fun parseReadsAMinimalTable() {
        val text = "params|2|2\nparam|0|a|FLOAT|0.5|0|1|FADE|LIVE\nparam|1|b|BOOL|1|0|1|HOLD|RETIRED\n"
        assertEquals(
            listOf(
                ParamSpec(0, "a", ParamSpec.Kind.FLOAT, 0.5f, 0f..1f, fades = true),
                ParamSpec(1, "b", ParamSpec.Kind.BOOL, 1f, 0f..1f, fades = false, retired = true),
            ),
            ParamSchema.parse(text),
        )
    }

    @Test
    fun parseRefusesWhatItCannotTrust() {
        val row = "param|0|a|FLOAT|0.5|0|1|FADE|LIVE"
        assertThrows(IllegalStateException::class.java) { ParamSchema.parse("params|1|1\n$row") }
        assertThrows(IllegalStateException::class.java) { ParamSchema.parse("catalogue|1\n$row") }
        assertThrows(IllegalStateException::class.java) { ParamSchema.parse("params|2|2\n$row") }
        assertThrows(IllegalStateException::class.java) { ParamSchema.parse("params|2|1\nparam|1|a|FLOAT|0.5|0|1|FADE|LIVE") }
        assertThrows(IllegalStateException::class.java) { ParamSchema.parse("params|2|1\nparam|0|a|FLOAT|0.5|0|1|FADE") }
        assertThrows(IllegalStateException::class.java) { ParamSchema.parse("") }
    }
}
