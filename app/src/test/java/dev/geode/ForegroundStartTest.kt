package dev.geode

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** A service whose startForeground fails stops instead of running on into the system's kill. */
class ForegroundStartTest {
    @Test
    fun aStartThatTakesNeverStops() {
        var stops = 0
        assertTrue(startedInForeground("t", start = {}, stop = { stops++ }))
        assertEquals(0, stops)
    }

    @Test
    fun aStartThatThrowsStopsOnceAndSaysSo() {
        var stops = 0
        val started =
            startedInForeground(
                "t",
                start = { throw IllegalStateException("ForegroundServiceStartNotAllowedException") },
                stop = { stops++ },
            )
        assertFalse(started)
        assertEquals(1, stops)
    }
}
