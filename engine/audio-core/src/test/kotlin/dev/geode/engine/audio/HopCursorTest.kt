package dev.geode.engine.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HopCursorTest {
    private val hop = 512
    private val window = 2048

    /** Feeds [cursor] a source writing [chunk] frames every [every] wakes for [wakes] wakes; returns frames fed. */
    private fun drive(
        cursor: HopCursor,
        chunk: Int,
        every: Int,
        wakes: Int,
        start: Long = 0L,
    ): Pair<Long, Long> {
        var written = start
        var fed = 0L
        repeat(wakes) { i ->
            if (i % every == 0) written += chunk
            val step = cursor.plan(written, epoch = 1)
            fed += step.frames
            assertFalse("audio is arriving, so it is not silence", step.silence)
        }
        return fed to written
    }

    @Test
    fun startsWithOneWindowOfTheNewestAudio() {
        val cursor = HopCursor(hop, window)
        assertEquals(0, cursor.plan(writtenFrames = window - 1L, epoch = 1).frames)
        val first = cursor.plan(writtenFrames = 10_000L, epoch = 1)
        assertEquals(10_000L - window, first.fromFrame)
        assertEquals(window, first.frames)
    }

    @Test
    fun consumesExactlyTheAudioThatArrivesInWholeHops() {
        // A decoder that writes 1024 frames every other wake, i.e. exactly one hop per wake on average.
        val cursor = HopCursor(hop, window)
        val (fed, written) = drive(cursor, chunk = 1024, every = 2, wakes = 4000, start = window.toLong())
        // Everything but the last partial delivery has been fed, never more than was written.
        assertTrue(fed <= written)
        assertTrue("lag ${written - fed} frames", written - fed <= 1024 + hop)
        assertEquals(0L, fed % hop)
    }

    @Test
    fun catchesUpWhenTheSourceRunsFasterThanTheWakes() {
        // Two hops per wake arrive (playback at 2x speed, or a slow loop). The cursor keeps up by
        // feeding extra hops instead of letting the lag grow.
        val cursor = HopCursor(hop, window)
        val (fed, written) = drive(cursor, chunk = 2 * hop, every = 1, wakes = 2000, start = window.toLong())
        assertTrue("lag ${written - fed} frames", written - fed <= 3L * hop)
    }

    @Test
    fun waitsThroughANormalDeliveryGapAndCallsALongerOneSilence() {
        val cursor = HopCursor(hop, window)
        var written = window.toLong()
        cursor.plan(written, epoch = 1)
        // FLAC-sized deliveries: 4096 frames every 8 wakes. The gaps between them are not silence.
        repeat(80) { i ->
            if (i % 8 == 0) written += 4096
            assertFalse(cursor.plan(written, epoch = 1).silence)
        }
        // Then the source stops (pause). Within a few delivery gaps the cursor reports silence.
        var silentAfter = -1
        for (i in 0 until 64) {
            if (cursor.plan(written, epoch = 1).silence) {
                silentAfter = i
                break
            }
        }
        assertTrue("silence after $silentAfter wakes", silentAfter in 1..24)
    }

    @Test
    fun aNewEpochContinuesFromItsFirstFrame() {
        // A gapless track change starts a new ring epoch mid-run; its audio follows straight on
        // from the last track's, so the cursor picks it up at frame 0 rather than skipping ahead.
        val cursor = HopCursor(hop, window)
        cursor.plan(50_000L, epoch = 1)
        val next = cursor.plan(writtenFrames = 700L, epoch = 2)
        assertEquals(0L, next.fromFrame)
        assertEquals(hop, next.frames)
    }

    @Test
    fun restartJumpsToTheNewestWindow() {
        val cursor = HopCursor(hop, window)
        cursor.plan(50_000L, epoch = 1)
        cursor.restart()
        val next = cursor.plan(writtenFrames = 60_000L, epoch = 1)
        assertEquals(60_000L - window, next.fromFrame)
        assertEquals(window, next.frames)
    }

    @Test
    fun audioThatWasOverwrittenIsSkippedNotRead() {
        val cursor = HopCursor(hop, window)
        cursor.plan(10_000L, epoch = 1)
        // The analysis stalled while the ring lapped it; only the newest window is still there.
        val step = cursor.plan(writtenFrames = 200_000L, epoch = 1, oldestAvailable = 150_000L)
        assertEquals(200_000L - window, step.fromFrame)
        assertEquals(window, step.frames)
    }
}
