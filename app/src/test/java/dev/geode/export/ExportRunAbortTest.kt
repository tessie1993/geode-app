package dev.geode.export

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A render only ever learns that it was cancelled. When the cancel came from the render's own
 * surroundings rather than from the user — `ExportService` failing to reach the foreground, say —
 * reporting a plain cancel is the same silence the abort was meant to end, so [ExportRun.finish]
 * substitutes the recorded reason.
 */
class ExportRunAbortTest {
    @After
    fun reset() {
        // ExportRun is a process-wide object; leaving a run or an abort reason behind would leak
        // into whichever test JUnit happens to run next.
        ExportRun.finish(ExportRun.Result.Cancelled)
        ExportRun.clear(ExportRun.Kind.Visualizer)
    }

    @Test
    fun `an aborted run reports why instead of looking like a user cancel`() {
        ExportRun.begin(ExportRun.Kind.Visualizer, "clip")
        ExportRun.abort("the service could not go foreground")

        assertTrue("abort must also stop the render", ExportRun.cancelRequested)
        ExportRun.finish(ExportRun.Result.Cancelled)

        val result = ExportRun.state.value.result
        assertEquals(ExportRun.Result.Failed("the service could not go foreground"), result)
    }

    @Test
    fun `a cancel the user asked for still reports as cancelled`() {
        ExportRun.begin(ExportRun.Kind.Visualizer, "clip")
        ExportRun.requestCancel()
        ExportRun.finish(ExportRun.Result.Cancelled)

        assertEquals(ExportRun.Result.Cancelled, ExportRun.state.value.result)
    }

    @Test
    fun `an abort does not overwrite a real outcome the render already reached`() {
        ExportRun.begin(ExportRun.Kind.Visualizer, "clip")
        ExportRun.abort("the service could not go foreground")
        // The render finished the file before it noticed the cancel flag. That outcome is true and
        // the substitution must not claim otherwise.
        ExportRun.finish(ExportRun.Result.Failed("the encoder stalled"))

        assertEquals(ExportRun.Result.Failed("the encoder stalled"), ExportRun.state.value.result)
    }

    @Test
    fun `a new run does not inherit the previous run's abort reason`() {
        ExportRun.begin(ExportRun.Kind.Visualizer, "clip")
        ExportRun.abort("the service could not go foreground")
        ExportRun.finish(ExportRun.Result.Cancelled)

        ExportRun.begin(ExportRun.Kind.Visualizer, "another clip")
        assertNull(ExportRun.abortReason)
        ExportRun.finish(ExportRun.Result.Cancelled)

        assertEquals(ExportRun.Result.Cancelled, ExportRun.state.value.result)
    }
}
