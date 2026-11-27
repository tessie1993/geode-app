package dev.geode.ui

import dev.geode.audio.CaptureEnd
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** What a capture Android ends leaves in the state: the reason, until the person resumes or picks another source. */
class CaptureEndedTest {
    private val running = ExternalAudioState(active = true, hasSessionAccess = true, hearingSilence = true, refusedByApp = true)

    @Test
    fun androidsEndingKeepsItsReasonAndNothingElse() {
        val ended = running.ended(CaptureEnd.SCREEN_LOCKED)
        assertFalse(ended.active)
        assertFalse(ended.hearingSilence)
        assertFalse(ended.refusedByApp)
        assertFalse(ended.playing)
        assertEquals(CaptureEnd.SCREEN_LOCKED, ended.ended)
    }

    @Test
    fun aPersonsStopLeavesNoReason() {
        val ended = running.ended(null)
        assertFalse(ended.active)
        assertNull(ended.ended)
        assertFalse(ended.holdsSource)
    }

    @Test
    fun anEndedCaptureStillHoldsTheSoundUntilStopped() {
        val ended = running.ended(CaptureEnd.SYSTEM)
        assertTrue(ended.holdsSource)
        assertFalse(ended.stopped().holdsSource)
        assertNull(ended.stopped().ended)
        assertFalse(ExternalAudioState().holdsSource)
    }
}
