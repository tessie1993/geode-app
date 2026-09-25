package dev.geode.playback

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PlaybackFocusStateTest {
    @Test
    fun delayedFocusNeverPlaysUntilGain() {
        val focus = PlaybackFocusState()
        val delayed = focus.request(PlaybackFocusState.RequestResult.DELAYED)
        assertTrue(delayed.playRequested)
        assertFalse(delayed.canPlay)
        assertTrue(focus.change(PlaybackFocusState.Change.GAIN).canPlay)
    }

    @Test
    fun deniedFocusDoesNotLeavePlaybackArmed() {
        val focus = PlaybackFocusState()
        assertFalse(focus.request(PlaybackFocusState.RequestResult.DENIED).playRequested)
        assertFalse(focus.change(PlaybackFocusState.Change.GAIN).canPlay)
    }

    @Test
    fun transientLossResumesOnlyWhileUserStillWantsPlayback() {
        val focus = PlaybackFocusState()
        focus.request(PlaybackFocusState.RequestResult.GRANTED)
        val interrupted = focus.change(PlaybackFocusState.Change.TRANSIENT_LOSS)
        assertTrue(interrupted.playRequested)
        assertFalse(interrupted.canPlay)
        assertTrue(focus.change(PlaybackFocusState.Change.GAIN).canPlay)
        focus.change(PlaybackFocusState.Change.TRANSIENT_LOSS)
        focus.pause()
        assertFalse(focus.change(PlaybackFocusState.Change.GAIN).canPlay)
    }

    @Test
    fun permanentLossRequiresAnExplicitNewPlayRequest() {
        val focus = PlaybackFocusState()
        focus.request(PlaybackFocusState.RequestResult.GRANTED)
        assertFalse(focus.change(PlaybackFocusState.Change.LOSS).playRequested)
        assertFalse(focus.change(PlaybackFocusState.Change.GAIN).canPlay)
        assertTrue(focus.request(PlaybackFocusState.RequestResult.GRANTED).canPlay)
    }

    @Test
    fun duckingUsesAMultiplierAndGainRestoresFullUserVolume() {
        val focus = PlaybackFocusState()
        focus.request(PlaybackFocusState.RequestResult.GRANTED)
        val ducked = focus.change(PlaybackFocusState.Change.DUCK)
        assertTrue(ducked.canPlay)
        assertEquals(0.2f, ducked.volumeMultiplier, 0f)
        assertEquals(1f, focus.change(PlaybackFocusState.Change.GAIN).volumeMultiplier, 0f)
    }

    @Test
    fun pauseCancelsDelayedFocusAndIgnoresStaleDucking() {
        val focus = PlaybackFocusState()
        focus.request(PlaybackFocusState.RequestResult.DELAYED)
        focus.pause()
        assertFalse(focus.change(PlaybackFocusState.Change.GAIN).canPlay)
        assertFalse(focus.change(PlaybackFocusState.Change.DUCK).canPlay)
        assertEquals(1f, focus.state.volumeMultiplier, 0f)
    }
}
