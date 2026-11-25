package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The Song events page keeps no list of its own: it splits the lines native hands over
 * (core/viz/modulation/ResponseCatalog.hpp names them, core/tests/response_catalog_test holds their content).
 * What is checked here is only the split.
 */
class ResponseCatalogueTest {
    private val text =
        """
        responses|1
        family|0|Appear
        family|1|Travel
        preset|0|Snap|0|0|0.15
        preset|1|Pulse|0.05|0.1|0.5
        curve|0|Linear|Follows the hit strength one to one.
        drive|0|Once|Plays once when it happens, then ends on its own.
        category|0|Instruments
        row|0|KICK|Kick|0|0
        row|10|BEAT|Beat|1|1
        entry|0|0|1|Pop burst|Dots burst out and fade
        entry|1|1|3|Pull in|Shapes gather toward a point
        kind|1|Things
        control|1|14|Shape|5|0|3|1|Dot|Ring|Shard|Blob
        control|1|17|Speed|1|0|1|0.05
        common|6|Fade in|4|0|4|0.05
        """.trimIndent()

    @Test
    fun theLinesSplitIntoWhatThePageDraws() {
        val c = ResponseCatalogue.parse(text)
        assertEquals(listOf("Appear", "Travel"), c.families)
        assertEquals(listOf("Snap", "Pulse"), c.presets)
        assertEquals(ResponseWord("Linear", "Follows the hit strength one to one."), c.curves.single())
        assertEquals(listOf("Instruments"), c.categories)
        assertEquals(EventRowInfo(10, "Beat", 1, true), c.rows[1])
        assertTrue(!c.rows[0].needsBeat)
        assertEquals(ResponseTile(1, 1, "Pull in", "Shapes gather toward a point"), c.tiles[1])
    }

    @Test
    fun aKindsControlsKeepTheirUnitRangeAndChoices() {
        val c = ResponseCatalogue.parse(text)
        val shape = c.controlsOf(1)[0]
        assertEquals(14, shape.field)
        assertEquals(ResponseCatalogue.UNIT_CHOICE, shape.unit)
        assertEquals(listOf("Dot", "Ring", "Shard", "Blob"), shape.choices)
        assertEquals(0f..3f, shape.range)
        assertEquals(0.05f, c.controlsOf(1)[1].step, 0f)
        assertTrue(c.controlsOf(7).isEmpty())
        assertEquals("Fade in", c.common.single().label)
    }

    @Test
    fun noTextIsAnEmptyCatalogue() {
        val c = ResponseCatalogue.parse("")
        assertTrue(c.tiles.isEmpty() && c.rows.isEmpty() && c.common.isEmpty())
    }
}
