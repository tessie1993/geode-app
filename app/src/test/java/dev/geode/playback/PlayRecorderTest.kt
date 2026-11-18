package dev.geode.playback

import androidx.media3.common.Player
import androidx.media3.common.Timeline
import dev.geode.data.SessionStore
import kotlinx.coroutines.test.TestScope
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * #371: with no UI session alive, the playback session alone must keep the resumption queue and the
 * play history current. Before the fix only the UI's poll loop and listener wrote them, so this
 * scenario (Back on track 1 at 0:30, listen through track 6 from the notification, process dies)
 * resumed at track 1, 0:30 and history missed tracks 2 to 6.
 */
class PlayRecorderTest {
    private val album = (1..6).map { SessionStore.SavedTrack("content://media/$it", "Track $it", "Artist") }

    /** The player as the recorder reads it: an album, a current index and position. */
    private class FakeSource(
        val tracks: List<SessionStore.SavedTrack>,
    ) : PlayRecorder.Source {
        var index = 0
        var positionMs = 0L
        var wantsToPlay = true
        var queueReads = 0

        override fun current() = tracks[index]

        override fun queue(): List<SessionStore.SavedTrack> {
            queueReads++
            return tracks
        }

        override fun index() = index

        override fun positionMs() = positionMs

        override fun wantsToPlay() = wantsToPlay
    }

    /** The plays as [dev.geode.data.HistoryStore] would get them, in order; its rules are native's (core/library/History). */
    private class FakeHistory : PlayRecorder.History {
        val plays = mutableListOf<String>()

        override fun recordPlay(
            uri: String,
            title: String,
            artist: String,
        ) {
            plays.add(uri)
        }

        override fun flush() = Unit
    }

    @Test
    fun restoredQueueIsNotAPlayUntilPlaybackStarts() {
        val source = FakeSource(album).also { it.wantsToPlay = false }
        val history = FakeHistory()
        val recorder = PlayRecorder(source, history, TestScope()) { }

        // The saved queue comes back paused: nothing was listened to yet.
        recorder.onMediaItemTransition(null, Player.MEDIA_ITEM_TRANSITION_REASON_PLAYLIST_CHANGED)
        assertEquals(0, recorder.plays.value)
        assertEquals(emptyList<String>(), history.plays)

        // Pressing play counts it, once.
        recorder.onIsPlayingChanged(true)
        recorder.onIsPlayingChanged(false)
        recorder.onIsPlayingChanged(true)
        assertEquals(1, recorder.plays.value)
        assertEquals(listOf(album[0].uri), history.plays)
    }

    @Test
    fun ticksAndSeeksReuseTheQueueAndATimelineChangeRereadsIt() {
        val source = FakeSource(album)
        val saved = mutableListOf<SessionStore.Saved>()
        val recorder = PlayRecorder(source, FakeHistory(), TestScope()) { saved.add(it) }

        recorder.onTimelineChanged(Timeline.EMPTY, Player.TIMELINE_CHANGE_REASON_PLAYLIST_CHANGED)
        assertEquals(1, source.queueReads)

        source.positionMs = 5_000
        recorder.onIsPlayingChanged(false)
        source.positionMs = 9_000
        recorder.onIsPlayingChanged(false)
        assertEquals(1, source.queueReads)
        assertEquals(listOf(0L, 5_000L, 9_000L), saved.map { it.positionMs })

        recorder.onTimelineChanged(Timeline.EMPTY, Player.TIMELINE_CHANGE_REASON_SOURCE_UPDATE)
        assertEquals(2, source.queueReads)
    }

    @Test
    fun serviceAloneKeepsResumptionAndHistoryCurrent() {
        val source = FakeSource(album)
        val history = FakeHistory()
        val writes = mutableListOf<SessionStore.Saved>()
        val recorder = PlayRecorder(source, history, TestScope()) { writes.add(it) }

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

        // The last session written: what PlaybackService.onPlaybackResumption reads back after process death (the
        // file's round trip is native's: library_session_test).
        val saved = writes.last()
        assertEquals(album, saved.tracks)
        assertEquals(5, saved.index)
        assertEquals(42_000L, saved.positionMs)

        // History saw every track, in the order played (Recent lists them newest first: library_history_test).
        assertEquals(album.map { it.uri }, history.plays)
        assertEquals(6, recorder.plays.value)
    }
}
