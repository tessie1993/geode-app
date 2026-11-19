package dev.geode.data

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Settings › Performance moves out of `geode-prefs` into `geode-device`, once, keeping the phone's values. */
@RunWith(RobolectricTestRunner::class)
class PerformancePrefsMoveTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    private fun prefs(name: String) = context.getSharedPreferences(name, Context.MODE_PRIVATE)

    private val general get() = prefs(GeodePrefsFiles.GENERAL)
    private val device get() = prefs(GeodePrefsFiles.DEVICE)

    private fun writeOldInstall() {
        general
            .edit()
            .putFloat(PerformanceStore.KEY_SCALE, 0.75f)
            .putInt(PerformanceStore.KEY_FPS, 30)
            .putFloat(PerformanceStore.KEY_MARCH, 0.5f)
            .putBoolean(PerformanceStore.KEY_POST_FX, false)
            .putBoolean("perf_adaptive", true)
            .putString("gui_font", "kept")
            .commit()
    }

    @Test
    fun anOldInstallsValuesMoveAndLeaveTheGeneralFile() {
        writeOldInstall()
        PerformanceStore.moveFrom(general, device)

        assertEquals(0.75f, device.getFloat(PerformanceStore.KEY_SCALE, 0f))
        assertEquals(30, device.getInt(PerformanceStore.KEY_FPS, 0))
        assertEquals(0.5f, device.getFloat(PerformanceStore.KEY_MARCH, 0f))
        assertFalse(device.getBoolean(PerformanceStore.KEY_POST_FX, true))
        assertFalse("a retired key is not carried over", device.contains("perf_adaptive"))
        assertEquals(emptyList<String>(), PerformanceStore.KEYS.filter(general::contains))
        assertEquals("kept", general.getString("gui_font", null))
    }

    @Test
    fun movingAgainChangesNothingAndTheDeviceFileWins() {
        device.edit().putInt(PerformanceStore.KEY_FPS, 60).commit()
        writeOldInstall()
        PerformanceStore.moveFrom(general, device)
        val first = device.all.toMap()
        PerformanceStore.moveFrom(general, device)

        assertEquals(first, device.all.toMap())
        assertEquals(60, device.getInt(PerformanceStore.KEY_FPS, 0))
    }

    @Test
    fun openingTheDeviceFileMovesTheValues() {
        writeOldInstall()
        val files = GeodePrefsFiles(context)
        assertEquals(30, files.device.getInt(PerformanceStore.KEY_FPS, 0))
        assertFalse(files.general.contains(PerformanceStore.KEY_FPS))
    }
}
