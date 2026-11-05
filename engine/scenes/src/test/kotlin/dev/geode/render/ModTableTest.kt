package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The shell's read of the native modulation-target table (core/tests/mod_targets.txt, which
 * mod_targets_test proves equal to what C++ emits). The rules themselves are tested natively; this
 * checks the enums still name the native rows in wire order and the reader picks the right columns.
 */
class ModTableTest {
    @Test
    fun enumsNameTheNativeRowsInWireOrder() {
        assertEquals(ModTable.names("target"), LfoTarget.entries.map { it.name })
        assertEquals(ModTable.names("source"), ModSource.entries.map { it.name })
        assertEquals(ModTable.names("polarity"), ModPolarity.entries.map { it.name })
        assertEquals(ModTable.names("curve"), ModCurve.entries.map { it.name })
        assertEquals(ModTable.names("wave"), LfoWave.entries.map { it.name })
    }

    @Test
    fun readsTheColumns() {
        assertFalse(ModTable.offered(LfoTarget.RETIRED_25))
        assertEquals(ModPolarity.POSITIVE, ModTable.naturalPolarity(ModSource.BASS))
        assertEquals(ModPolarity.BIPOLAR, ModTable.naturalPolarity(ModSource.STEREO_PAN))
        assertEquals(ModTable.defaultRateSeconds, LfoConfig().rateSeconds, 0f)
        assertTrue(ModTable.minRateSeconds < ModTable.defaultRateSeconds && ModTable.defaultRateSeconds < ModTable.maxRateSeconds)
    }

    @Test
    fun reachFollowsTheNativeScope() {
        assertTrue(ModTable.reaches(LfoTarget.MORPH, "julia"))
        assertFalse(ModTable.reaches(LfoTarget.MORPH, "silk_web"))
        assertTrue(ModTable.reaches(LfoTarget.LFO2_DEPTH, "milkdrop"))
        assertFalse(ModTable.reaches(LfoTarget.RETIRED_25, "julia"))
    }

    @Test
    fun readsEachPickersOfferMask() {
        assertFalse(ModTable.offeredTo(LfoTarget.LFO1_RATE, "julia", ModTable.REACTION))
        assertTrue(ModTable.offeredTo(LfoTarget.LFO1_RATE, "julia", ModTable.ENVELOPE))
        assertTrue(ModTable.offeredTo(LfoTarget.LFO2_RATE, "julia", 0))
        assertFalse(ModTable.offeredTo(LfoTarget.LFO1_RATE, "julia", 1))
        assertFalse(ModTable.offeredTo(LfoTarget.ZOOM, "julia", 3))
        assertTrue(ModTable.offeredTo(LfoTarget.ZOOM, "not_a_style", 0))
    }
}
