package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TabLabelsTest {
    @Test
    fun autoIsTheBarAsBefore() {
        assertTrue(TabLabels.AUTO.shows(selected = false, roomIsShort = false))
        assertTrue(TabLabels.AUTO.shows(selected = true, roomIsShort = true))
        assertFalse(TabLabels.AUTO.shows(selected = false, roomIsShort = true))
    }

    @Test
    fun theFixedChoicesIgnoreTheRoom() {
        for (short in listOf(false, true)) {
            assertTrue(TabLabels.ALWAYS.shows(selected = false, roomIsShort = short))
            assertTrue(TabLabels.SELECTED.shows(selected = true, roomIsShort = short))
            assertFalse(TabLabels.SELECTED.shows(selected = false, roomIsShort = short))
            assertFalse(TabLabels.NEVER.shows(selected = true, roomIsShort = short))
        }
    }

    @Test
    fun anUnknownOrMissingNameIsAuto() {
        assertEquals(TabLabels.AUTO, TabLabels.fromName(null))
        assertEquals(TabLabels.AUTO, TabLabels.fromName("SOMETIMES"))
        assertEquals(TabLabels.NEVER, TabLabels.fromName("NEVER"))
    }
}
