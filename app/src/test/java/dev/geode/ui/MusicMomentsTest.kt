package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Customize › Music's enums against native's catalog rows (core/tests/mod_catalog_music.txt, which
 * core/tests/mod_document_test proves equal to what C++ emits). The enums' ordinals are the wire values of
 * native's `Moment` and `MusicAction`, so a reorder on either side shows here; the menu's order and groups
 * are native's alone.
 */
class MusicMomentsTest {
    @Test
    fun everyMomentIsNativesInWireOrder() {
        val rows = ModCatalog.words("moment")
        assertEquals(MusicMoment.entries.map { it.ordinal to it.name }, rows.map { it.ordinal to it.name })
    }

    @Test
    fun everyActionIsNativesByWireOrdinal() {
        val rows = ModCatalog.words("music")
        assertEquals(MusicAction.entries.size, rows.size)
        rows.forEach { row -> assertEquals(row.toString(), row.name, MusicAction.entries[row.ordinal].name) }
    }

    @Test
    fun theMenuTakesNativesOrderAndGroups() {
        val chips = MusicMoments.chips(ALL)
        assertEquals(ModCatalog.words("music").map { MusicAction.entries[it.ordinal] }, chips.map { it.action })
        assertEquals(listOf("LIGHT", "MOTION"), chips.map { it.group }.distinct())
        val noFlash = MusicMoments.chips(ALL and (1 shl MusicAction.FLASH.ordinal).inv())
        assertEquals(chips.filter { it.action != MusicAction.FLASH }, noFlash)
    }

    private companion object {
        const val ALL = -1
    }
}
