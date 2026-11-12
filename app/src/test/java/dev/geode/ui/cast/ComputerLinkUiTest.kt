package dev.geode.ui.cast

import dev.geode.R
import dev.geode.cast.CastFailure
import dev.geode.cast.LinkState
import dev.geode.engine.bridge.GeodeNative
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ComputerLinkUiTest {
    private val live = LinkState.Live("http://192.168.1.23:41237/0123456789abcdef0123456789abcdef/")

    @Test
    fun onlyStartingAndLiveRun() {
        assertTrue(LinkState.Starting.running)
        assertTrue(live.running)
        assertFalse(LinkState.Idle.running)
        assertFalse(LinkState.Ended(GeodeNative.LINK_REASON_STREAM).running)
    }

    @Test
    fun theRowSaysWhatTheShareIsDoing() {
        assertEquals(R.string.link_row_live, linkRowValue(live, locked = false))
        assertEquals(R.string.link_row_starting, linkRowValue(LinkState.Starting, locked = false))
        assertEquals(R.string.link_row_idle, linkRowValue(LinkState.Idle, locked = false))
        assertEquals(R.string.link_row_locked, linkRowValue(LinkState.Idle, locked = true))
        // A share that runs on (a trial that ends waits for the song) still reads as live.
        assertEquals(R.string.link_row_live, linkRowValue(live, locked = true))
    }

    @Test
    fun theStageIconSaysWhetherItShares() {
        assertEquals(R.string.link_button, linkButtonLabel(LinkState.Idle, locked = false))
        assertEquals(R.string.link_row_locked, linkButtonLabel(LinkState.Idle, locked = true))
        assertEquals(R.string.link_button_live, linkButtonLabel(live, locked = false))
        assertEquals(R.string.link_button_live, linkButtonLabel(LinkState.Starting, locked = true))
        assertEquals(R.string.link_button, linkButtonLabel(LinkState.Ended(GeodeNative.LINK_REASON_STREAM), locked = false))
    }

    @Test
    fun theSheetNamesStartingAndLive() {
        assertEquals(R.string.link_sheet_live, linkSheetStatus(live))
        assertEquals(R.string.link_sheet_starting, linkSheetStatus(LinkState.Starting))
        assertNull(linkSheetStatus(LinkState.Idle))
    }

    @Test
    fun eachEndHasItsOwnMessage() {
        assertEquals(R.string.link_ended_stream, linkEndedMessage(GeodeNative.LINK_REASON_STREAM))
        assertEquals(R.string.link_ended_no_lan, linkEndedMessage(GeodeNative.LINK_REASON_NO_LAN))
        assertEquals(R.string.link_ended_background, linkEndedMessage(GeodeNative.LINK_REASON_BACKGROUND))
        assertEquals(R.string.link_ended_network, linkEndedMessage(GeodeNative.LINK_REASON_NETWORK))
        assertEquals(R.string.link_ended_trial, linkEndedMessage(GeodeNative.LINK_REASON_TRIAL))
        assertEquals(R.string.link_ended_stream, linkEndedMessage(0))
    }

    @Test
    fun aCastFailureEndsTheShareForTheSameReason() {
        assertEquals(GeodeNative.LINK_REASON_STREAM, CastFailure.STREAM.linkReason)
        assertEquals(GeodeNative.LINK_REASON_NO_LAN, CastFailure.NO_LAN.linkReason)
        assertEquals(GeodeNative.LINK_REASON_BACKGROUND, CastFailure.BACKGROUND_START.linkReason)
        assertEquals(GeodeNative.LINK_REASON_NETWORK, CastFailure.NETWORK_CHANGED.linkReason)
    }
}
