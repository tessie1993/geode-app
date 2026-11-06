package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class PresetShelfTest {
    private val entries =
        listOf(
            PresetEntry("Glow", "cymatics", builtIn = true, forThisStyle = true),
            PresetEntry("Ink", "fluid", builtIn = true),
            PresetEntry("Mine A", "cymatics", builtIn = false, folder = "", forThisStyle = true),
            PresetEntry("Party", "fluid", builtIn = false, folder = "Live"),
            PresetEntry("Party", "fluid", builtIn = false, folder = "Live"),
        )

    private fun names(filter: PresetFilter) = PresetFiltering.apply(entries, filter).map { it.name }

    @Test
    fun allHoldsOnlyTheUsersPresetsOnce() {
        assertEquals(listOf("Mine A", "Party"), names(PresetFilter.All))
    }

    @Test
    fun builtInsNeverShowTheyAreStyles() {
        assertEquals(listOf("Mine A"), names(PresetFilter.ThisStyle))
    }

    @Test
    fun folderHoldsOnlyTheUsersPresetsInIt() {
        assertEquals(listOf("Party"), names(PresetFilter.Folder("Live")))
        assertEquals(emptyList<String>(), names(PresetFilter.Folder("Empty")))
    }

    @Test
    fun filtersListShelvesThenFolders() {
        assertEquals(
            listOf(
                PresetFilter.All,
                PresetFilter.ThisStyle,
                PresetFilter.Folder("Live"),
                PresetFilter.Folder("Slow"),
            ),
            PresetFiltering.filters(listOf("Live", "", "Slow", "Live")),
        )
    }

    @Test
    fun appliedPrefersTheLastTappedWhileItIsOnStage() {
        val staged =
            listOf(
                PresetEntry("A", "s", builtIn = true, onStage = true),
                PresetEntry("B", "s", builtIn = false, onStage = true),
                PresetEntry("C", "s", builtIn = false),
            )
        assertEquals("B", PresetFiltering.applied(staged, "B"))
        assertEquals("A", PresetFiltering.applied(staged, "C"))
        assertEquals("A", PresetFiltering.applied(staged, null))
        assertNull(PresetFiltering.applied(staged.map { it.copy(onStage = false) }, "B"))
    }

    @Test
    fun renameRefusesBlankAndTakenNames() {
        val taken = listOf("Glow", "Mine A")
        assertNotNull(PresetFiltering.renameError("  ", "Mine A", taken))
        assertNotNull(PresetFiltering.renameError("glow", "Mine A", taken))
        assertNull(PresetFiltering.renameError("Mine A", "Mine A", taken))
        assertNull(PresetFiltering.renameError("mine a", "Mine A", taken))
        assertNull(PresetFiltering.renameError("Fresh", "Mine A", taken))
    }

    @Test
    fun cleanNameDropsTheLinkSeparator() {
        assertEquals("Night - Rain", PresetFiltering.cleanName("  Night · Rain "))
    }

    @Test
    fun uniqueNameCountsUpPastTakenNames() {
        assertEquals("Glow copy", PresetFiltering.uniqueName("Glow copy", listOf("Glow")))
        assertEquals("Glow copy 3", PresetFiltering.uniqueName("Glow copy", listOf("glow copy", "Glow copy 2")))
    }
}
