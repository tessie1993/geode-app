package dev.geode.ui.cast

import dev.geode.R
import dev.geode.cast.CastFailure
import dev.geode.cast.CastState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CastButtonUiTest {
    @Test
    fun connectingSpinsAndNamesTheTv() {
        val ui = castButtonUi(CastState.Connecting("Living room"))
        assertTrue(ui.busy)
        assertEquals(R.string.cast_connecting, ui.status)
        assertEquals("Living room", ui.deviceName)
        assertEquals(R.string.cast_connecting_unnamed, castButtonUi(CastState.Connecting(null)).status)
    }

    @Test
    fun castingIsStill() {
        val ui = castButtonUi(CastState.Casting("Living room"))
        assertFalse(ui.busy)
        assertEquals(R.string.cast_casting, ui.status)
        assertEquals(R.string.cast_casting_unnamed, castButtonUi(CastState.Casting(null)).status)
    }

    @Test
    fun endStatesAddNothingToTheButton() {
        listOf(CastState.Idle, CastState.Locked, CastState.TrialEnded, CastState.Failed(CastFailure.STREAM)).forEach {
            assertEquals(CastButtonUi(), castButtonUi(it))
        }
    }

    @Test
    fun eachFailureHasItsOwnMessage() {
        assertEquals(R.string.cast_failed, castMessage(CastState.Failed(CastFailure.STREAM)))
        assertEquals(R.string.cast_failed_no_lan, castMessage(CastState.Failed(CastFailure.NO_LAN)))
        assertEquals(R.string.cast_failed_background, castMessage(CastState.Failed(CastFailure.BACKGROUND_START)))
        assertEquals(R.string.cast_failed_network, castMessage(CastState.Failed(CastFailure.NETWORK_CHANGED)))
        assertEquals(R.string.cast_failed_tv_play, castMessage(CastState.Failed(CastFailure.TV_COULD_NOT_PLAY)))
        assertEquals(R.string.cast_failed_tv_unreachable, castMessage(CastState.Failed(CastFailure.TV_UNREACHABLE)))
    }

    @Test
    fun lockedAndTrialEndedLeaveNoMessageHere() {
        // Locked opens the paywall; the trial's end is told by CastService and the paywall's own notice.
        assertNull(castMessage(CastState.Locked))
        assertNull(castMessage(CastState.TrialEnded))
        assertNull(castMessage(CastState.Casting(null)))
    }
}
