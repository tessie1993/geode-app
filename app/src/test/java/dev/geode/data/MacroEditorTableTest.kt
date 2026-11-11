package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * The knob editor's read of native's text, against core/tests/macro_editor.txt: a bank with a renamed
 * built-in knob, a turned one, a user's knob and a link the style cannot show, which
 * core/tests/macro_bank_test keeps equal to what C++ emits.
 */
class MacroEditorTableTest {
    private val text = File(checkNotNull(System.getProperty(MacroEditorTable.TEXT_FILE_PROPERTY))).readText()
    private val state = MacroEditorTable.parse(text)

    @Test
    fun `the caps and the bank's offers are native's`() {
        assertEquals(8, state.maxKnobs)
        assertEquals(12, state.maxLinks)
        assertEquals(24, state.maxNameBytes)
        assertTrue(state.canAdd)
        assertFalse(state.isDefault)
    }

    @Test
    fun `knobs come in bank order with what each offers`() {
        assertEquals(listOf("Shine", "Motion", "Colour", "Depth", "Mine"), state.knobs.map { it.name })
        val (shine, motion) = state.knobs
        assertTrue(shine.builtIn && !shine.asBuilt)
        assertEquals("", shine.hint)
        assertTrue(motion.builtIn && motion.asBuilt)
        assertEquals(0.5f, motion.value)
        assertTrue(motion.hint.isNotEmpty())
        val mine = state.knobs.last()
        assertFalse(mine.builtIn)
        assertTrue(mine.canLink)
    }

    @Test
    fun `links belong to the knob above them`() {
        assertEquals(listOf("MORPH", "WARP", "TURBULENCE", "TWIST", "TILE"), state.knobs[0].links.map { it.target })
        val mine = state.knobs.last()
        assertEquals(
            listOf(
                MacroLink("ZOOM", 0.25f, -0.25f, true, "EASE_IN", 0.675f, -0.675f),
                MacroLink("FLUID_GLOW", 1f, 0f, false, MacroLink.LINEAR, 1.9f, 0f),
            ),
            mine.links,
        )
    }

    @Test
    fun `a link's range comes in the param's units too`() {
        val tile = state.knobs[0].links.last()
        assertEquals(MacroLink("TILE", 0f, 0.5f, true, MacroLink.LINEAR, 0f, 2.5f), tile)
    }

    @Test
    fun `the curves are native's list`() {
        assertEquals(listOf("LINEAR", "EASE_IN", "EASE_OUT", "S_CURVE"), state.curves)
    }

    @Test
    fun `the controls table maps a slider to its target and knobs`() {
        val controls =
            MacroControlsTable.parse("macrocontrols|1\ncontrol|Bloom|BLOOM|LOOK|1,5\ncontrol|Zoom|ZOOM|MOTION|\ncontrol|bad\n")
        assertEquals(MacroControl("BLOOM", "LOOK", listOf(1, 5)), controls["Bloom"])
        assertEquals(MacroControl("ZOOM", "MOTION", emptyList()), controls["Zoom"])
        assertEquals(2, controls.size)
        assertTrue(MacroControlsTable.parse("macrocontrols|2\ncontrol|Bloom|BLOOM|LOOK|1\n").isEmpty())
    }

    @Test
    fun `every target says whether the style shows it`() {
        assertTrue(state.targets.size > 30)
        assertTrue(state.targets.first { it.name == "ZOOM" }.reaches)
        assertFalse(state.targets.first { it.name == "FLUID_GLOW" }.reaches)
        assertEquals(
            state.targets.size,
            state.targets
                .map { it.name }
                .toSet()
                .size,
        )
    }

    @Test
    fun `the store's knob rows and the editor's agree on a bank text's knob shape`() {
        val bank = "macrobank|1|6\nknob|5|0.25|Mine|\nlink|ZOOM|0.25|-0.25\n"
        assertEquals(listOf(MacroKnob(5, "Mine", "", 0.25f)), MacroStore.knobsOf(bank))
    }

    @Test
    fun `another version or garbage reads as nothing to offer`() {
        assertEquals(MacroEditorState(), MacroEditorTable.parse("macroeditor|2\nknob|1|1|1|1|0|Glow|\n"))
        assertEquals(MacroEditorState(), MacroEditorTable.parse(""))
        val odd =
            MacroEditorTable.parse(
                "macroeditor|1\nknob|x|1|1|1|0|Glow|\nknob|2|1|1|1|0|Heat|\nlink|ZOOM|a|b|1|LINEAR|0|0\nwhat|ever\n",
            )
        assertEquals(listOf(2), odd.knobs.map { it.id })
        assertTrue(
            odd.knobs
                .single()
                .links
                .isEmpty(),
        )
    }
}
