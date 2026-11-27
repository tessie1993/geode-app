package dev.geode.ui

import dev.geode.audio.CaptureEnd
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** A capture Android ends is said and kept going on the device mix; one the person stops is not. */
class CaptureEndedTest {
    private val running = ExternalAudioState(active = true, hasSessionAccess = true, hearingSilence = true)

    @Test
    fun aLockEndingKeepsListeningOnTheMixAndSaysSo() {
        assertTrue(keepsListeningAfter(CaptureEnd.SCREEN_LOCKED))
        val ending = running.ended(CaptureEnd.SCREEN_LOCKED, onDeviceMix = true)
        assertFalse(ending.state.active)
        assertFalse(ending.state.hearingSilence)
        assertEquals(CaptureEnd.SCREEN_LOCKED, ending.state.ended)
        assertTrue(ending.state.onDeviceMix)
        assertEquals(CaptureNotice.Ended(CaptureEnd.SCREEN_LOCKED, onDeviceMix = true), ending.notice)
    }

    @Test
    fun aRefusedMixStillSaysTheCaptureEnded() {
        val ending = running.ended(CaptureEnd.SYSTEM, onDeviceMix = false)
        assertEquals(CaptureEnd.SYSTEM, ending.state.ended)
        assertFalse(ending.state.onDeviceMix)
        assertEquals(CaptureNotice.Ended(CaptureEnd.SYSTEM, onDeviceMix = false), ending.notice)
    }

    @Test
    fun aPersonsStopIsQuietAndLeavesNoMix() {
        assertFalse(keepsListeningAfter(null))
        val ending = running.ended(null, onDeviceMix = true)
        assertFalse(ending.state.active)
        assertNull(ending.state.ended)
        assertFalse(ending.state.onDeviceMix)
        assertNull(ending.notice)
    }

    @Test
    fun anEndedCaptureStillHoldsTheSoundUntilStopped() {
        val ended = running.ended(CaptureEnd.SCREEN_LOCKED, onDeviceMix = true).state
        assertTrue(ended.holdsSource)
        assertFalse(ended.stopped().holdsSource)
        assertNull(ended.stopped().ended)
        assertFalse(ExternalAudioState().holdsSource)
    }
}
