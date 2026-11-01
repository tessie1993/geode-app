package dev.geode.export

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The Cancel button must stop the render even when the UI that presses it is not the one that
 * started it (the Activity was recreated and re-attached to a running render). Cancel therefore
 * goes through [ExportRun], which holds the render's job, rather than a controller's own flag.
 */
class ExportRunTest {
    @After
    fun tearDown() {
        ExportRun.finish()
    }

    @Test
    fun `requestCancel cancels the running job and raises the flag the frame loops poll`() =
        runBlocking {
            ExportRun.begin("track.mp3")
            val started = CompletableDeferred<Unit>()
            val finished = CompletableDeferred<Unit>()
            val job =
                ExportRun.launch {
                    try {
                        started.complete(Unit)
                        awaitCancellation()
                    } finally {
                        ExportRun.finish()
                        finished.complete(Unit)
                    }
                }
            withTimeout(TIMEOUT_MS) { started.await() }
            assertTrue(ExportRun.running)

            // No reference to the job or to whoever launched it: the rebuilt UI's position.
            ExportRun.requestCancel()

            withTimeout(TIMEOUT_MS) { finished.await() }
            assertTrue(job.isCancelled)
            assertTrue(ExportRun.cancelRequested)
            assertFalse("finish() in the render's finally ends the run", ExportRun.running)
        }

    @Test
    fun `a new render starts with the flag down`() {
        ExportRun.begin("a")
        ExportRun.requestCancel()
        ExportRun.finish()
        ExportRun.begin("b")
        assertFalse(ExportRun.cancelRequested)
    }

    private companion object {
        const val TIMEOUT_MS = 5_000L
    }
}
