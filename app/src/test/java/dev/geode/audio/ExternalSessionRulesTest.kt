package dev.geode.audio

import android.media.session.PlaybackState
import dev.geode.audio.ExternalSessionRules.Candidate
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ExternalSessionRulesTest {
    private val own = "dev.geode"
    private val playing = PlaybackState.STATE_PLAYING
    private val paused = PlaybackState.STATE_PAUSED

    @Test
    fun `never the app's own session, even when it is the one playing`() {
        val sessions = listOf(Candidate(own, playing), Candidate("a.music", paused))
        assertEquals(1, ExternalSessionRules.choose(sessions, own, null))
    }

    @Test
    fun `none when only the app's own session is active`() {
        assertEquals(-1, ExternalSessionRules.choose(listOf(Candidate(own, playing)), own, null))
        assertEquals(-1, ExternalSessionRules.choose(emptyList(), own, null))
    }

    @Test
    fun `a playing session wins over one listed first`() {
        val sessions = listOf(Candidate("a.music", paused), Candidate("b.music", playing))
        assertEquals(1, ExternalSessionRules.choose(sessions, own, null))
    }

    @Test
    fun `the shown session stays while it plays, though another plays too`() {
        val sessions = listOf(Candidate("a.music", playing), Candidate("b.music", playing))
        assertEquals(1, ExternalSessionRules.choose(sessions, own, "b.music"))
    }

    @Test
    fun `pausing the shown session does not hand the transport to another paused app`() {
        val sessions = listOf(Candidate("a.music", paused), Candidate("b.music", paused))
        assertEquals(1, ExternalSessionRules.choose(sessions, own, "b.music"))
    }

    @Test
    fun `another app starting to play takes over from a paused one`() {
        val sessions = listOf(Candidate("a.music", playing), Candidate("b.music", paused))
        assertEquals(0, ExternalSessionRules.choose(sessions, own, "b.music"))
    }

    @Test
    fun `with nothing playing and nothing shown the platform's first is taken`() {
        val sessions = listOf(Candidate("a.music", null), Candidate("b.music", paused))
        assertEquals(0, ExternalSessionRules.choose(sessions, own, null))
    }

    @Test
    fun `buffering counts as playing`() {
        assertTrue(ExternalSessionRules.isPlaying(PlaybackState.STATE_BUFFERING))
        assertFalse(ExternalSessionRules.isPlaying(PlaybackState.STATE_STOPPED))
        assertFalse(ExternalSessionRules.isPlaying(null))
    }

    @Test
    fun `each button follows the action the session declares`() {
        val all =
            PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or PlaybackState.ACTION_SKIP_TO_NEXT or
                PlaybackState.ACTION_SKIP_TO_PREVIOUS or PlaybackState.ACTION_SEEK_TO
        assertEquals(ExternalControls(true, true, true, true), ExternalSessionRules.controls(all, playing, 200_000L))
        val noSkips = PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE
        assertEquals(ExternalControls(playPause = true), ExternalSessionRules.controls(noSkips, paused, 200_000L))
    }

    @Test
    fun `seeking needs the action and a known length`() {
        val seek = PlaybackState.ACTION_SEEK_TO
        assertFalse(ExternalSessionRules.controls(seek, playing, 0L).seek)
        assertTrue(ExternalSessionRules.controls(seek, playing, 1_000L).seek)
    }

    @Test
    fun `no playback state, no buttons`() {
        assertEquals(ExternalControls(), ExternalSessionRules.controls(-1L, null, 1_000L))
    }

    @Test
    fun `play pause asks for what the session is not doing`() {
        val both = PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE
        assertEquals(ExternalToggle.PAUSE, ExternalSessionRules.toggle(both, playing))
        assertEquals(ExternalToggle.PLAY, ExternalSessionRules.toggle(both, paused))
    }

    @Test
    fun `a session that only takes its play pause key gets the key`() {
        assertEquals(ExternalToggle.PLAY_PAUSE_KEY, ExternalSessionRules.toggle(PlaybackState.ACTION_PLAY_PAUSE, playing))
    }

    @Test
    fun `a playing session that cannot pause leaves the button off`() {
        assertEquals(ExternalToggle.NONE, ExternalSessionRules.toggle(PlaybackState.ACTION_PLAY, playing))
        assertFalse(ExternalSessionRules.controls(PlaybackState.ACTION_PLAY, playing, 0L).playPause)
    }

    @Test
    fun `the playhead moves on from the reported position while playing`() {
        assertEquals(12_000L, ExternalSessionRules.positionAt(10_000L, 5_000L, 1f, true, 0L, 7_000L))
        assertEquals(14_000L, ExternalSessionRules.positionAt(10_000L, 5_000L, 2f, true, 0L, 7_000L))
    }

    @Test
    fun `a paused playhead stays where it was reported`() {
        assertEquals(10_000L, ExternalSessionRules.positionAt(10_000L, 5_000L, 1f, false, 0L, 9_000L))
    }

    @Test
    fun `the playhead stops at the track's end and an unknown one reads zero`() {
        assertEquals(11_000L, ExternalSessionRules.positionAt(10_000L, 5_000L, 1f, true, 11_000L, 9_000L))
        assertEquals(0L, ExternalSessionRules.positionAt(PlaybackState.PLAYBACK_POSITION_UNKNOWN, 5_000L, 1f, true, 0L, 9_000L))
    }
}
