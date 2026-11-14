package dev.geode.data

import android.app.ActivityManager
import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import androidx.core.content.edit
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Settings › Performance, in the general prefs file so the wallpaper and the dream (which build
 * their own stores, in their own processes' lifetimes) read the same values the app wrote.
 *
 * Nothing stored yet means first run: the device decides the mode once, and that pick is written
 * back so it never changes under the user after an OS update or a RAM reading that wobbles.
 */
class PerformanceStore(
    private val prefs: SharedPreferences,
    private val deviceMode: () -> PerformanceMode,
) {
    constructor(prefs: SharedPreferences, context: Context) : this(prefs, { devicePerformanceMode(context) })

    private val state by lazy { MutableStateFlow(load()) }

    val settings: StateFlow<PerformanceSettings> get() = state.asStateFlow()

    fun set(settings: PerformanceSettings) {
        state.value = settings
        save(settings)
    }

    private fun load(): PerformanceSettings {
        if (!prefs.contains(KEY_SCALE)) return deviceMode().settings.also(::save)
        val fallback = PerformanceSettings.HIGH
        return PerformanceSettings(
            renderScale = prefs.getFloat(KEY_SCALE, fallback.renderScale),
            fpsCap = prefs.getInt(KEY_FPS, fallback.fpsCap),
            marchScale = prefs.getFloat(KEY_MARCH, fallback.marchScale),
            postFx = prefs.getBoolean(KEY_POST_FX, fallback.postFx),
        )
    }

    private fun save(settings: PerformanceSettings) =
        prefs.edit {
            putFloat(KEY_SCALE, settings.renderScale)
            putInt(KEY_FPS, settings.fpsCap)
            putFloat(KEY_MARCH, settings.marchScale)
            putBoolean(KEY_POST_FX, settings.postFx)
        }

    internal companion object {
        const val KEY_SCALE = "perf_render_scale"
        const val KEY_FPS = "perf_fps_cap"
        const val KEY_MARCH = "perf_march_scale"
        const val KEY_POST_FX = "perf_post_fx"

        /**
         * Every key this store writes. They describe this phone (picked from its RAM and performance
         * class at first launch), so a backup leaves them out and a restore never carries one phone's
         * tuning onto another: the new phone keeps its own pick.
         */
        val KEYS: Set<String> = setOf(KEY_SCALE, KEY_FPS, KEY_MARCH, KEY_POST_FX)

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                device(PrefType.FLOAT, KEY_SCALE, KEY_MARCH)
                device(PrefType.INT, KEY_FPS)
                device(PrefType.BOOLEAN, KEY_POST_FX)
            }
    }
}

/** The first-run pick from what the platform says about this phone; see [PerformanceSettings.modeForDevice]. */
fun devicePerformanceMode(context: Context): PerformanceMode {
    val am = context.getSystemService(ActivityManager::class.java)
    val memory = ActivityManager.MemoryInfo().also { am?.getMemoryInfo(it) }
    val performanceClass = Build.VERSION.MEDIA_PERFORMANCE_CLASS
    return PerformanceSettings.modeForDevice(memory.totalMem, am?.isLowRamDevice == true, performanceClass)
}
