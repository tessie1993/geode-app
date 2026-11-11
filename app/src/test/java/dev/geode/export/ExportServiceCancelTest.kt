package dev.geode.export

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The notification's Cancel button reaches the same [ExportRun.requestCancel] as the in-app Cancel. */
class ExportServiceCancelTest {
    @Test
    fun cancelActionRequestsCancelOnExportRun() {
        ExportRun.begin("test")
        try {
            assertFalse(ExportRun.cancelRequested)
            assertTrue(routeCommand(ExportService.ACTION_CANCEL, ExportRun::requestCancel))
            assertTrue(ExportRun.cancelRequested)
        } finally {
            ExportRun.finish()
        }
    }

    @Test
    fun otherCommandsDoNotCancel() {
        var calls = 0
        assertFalse(routeCommand(null, { calls++ }))
        assertFalse(routeCommand("dev.geode.export.OTHER", { calls++ }))
        assertEquals(0, calls)
    }
}
