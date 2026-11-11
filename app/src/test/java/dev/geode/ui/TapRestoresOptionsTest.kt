package dev.geode.ui

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The default tap's way out of full screen (#862): wake first, then bring the options back. */
class TapRestoresOptionsTest {
    @Test
    fun theFirstTapOutOfFullScreenOnlyWakesTheChrome() {
        assertFalse(tapRestoresOptions(chromeAwake = false, sheetShown = false))
    }

    @Test
    fun aTapWhileAwakeAndHiddenBringsTheOptionsBack() {
        assertTrue(tapRestoresOptions(chromeAwake = true, sheetShown = false))
    }

    @Test
    fun aTapWithTheOptionsUpJustWakes() {
        assertFalse(tapRestoresOptions(chromeAwake = true, sheetShown = true))
        assertFalse(tapRestoresOptions(chromeAwake = false, sheetShown = true))
    }
}
