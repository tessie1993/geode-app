package dev.geode.playback

import dev.geode.data.SessionStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

/** #877: no saved session resumes nothing, never the last history track at 0:00. */
class ResumeSourceTest {
    private val album = (1..4).map { SessionStore.SavedTrack("content://media/$it", "Track $it", "Artist") }

    @Test
    fun missingOrQuarantinedSessionResumesNothing() {
        assertSame(ResumeSource.Nothing, ResumeSource.of(null))
    }

    @Test
    fun emptyQueueResumesNothing() {
        assertSame(ResumeSource.Nothing, ResumeSource.of(SessionStore.Saved(emptyList(), 0, 5_000L)))
    }

    @Test
    fun midTrackSessionComesBackWithTheSameQueueTrackAndPosition() {
        val saved = SessionStore.Saved(album, 2, 91_500L)
        assertEquals(ResumeSource.Queue(saved), ResumeSource.of(saved))
    }

    @Test
    fun outOfRangeIndexAndNegativePositionAreClamped() {
        val source = ResumeSource.of(SessionStore.Saved(album, 9, -40L)) as ResumeSource.Queue
        assertEquals(3, source.saved.index)
        assertEquals(0L, source.saved.positionMs)
    }
}
