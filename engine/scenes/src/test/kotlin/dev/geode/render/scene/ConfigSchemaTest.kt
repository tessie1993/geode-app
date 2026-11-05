package dev.geode.render.scene

import dev.geode.render.scene.ParamSpec.Kind.BOOL
import dev.geode.render.scene.ParamSpec.Kind.FLOAT
import dev.geode.render.scene.ParamSpec.Kind.INT
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The shell's reading of the native config schema. These run on the checked-in copy of the table
 * (core/tests/config_schema.txt, which the native config_schema_test proves equal to what C++ emits),
 * so a failure here is the parser misreading the engine's table.
 */
class ConfigSchemaTest {
    @Test
    fun everyGroupHoldsItsRowsInWireOrder() {
        assertEquals(listOf("enabled", "source", "target", "wave", "rateSeconds", "depth", "polarity", "curve"), names("lfo"))
        assertEquals(10, ConfigSchema.group("adsr").size)
        assertEquals(listOf("energy", "speed", "complexity", "mood", "space", "chaos"), names("macro"))
        assertEquals(2, ConfigSchema.group("palette").size)
        assertEquals(2, ConfigSchema.group("look").size)
        assertEquals(6, ConfigSchema.group("clip").size)
        assertEquals(1, ConfigSchema.group("theme").size)
        assertTrue(ConfigSchema.group("noSuchGroup").isEmpty())
    }

    @Test
    fun rowsCarryDefaultAndRange() {
        assertEquals(ParamSpec(4, "rateSeconds", FLOAT, 2f, 0.05f..60f, false), ConfigSchema["lfo", "rateSeconds"])
        assertEquals(ParamSpec(1, "attack", FLOAT, 0.05f, 0.005f..1f, false), ConfigSchema["adsr", "attack"])
        assertEquals(ParamSpec(9, "retrigger", BOOL, 1f, 0f..1f, false), ConfigSchema["adsr", "retrigger"])
        assertEquals(ParamSpec(5, "chaos", FLOAT, 0f, -1f..1f, false), ConfigSchema["macro", "chaos"])
        assertEquals(ParamSpec(0, "presetMorphSeconds", FLOAT, 2f, 0f..8f, false), ConfigSchema["theme", "presetMorphSeconds"])
        assertThrows(NoSuchElementException::class.java) { ConfigSchema["lfo", "rateHz"] }
    }

    @Test
    fun everyDefaultLiesInItsRange() {
        listOf("lfo", "adsr", "macro", "palette", "look", "clip", "theme").flatMap(ConfigSchema::group).forEach { spec ->
            assertTrue(spec.name, spec.default in spec.range)
        }
    }

    @Test
    fun parseReadsAMinimalTable() {
        val text = "configs|1|3\nconfig|a|0|x|FLOAT|0.5|0|1|NONE\nconfig|b|0|y|BOOL|1|0|1|NONE\nconfig|a|1|z|INT|2|0|4|NONE\n"
        assertEquals(
            mapOf(
                "a" to listOf(ParamSpec(0, "x", FLOAT, 0.5f, 0f..1f, false), ParamSpec(1, "z", INT, 2f, 0f..4f, false)),
                "b" to listOf(ParamSpec(0, "y", BOOL, 1f, 0f..1f, false)),
            ),
            ConfigSchema.parse(text),
        )
    }

    @Test
    fun parseRefusesWhatItCannotTrust() {
        val row = "config|a|0|x|FLOAT|0.5|0|1|NONE"
        assertThrows(IllegalStateException::class.java) { ConfigSchema.parse("configs|2|1\n$row") }
        assertThrows(IllegalStateException::class.java) { ConfigSchema.parse("params|1|1\n$row") }
        assertThrows(IllegalStateException::class.java) { ConfigSchema.parse("configs|1|2\n$row") }
        assertThrows(IllegalStateException::class.java) { ConfigSchema.parse("configs|1|1\nconfig|a|1|x|FLOAT|0.5|0|1|NONE") }
        assertThrows(IllegalStateException::class.java) { ConfigSchema.parse("configs|1|1\nconfig|a|0|x|FLOAT|0.5|0|1") }
        assertThrows(IllegalStateException::class.java) { ConfigSchema.parse("configs|1|1\nx") }
        assertThrows(IllegalStateException::class.java) { ConfigSchema.parse("") }
    }

    private fun names(group: String) = ConfigSchema.group(group).map { it.name }
}
