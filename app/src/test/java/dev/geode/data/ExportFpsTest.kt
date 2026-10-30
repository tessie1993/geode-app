package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Test

class ExportFpsTest {
    @Test
    fun templateFpsSnapsToTheNearestOfferedRate() {
        val cases = mapOf(0 to 24, 24 to 24, 25 to 25, 27 to 25, 30 to 30, 40 to 30, 45 to 50, 50 to 50, 55 to 50, 60 to 60, 120 to 60)
        for ((authored, expected) in cases) {
            assertEquals("authored $authored", expected, TemplateExport(fps = authored).resolvedFps())
        }
    }

    @Test
    fun encoderFallbackStaysInTheRequestsCadenceFamily() {
        assertEquals(24, fallbackExportFps(24))
        assertEquals(25, fallbackExportFps(25))
        assertEquals(30, fallbackExportFps(30))
        assertEquals(25, fallbackExportFps(50))
        assertEquals(30, fallbackExportFps(60))
        for (fps in EXPORT_FPS_OPTIONS) assertEquals(true, fallbackExportFps(fps) <= fps)
    }
}
