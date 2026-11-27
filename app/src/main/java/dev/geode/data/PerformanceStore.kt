package dev.geode.data

import android.app.ActivityManager
import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import androidx.core.content.edit
import dev.geode.data.prefs.BackupClass
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.PerfNative
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Settings › Performance, in the general prefs file so a store built on its own reads the same values the app wrote.
 *
 * Nothing stored yet means first run: the device decides the mode once, and that pick is written
 * back so it never changes under the user after an OS update or a RAM reading that wobbles. An install from
 * before the resolution setting has a render scale instead: it becomes the nearest resolution once, on [panel].
 *
 * Every change goes to native as a void event (the [panel], the view's size, the settings), and [resolution] is what
 * native derived from them, published: the window buffer, the steps on offer, the one selected, the preset.
 */
class PerformanceStore(
    private val prefs: SharedPreferences,
    private val deviceMode: () -> PerformanceMode,
    private val panel: () -> DisplayPanel,
) {
    constructor(prefs: SharedPreferences, context: Context) : this(
        prefs,
        { devicePerformanceMode(context) },
        { DisplayPanel.atBoot(context) },
    )

    private val state by lazy { MutableStateFlow(load()) }

    private val mirror by lazy {
        ensureLoaded()
        MutableStateFlow(ResolutionMirror.read())
    }

    val settings: StateFlow<PerformanceSettings> get() = state.asStateFlow()

    val resolution: StateFlow<ResolutionMirror> get() = mirror.asStateFlow()

    fun set(settings: PerformanceSettings) {
        send(settings)
        publish()
        state.value = settings
        save(settings)
    }

    /** The display's panel and modes, and the stage view's size in pixels, as Android says them now. */
    fun view(
        panel: DisplayPanel,
        width: Int,
        height: Int,
    ) {
        ensureLoaded()
        send(panel)
        PerfNative.perfViewSet(width, height)
        publish()
    }

    private fun ensureLoaded() = state.value

    private fun publish() {
        mirror.value = ResolutionMirror.read()
    }

    private fun send(panel: DisplayPanel) =
        PerfNative.perfPanelSet(panel.panelWidth, panel.panelHeight, panel.modeWidth, panel.modeHeight)

    private fun send(settings: PerformanceSettings) =
        PerfNative.perfSettingsSet(settings.resolution, settings.fpsCap, settings.marchScale, settings.postFx)

    private fun load(): PerformanceSettings {
        send(panel())
        return read().also(::send)
    }

    private fun read(): PerformanceSettings {
        val stored = prefs.contains(KEY_RESOLUTION)
        // An old install's scale; one of another type (a bad restore) reads as full size.
        val oldScale =
            when {
                stored || !prefs.contains(RETIRED_RENDER_SCALE) -> null
                else -> runCatching { prefs.getFloat(RETIRED_RENDER_SCALE, 1f) }.getOrDefault(1f)
            }
        if (!stored && oldScale == null) return deviceMode().settings.also(::save)
        val fallback = PerformanceSettings.HIGH
        val resolution =
            oldScale?.let { PerformanceSettings.resolutionForScale(it) } ?: prefs.getInt(KEY_RESOLUTION, fallback.resolution)
        val settings =
            PerformanceSettings(
                resolution = resolution,
                fpsCap = prefs.getInt(KEY_FPS, fallback.fpsCap),
                marchScale = prefs.getFloat(KEY_MARCH, fallback.marchScale),
                postFx = prefs.getBoolean(KEY_POST_FX, fallback.postFx),
            )
        if (oldScale != null) save(settings)
        return settings
    }

    private fun save(settings: PerformanceSettings) =
        prefs.edit {
            putInt(KEY_RESOLUTION, settings.resolution)
            putInt(KEY_FPS, settings.fpsCap)
            putFloat(KEY_MARCH, settings.marchScale)
            putBoolean(KEY_POST_FX, settings.postFx)
            remove(RETIRED_RENDER_SCALE)
        }

    internal companion object {
        const val KEY_RESOLUTION = "perf_resolution"
        const val KEY_FPS = "perf_fps_cap"
        const val KEY_MARCH = "perf_march_scale"
        const val KEY_POST_FX = "perf_post_fx"

        /** The render scale, a fraction of the screen (to 2026-11-27): read once to carry an old install over, then removed. */
        private const val RETIRED_RENDER_SCALE = "perf_render_scale"

        /** "Adapt to frame time" (2026-11-18), retired 2026-11-25 with the frame governor: never read or written. */
        private const val RETIRED_ADAPT_FRAME_TIME = "perf_adapt_frame_time"

        /** The thermal-era "Adapt to frame time" and Liquid's hot-phone choice, retired with #1213: never read. */
        private const val RETIRED_ADAPTIVE = "perf_adaptive"
        private const val RETIRED_LIQUID_HOT = "perf_liquid_hot"

        /**
         * Every key this store writes, and the ones it no longer does. They describe this phone (picked from its
         * RAM, performance class and panel at first launch), so a backup leaves them out and a restore never carries
         * one phone's tuning onto another: the new phone keeps its own pick.
         */
        val KEYS: Set<String> =
            setOf(
                KEY_RESOLUTION,
                KEY_FPS,
                KEY_MARCH,
                KEY_POST_FX,
                RETIRED_RENDER_SCALE,
                RETIRED_ADAPT_FRAME_TIME,
                RETIRED_ADAPTIVE,
                RETIRED_LIQUID_HOT,
            )

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                device(PrefType.INT, KEY_RESOLUTION, KEY_FPS)
                device(PrefType.FLOAT, KEY_MARCH)
                device(PrefType.BOOLEAN, KEY_POST_FX)
                retired(PrefType.FLOAT, RETIRED_RENDER_SCALE, backup = BackupClass.DEVICE)
                retired(PrefType.BOOLEAN, RETIRED_ADAPT_FRAME_TIME, backup = BackupClass.DEVICE)
                retired(PrefType.BOOLEAN, RETIRED_ADAPTIVE, backup = BackupClass.DEVICE)
                retired(PrefType.INT, RETIRED_LIQUID_HOT, backup = BackupClass.DEVICE)
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
