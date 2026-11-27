package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LiveFrameRateTest {
    private val fhd60 = ModeRate(1, 1080, 2400, 60f)
    private val fhd90 = ModeRate(2, 1080, 2400, 90f)
    private val qhd60 = ModeRate(3, 1440, 3200, 60f)
    private val qhd90 = ModeRate(4, 1440, 3200, 90f)
    private val modes = listOf(fhd60, fhd90, qhd60, qhd90)

    @Test
    fun aRateTakesTheModeAtTheCurrentSize() {
        assertEquals(fhd60.id, displayModeFor(modes, fhd90, 60f))
        assertEquals(fhd90.id, displayModeFor(modes, fhd60, 90f))
        assertEquals("a resolution change would blank the panel", qhd90.id, displayModeFor(modes, qhd60, 90f))
    }

    @Test
    fun noRateOrNoModeIsNoPreference() {
        assertEquals(0, displayModeFor(modes, fhd60, 0f))
        assertEquals(0, displayModeFor(modes, fhd60, 120f))
    }

    @Test
    fun theRateIsAModesOwnRate() {
        // Native picks the refresh rate from these modes' own rates, so a 59.94 Hz mode comes back as 59.94.
        val ntsc = ModeRate(9, 1080, 2400, 59.94f)
        assertEquals(ntsc.id, displayModeFor(listOf(ntsc, fhd90), fhd90, 59.94f))
        assertEquals(0, displayModeFor(listOf(ntsc, fhd90), fhd90, 60f))
    }

    @Test
    fun followDeviceIsNoVote() {
        assertTrue(LiveFrameRate(-1, 0f).follows)
        assertTrue(LiveFrameRate(60, 0f).follows)
        assertEquals(false, LiveFrameRate(60, 60f).follows)
    }
}
