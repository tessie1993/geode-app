package dev.geode.render.scene

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
        assertEquals(ConfigSpec("rateSeconds", 2f, 0.05f..60f), ConfigSchema["lfo", "rateSeconds"])
        assertEquals(ConfigSpec("attack", 0.05f, 0.005f..1f), ConfigSchema["adsr", "attack"])
        assertEquals(ConfigSpec("retrigger", 1f, 0f..1f), ConfigSchema["adsr", "retrigger"])
        assertEquals(ConfigSpec("chaos", 0f, -1f..1f), ConfigSchema["macro", "chaos"])
        assertEquals(ConfigSpec("presetMorphSeconds", 2f, 0f..8f), ConfigSchema["theme", "presetMorphSeconds"])
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
                "a" to listOf(ConfigSpec("x", 0.5f, 0f..1f), ConfigSpec("z", 2f, 0f..4f)),
                "b" to listOf(ConfigSpec("y", 1f, 0f..1f)),
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
        assertThrows(IllegalStateException::class.java) { ConfigSchema.parse("") }
    }

    private fun names(group: String) = ConfigSchema.group(group).map { it.name }
}
