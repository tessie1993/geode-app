package dev.geode.pro

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Customize's free tier: every category's Basics, nothing below them. */
class FreeTierCustomizeTest {
    @Test
    fun `the free controls are exactly the Basics handed in`() {
        val basics = setOf("Palette", "Speed", "@macros")
        assertEquals(basics, FreeTier.freeCustomizeControls(basics))
        assertTrue(FreeTier.freeCustomizeControls(emptySet()).isEmpty())
    }

    @Test
    fun `Basics sections are free and every other section is Pro`() {
        assertTrue(FreeTier.customizeSectionFree(isBasics = true))
        assertFalse(FreeTier.customizeSectionFree(isBasics = false))
    }
}
