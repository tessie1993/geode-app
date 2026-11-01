package dev.geode.pro

import dev.geode.render.scene.ParamKeys
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Exactly one free control per simple Customize page, whatever the style shows. */
class FreeTierCustomizeTest {
    @Test
    fun `look and events each give away one control`() {
        assertEquals(ParamKeys.PALETTE, FreeTier.FREE_LOOK_CONTROL)
        assertEquals("Event recipes", FreeTier.FREE_EVENTS_CONTROL)
    }

    @Test
    fun `a style page frees its most specific control and nothing else`() {
        val cymatics = setOf(ParamKeys.GEOMETRY, ParamKeys.KALEIDOSCOPE, ParamKeys.MORPH)
        assertEquals(ParamKeys.GEOMETRY, FreeTier.freeStyleControl { it in cymatics })

        val fluid = setOf(ParamKeys.FLUID_CURL, ParamKeys.PARTICLE_SHAPE, ParamKeys.KALEIDOSCOPE)
        assertEquals(ParamKeys.FLUID_CURL, FreeTier.freeStyleControl { it in fluid })

        val shader = setOf(ParamKeys.KALEIDOSCOPE, ParamKeys.MORPH, ParamKeys.ENDLESS_ZOOM)
        assertEquals(ParamKeys.KALEIDOSCOPE, FreeTier.freeStyleControl { it in shader })
    }

    @Test
    fun `the free set is one control per simple page, plus Look's note blooms`() {
        val shown = setOf(ParamKeys.PARTICLE_SHAPE, ParamKeys.KALEIDOSCOPE)
        assertEquals(
            setOf(ParamKeys.PALETTE, FreeTier.NOTE_BLOOMS_CONTROL, "Event recipes", ParamKeys.PARTICLE_SHAPE),
            FreeTier.freeCustomizeControls { it in shown },
        )
    }

    @Test
    fun `a style with none of the candidates frees nothing on its page`() {
        assertNull(FreeTier.freeStyleControl { false })
        assertEquals(
            setOf(ParamKeys.PALETTE, FreeTier.NOTE_BLOOMS_CONTROL, "Event recipes"),
            FreeTier.freeCustomizeControls { false },
        )
    }
}
