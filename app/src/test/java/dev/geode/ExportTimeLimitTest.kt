package dev.geode

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** A render Android stops at its time limit ends cancelled, and must not look like the user's Cancel. */
class ExportTimeLimitTest {
    @Test
    fun theLimitCancelsTheRenderAndIsRememberedWhileItWindsDown() {
        var cancels = 0
        ExportTimeLimit.reached(running = true, cancel = { cancels++ }, nowMs = 1_000_000L)
        assertEquals(1, cancels)
        assertTrue(ExportTimeLimit.justReached(nowMs = 1_000_000L))
        assertTrue("reading it does not clear it", ExportTimeLimit.justReached(nowMs = 1_000_000L + ExportTimeLimit.WINDOW_MS - 1))
    }

    @Test
    fun theMarkExpiresSoALaterCancelIsTheUsers() {
        ExportTimeLimit.reached(running = true, cancel = {}, nowMs = 2_000_000L)
        assertFalse(ExportTimeLimit.justReached(nowMs = 2_000_000L + ExportTimeLimit.WINDOW_MS))
    }

    @Test
    fun aLimitWithNoRenderRunningStillCancelsButLeavesNoMark() {
        var cancels = 0
        ExportTimeLimit.reached(running = false, cancel = { cancels++ }, nowMs = 9_000_000L)
        assertEquals(1, cancels)
        assertFalse(ExportTimeLimit.justReached(nowMs = 9_000_000L))
    }
}
