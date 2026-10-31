package dev.geode.export

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RenderBatchTest {
    private val a = BatchTrack("content://a", "A")
    private val b = BatchTrack("content://b", "B")
    private val c = BatchTrack("content://c", "C")

    @Test
    fun eachScopeCoversItsOwnTracksOnceInOrder() {
        fun tracks(scope: RenderScope) =
            RenderScope.tracksFor(
                scope,
                current = b,
                queue = listOf(a, b, a, c),
                playlist = listOf(c, a),
                chosen = listOf(b, c, b),
            )

        assertEquals(listOf(b), tracks(RenderScope.CURRENT_TRACK))
        assertEquals(listOf(a, b, c), tracks(RenderScope.PLAY_QUEUE))
        assertEquals(listOf(c, a), tracks(RenderScope.PLAYLIST))
        assertEquals(listOf(b, c), tracks(RenderScope.CHOSEN_TRACKS))
    }

    @Test
    fun nothingPlayingMeansNoCurrentTrack() {
        assertEquals(
            emptyList<BatchTrack>(),
            RenderScope.tracksFor(RenderScope.CURRENT_TRACK, null, listOf(a), emptyList(), emptyList()),
        )
    }

    @Test
    fun onlyTheCurrentTrackIsNotABatch() {
        assertFalse(RenderScope.CURRENT_TRACK.isBatch)
        assertTrue(RenderScope.entries.filter { it != RenderScope.CURRENT_TRACK }.all { it.isBatch })
    }

    @Test
    fun aFailedTrackIsSkippedAndTheRestStillRender() {
        var batch = RenderBatch(listOf(a, b, c))
        assertEquals(a, batch.current)

        batch = batch.record(BatchOutcome.Saved("content://out/1", "muzviz_1.mp4"))
        assertEquals(b, batch.current)
        batch = batch.record(BatchOutcome.Failed("unreadable"))
        assertEquals(c, batch.current)
        batch = batch.record(BatchOutcome.Saved("content://out/3", "muzviz_3.mp4"))

        assertTrue(batch.finished)
        assertNull(batch.current)
        assertEquals(2, batch.savedCount)
        assertEquals(listOf(b to "unreadable"), batch.failures)
        assertEquals("content://out/3", batch.lastSaved?.uri)
    }

    @Test
    fun cancellingMarksEveryTrackNotYetRendered() {
        val batch =
            RenderBatch(listOf(a, b, c))
                .record(BatchOutcome.Saved("content://out/1", null))
                .cancelRest()

        assertTrue(batch.finished)
        assertEquals(1, batch.savedCount)
        assertEquals(2, batch.outcomes.count { it == BatchOutcome.Cancelled })
        assertTrue(batch.failures.isEmpty())
    }

    @Test
    fun progressCountsFinishedTracksPlusTheShareOfTheOneRendering() {
        val batch = RenderBatch(listOf(a, b, c, BatchTrack("content://d", "D"))).record(BatchOutcome.Failed("x"))

        assertEquals(0.25f, batch.progress(0f), 1e-6f)
        assertEquals(0.375f, batch.progress(0.5f), 1e-6f)
        assertEquals(0.5f, batch.progress(2f), 1e-6f)
    }

    @Test
    fun recordingPastTheEndChangesNothing() {
        val done = RenderBatch(listOf(a)).record(BatchOutcome.Failed("x"))

        assertEquals(done, done.record(BatchOutcome.Saved("content://late", null)))
        assertEquals(1f, done.progress(0.7f), 1e-6f)
    }
}
