package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The batch queue's bookkeeping; which tracks a batch renders and its progress are native (core/tests/render_plan_test.cpp). */
class RenderBatchTest {
    private val a = BatchTrack("content://a", "A")
    private val b = BatchTrack("content://b", "B")
    private val c = BatchTrack("content://c", "C")

    @Test
    fun onlyTheCurrentTrackIsNotABatch() {
        assertFalse(RenderScope.CURRENT_TRACK.isBatch)
        assertTrue(RenderScope.entries.filter { it != RenderScope.CURRENT_TRACK }.all { it.isBatch })
    }

    @Test
    fun aFailedTrackIsSkippedAndTheRestStillRender() {
        var batch = RenderBatch(listOf(a, b, c))
        assertEquals(a, batch.current)

        batch = batch.record(BatchOutcome.Saved("content://out/1", "beatrealms_1.mp4"))
        assertEquals(b, batch.current)
        batch = batch.record(BatchOutcome.Failed("unreadable"))
        assertEquals(c, batch.current)
        batch = batch.record(BatchOutcome.Saved("content://out/3", "beatrealms_3.mp4"))

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
    fun recordingPastTheEndChangesNothing() {
        val done = RenderBatch(listOf(a)).record(BatchOutcome.Failed("x"))

        assertEquals(done, done.record(BatchOutcome.Saved("content://late", null)))
    }
}
