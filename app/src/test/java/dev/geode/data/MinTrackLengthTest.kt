package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MinTrackLengthTest {
    @Test
    fun defaultHidesUnderThirtySeconds() {
        assertEquals(30, MinTrackLength.DEFAULT_SECONDS)
        assertFalse(MinTrackLength.keeps(3_000L, MinTrackLength.DEFAULT_SECONDS))
        assertFalse(MinTrackLength.keeps(29_999L, MinTrackLength.DEFAULT_SECONDS))
        assertTrue(MinTrackLength.keeps(30_000L, MinTrackLength.DEFAULT_SECONDS))
        assertTrue(MinTrackLength.keeps(215_000L, MinTrackLength.DEFAULT_SECONDS))
    }

    @Test
    fun zeroListsEverything() {
        assertTrue(MinTrackLength.keeps(1L, 0))
        assertTrue(MinTrackLength.keeps(1L, -5))
    }

    @Test
    fun aChangedThresholdApplies() {
        assertFalse(MinTrackLength.keeps(59_000L, 60))
        assertTrue(MinTrackLength.keeps(12_000L, 10))
    }

    @Test
    fun unknownLengthIsAlwaysKept() {
        assertTrue(MinTrackLength.keeps(0L, 30))
        assertTrue(MinTrackLength.keeps(-1L, 120))
    }

    @Test
    fun choicesOfferOffAndTheDefault() {
        assertTrue(0 in MinTrackLength.CHOICES)
        assertTrue(MinTrackLength.DEFAULT_SECONDS in MinTrackLength.CHOICES)
    }
}
