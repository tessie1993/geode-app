package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PerformanceSettingsTest {
    @Test
    fun everyPresetNamesItsOwnMode() {
        PerformanceMode.entries.forEach { assertEquals(it, it.settings.mode) }
    }

    @Test
    fun anEditedPresetIsCustom() {
        assertNull(PerformanceSettings.HIGH.copy(fpsCap = 30).mode)
    }

    @Test
    fun presetsOnlyUseValuesTheTabOffers() {
        PerformanceMode.entries.map { it.settings }.forEach { s ->
            assertTrue(s.renderScale in PerformanceSettings.RENDER_SCALES)
            assertTrue(s.fpsCap in PerformanceSettings.FPS_CAPS)
            assertTrue(s.marchScale in PerformanceSettings.MARCH_SCALES)
        }
    }

    @Test
    fun saverIsCheaperThanHighOnEveryAxis() {
        val saver = PerformanceSettings.SAVER
        val high = PerformanceSettings.HIGH
        assertTrue(saver.renderScale < high.renderScale)
        assertTrue(saver.fpsCap < high.fpsCap)
        assertTrue(saver.marchScale < high.marchScale)
    }

    @Test
    fun devicePickFollowsRamAndPerformanceClass() {
        val gb = 1024L * 1024L * 1024L
        assertEquals(PerformanceMode.SAVER, PerformanceSettings.modeForDevice(2 * gb, false, 0))
        assertEquals(PerformanceMode.SAVER, PerformanceSettings.modeForDevice(8 * gb, true, 0))
        assertEquals(PerformanceMode.BALANCED, PerformanceSettings.modeForDevice(4 * gb, false, 0))
        assertEquals(PerformanceMode.HIGH, PerformanceSettings.modeForDevice(4 * gb, false, 33))
        assertEquals(PerformanceMode.HIGH, PerformanceSettings.modeForDevice(8 * gb, false, 0))
    }
}
