package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class PerformanceStoreTest {
    private lateinit var context: Context

    @Before
    fun setUp() {
        context = ApplicationProvider.getApplicationContext()
    }

    @Test
    fun legacyStoredFpsIsIgnoredNormalizedAndPersisted() {
        val prefs = prefs("performance-legacy-fps")
        prefs
            .edit()
            .putInt(PerformanceStore.KEY_RESOLUTION, 900)
            .putInt(PerformanceStore.KEY_FPS, 60)
            .putFloat(PerformanceStore.KEY_MARCH, 0.75f)
            .putBoolean(PerformanceStore.KEY_POST_FX, true)
            .commit()
        val bridge = FakeBridge()

        val loaded = store(prefs, bridge).settings.value

        assertEquals(PerformanceSettings.LIVE_FPS, loaded.fpsCap)
        assertEquals(900, loaded.resolution)
        assertEquals(0.75f, loaded.marchScale, 0f)
        assertEquals(true, loaded.postFx)
        assertEquals(PerformanceSettings.LIVE_FPS, prefs.getInt(PerformanceStore.KEY_FPS, -1))
        assertEquals(PerformanceSettings.LIVE_FPS, bridge.sent.single().fpsCap)
    }

    @Test
    fun legacyFollowAndMalformedFpsTypesAlsoMigrateToThirty() {
        for ((name, legacy) in listOf("performance-legacy-follow" to -1, "performance-malformed-fps" to "120")) {
            val prefs = prefs(name)
            prefs
                .edit()
                .putInt(PerformanceStore.KEY_RESOLUTION, 900)
                .putFloat(PerformanceStore.KEY_MARCH, 0.75f)
                .putBoolean(PerformanceStore.KEY_POST_FX, true)
                .apply()
            when (legacy) {
                is Int -> prefs.edit().putInt(PerformanceStore.KEY_FPS, legacy).apply()
                is String -> prefs.edit().putString(PerformanceStore.KEY_FPS, legacy).apply()
            }

            assertEquals(PerformanceSettings.LIVE_FPS, store(prefs, FakeBridge()).settings.value.fpsCap)
            assertEquals(PerformanceSettings.LIVE_FPS, prefs.getInt(PerformanceStore.KEY_FPS, -1))
        }
    }

    @Test
    fun firstRunKeepsTheDeviceQualityPickButNotItsLegacyFps() {
        val prefs = prefs("performance-first-run")
        val bridge =
            FakeBridge(
                mode = PerformanceSettings(540, 60, 0.5f, false),
            )

        val loaded = store(prefs, bridge, deviceMode = { PerformanceMode.SAVER }).settings.value

        assertEquals(540, loaded.resolution)
        assertEquals(0.5f, loaded.marchScale, 0f)
        assertFalse(loaded.postFx)
        assertEquals(PerformanceSettings.LIVE_FPS, loaded.fpsCap)
        assertEquals(PerformanceSettings.LIVE_FPS, prefs.getInt(PerformanceStore.KEY_FPS, -1))
    }

    @Test
    fun setNormalizesAnOlderCallerBeforeNativeStateAndPreferences() {
        val prefs = prefs("performance-set")
        prefs
            .edit()
            .putInt(PerformanceStore.KEY_RESOLUTION, 900)
            .putInt(PerformanceStore.KEY_FPS, 30)
            .putFloat(PerformanceStore.KEY_MARCH, 0.75f)
            .putBoolean(PerformanceStore.KEY_POST_FX, true)
            .commit()
        val bridge = FakeBridge()
        val store = store(prefs, bridge)
        store.settings.value
        bridge.sent.clear()

        store.set(PerformanceSettings(720, 120, 1.0f, true))

        val saved = store.settings.value
        assertEquals(720, saved.resolution)
        assertEquals(PerformanceSettings.LIVE_FPS, saved.fpsCap)
        assertEquals(PerformanceSettings.LIVE_FPS, prefs.getInt(PerformanceStore.KEY_FPS, -1))
        assertEquals(PerformanceSettings.LIVE_FPS, bridge.sent.single().fpsCap)
    }

    private fun prefs(name: String): SharedPreferences =
        context
            .getSharedPreferences(name, Context.MODE_PRIVATE)
            .also { it.edit().clear().commit() }

    private fun store(
        prefs: SharedPreferences,
        bridge: FakeBridge,
        deviceMode: () -> PerformanceMode = { error("a stored install must not re-pick its device mode") },
    ): PerformanceStore =
        PerformanceStore(
            prefs = prefs,
            deviceMode = deviceMode,
            panel = { DisplayPanel(1080, 2400, 1080, 2400) },
        ).also { it.bridge = bridge }

    private class FakeBridge(
        private val mode: PerformanceSettings = PerformanceSettings(0, 60, 1.0f, true),
    ) : PerformanceStore.Bridge {
        val sent = mutableListOf<PerformanceSettings>()

        override fun panelSet(panel: DisplayPanel) = Unit

        override fun viewSet(
            width: Int,
            height: Int,
        ) = Unit

        override fun settingsSet(settings: PerformanceSettings) {
            sent += settings
        }

        override fun resolutionRead(): ResolutionMirror = ResolutionMirror.EMPTY

        override fun modeSettings(mode: PerformanceMode): PerformanceSettings = this.mode

        override fun high(): PerformanceSettings = mode
    }
}
