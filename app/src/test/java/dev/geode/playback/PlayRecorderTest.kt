package dev.geode.playback

import android.content.ContextWrapper
import androidx.media3.common.Player
import dev.geode.data.HistoryStore
import dev.geode.data.SessionStore
import kotlinx.coroutines.test.TestScope
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/**
 * #371: with no UI session alive, the playback session alone must keep the resumption queue and the
 * play history current. Before the fix only the UI's poll loop and listener wrote them, so this
 * scenario (Back on track 1 at 0:30, listen through track 6 from the notification, process dies)
 * resumed at track 1, 0:30 and history missed tracks 2 to 6.
 */
class PlayRecorderTest {
    @get:Rule
    val dir = TemporaryFolder()

    private val context =
        object : ContextWrapper(null) {
            override fun getFilesDir(): File = dir.root
        }

    private val album = (1..6).map { SessionStore.SavedTrack("content://media/$it", "Track $it", "Artist") }

    /** The player as the recorder reads it: an album, a current index and position. */
    private class FakeSource(
        val tracks: List<SessionStore.SavedTrack>,
    ) : PlayRecorder.Source {
        var index = 0
        var positionMs = 0L

        override fun current() = tracks[index]

        override fun snapshot() = SessionStore.Saved(tracks, index, positionMs)
    }

    @Test
    fun serviceAloneKeepsResumptionAndHistoryCurrent() {
        val source = FakeSource(album)
        val history = HistoryStore(context)
        val store = SessionStore(context)
        val recorder = PlayRecorder(source, history, TestScope(), store::save)

        // Track 1 starts, then the UI goes away at 0:30; only the playback session is left.
        recorder.onMediaItemTransition(null, Player.MEDIA_ITEM_TRANSITION_REASON_PLAYLIST_CHANGED)
        source.positionMs = 30_000
        // Tracks 2 to 6 play out from the notification.
        for (i in 1..5) {
            source.index = i
            source.positionMs = 0
            recorder.onMediaItemTransition(null, Player.MEDIA_ITEM_TRANSITION_REASON_AUTO)
        }
        source.positionMs = 42_000
        recorder.onIsPlayingChanged(false)

        // What PlaybackService.onPlaybackResumption reads after process death.
        val saved = SessionStore(context).load()!!
        assertEquals(album, saved.tracks)
        assertEquals(5, saved.index)
        assertEquals(42_000L, saved.positionMs)

        // Recent (LibraryTree's RECENT) and history saw every track, newest first.
        assertEquals(album.reversed().map { it.uri }, history.recentlyPlayed(10).map { it.uri })
        assertEquals(6, recorder.plays.value)
    }
}
