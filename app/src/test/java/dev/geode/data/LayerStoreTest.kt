package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Test

/** Customize › Layers draws native's rows (core/viz/LayerPick editorText) as they are: this is only the split. */
class LayerStoreTest {
    @Test
    fun readsEveryRowNativeWrites() {
        val editor =
            LayersEditor.parse(
                """
                layers|1
                state|1|MACRO|kifs|2|5|0.25
                shown|plasma
                source|USER|You
                source|DIRECTOR|Auto Director
                source|MACRO|Macro
                blend|0|Normal
                blend|1|Screen
                family|fractals|julia,mandel
                knob|1|Glow
                knob|2|Heat
                """.trimIndent(),
            )
        assertEquals(true, editor.on)
        assertEquals("MACRO", editor.source)
        assertEquals("kifs", editor.styleId)
        assertEquals(2, editor.knob)
        assertEquals(5, editor.blend)
        assertEquals(0.25f, editor.mix)
        assertEquals("plasma", editor.shown)
        assertEquals(listOf("You", "Auto Director", "Macro"), editor.sources.map { it.label })
        assertEquals(listOf("Normal", "Screen"), editor.blends)
        assertEquals(listOf(LayerFamily("fractals", listOf("julia", "mandel"))), editor.families)
        assertEquals(listOf(LayerChoice("1", "Glow"), LayerChoice("2", "Heat")), editor.knobs)
    }

    @Test
    fun noRowsIsTheLayerOff() {
        assertEquals(LayersEditor(), LayersEditor.parse("garbage\n\n"))
        assertEquals("", LayersEditor.parse("state|1|USER||0|1|0.5").styleId)
    }
}
