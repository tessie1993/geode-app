package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Test

/** The encoder's fps retry is native now (core/output/EncodePlan); core/tests/output_test.cpp pins it. */
class ExportFpsTest {
    @Test
    fun templateFpsSnapsToTheNearestOfferedRate() {
        val cases = mapOf(0 to 24, 24 to 24, 25 to 25, 27 to 25, 30 to 30, 40 to 30, 45 to 50, 50 to 50, 55 to 50, 60 to 60, 120 to 60)
        for ((authored, expected) in cases) {
            assertEquals("authored $authored", expected, TemplateExport(fps = authored).resolvedFps())
        }
    }
}
