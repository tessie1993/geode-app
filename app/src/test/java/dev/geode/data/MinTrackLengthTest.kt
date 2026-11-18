package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Which tracks the minimum hides is native's (`core/tests/library_tracks_test.cpp`); this holds what the screen offers. */
class MinTrackLengthTest {
    @Test
    fun defaultIsThirtySeconds() {
        assertEquals(30, MinTrackLength.DEFAULT_SECONDS)
    }

    @Test
    fun choicesOfferOffAndTheDefault() {
        assertTrue(0 in MinTrackLength.CHOICES)
        assertTrue(MinTrackLength.DEFAULT_SECONDS in MinTrackLength.CHOICES)
    }
}
