package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/** The portrait bottom sheet's width cap (#864): a phone keeps the whole width, a tablet gets a bounded sheet. */
class VisualsSheetWidthTest {
    @Test
    fun aPhoneKeepsItsWholeWidth() {
        assertEquals(411f, sheetWidthDp(411f), 0f)
    }

    @Test
    fun theCapIsTheMiniPlayersSixHundredForty() {
        assertEquals(640f, SHEET_MAX_WIDTH.value, 0f)
    }

    @Test
    fun aPortraitTabletIsCapped() {
        assertEquals(640f, sheetWidthDp(800f), 0f)
        assertEquals(640f, sheetWidthDp(1280f), 0f)
    }
}
