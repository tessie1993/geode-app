package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for performance tiers.
object PerfNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // Settings › Performance (core/viz/frame/Quality): mode 0 Saver, 1 Balanced, 2 High.

    /** Floats [perfTier] writes: resolution (a short side in pixels, or [PERF_NATIVE]), fps cap, march scale, post FX (0/1). */
    const val PERF_TIER_FLOATS = 4

    /** Mirrors GEODE_PERF_NATIVE: the render resolution that is the panel's own. */
    const val PERF_NATIVE = 0

    /** Fills [out] with [mode]'s tier; false for an unknown mode. */
    external fun perfTier(
        mode: Int,
        out: FloatArray,
    ): Boolean

    /** Mirrors GEODE_LIVE_FPS_MAX_CHOICES: the size of the array [liveFpsChoices] fills. */
    const val LIVE_FPS_MAX_CHOICES = 12

    /**
     * Mirrors GEODE_LIVE_FPS_FOLLOW: Settings › Performance's "Follow device", stored as the live cap but
     * stating no rate. The stage runs at the rate the display runs now and states no rate of its own.
     */
    const val LIVE_FPS_FOLLOW = -1

    /**
     * Settings › Performance's live frame rates (core/viz/frame/LiveFps) on a display with [refreshRates],
     * ascending into [out]; returns how many. Every refresh rate divided by a whole number (30 to 120; 30 and 60
     * for an unknown display), then [LIVE_FPS_FOLLOW]. The in-app stage only: the
     * export and Cast keep their own.
     */
    external fun liveFpsChoices(
        refreshRates: FloatArray,
        out: IntArray,
    ): Int

    /** The stored live cap as a display with [refreshRates] runs it: offered as is, else the nearest offered. */
    external fun liveFpsCap(
        storedFps: Int,
        refreshRates: FloatArray,
    ): Int

    /**
     * The refresh rate the window asks the display for while [fps] is the live rate: the lowest of [refreshRates]
     * that shows it evenly (60 on a 60/90 Hz panel runs the 60 Hz mode, 45 the 90 Hz one); 0 for no preference.
     */
    external fun liveFpsRefresh(
        fps: Int,
        refreshRates: FloatArray,
    ): Float

    /** The first-run mode for this phone on the panel last sent ([perfPanelSet]); [totalRamBytes] 0 = unknown. */
    external fun perfModeForDevice(
        totalRamBytes: Long,
        lowRam: Boolean,
        mediaPerformanceClass: Int,
    ): Int

    /** The resolution an old install's stored render scale maps to on the panel last sent. */
    external fun perfResolutionForScale(renderScale: Float): Int
    /** The march scales Settings › Performance offers, ascending. */
    external fun perfScaleChoices(): FloatArray

    // The resolution the app holds (core/viz/frame/ResolutionState): each event returns nothing, and
    // [perfResolutionRead] reads what native derived.

    /** The display's largest physical mode (the panel) and its current one, 0 for unknown. */
    external fun perfPanelSet(
        panelWidth: Int,
        panelHeight: Int,
        modeWidth: Int,
        modeHeight: Int,
    )

    /** The stage view's size in pixels. */
    external fun perfViewSet(
        viewWidth: Int,
        viewHeight: Int,
    )

    /** The user's resolution, fps cap, march scale and post FX. */
    external fun perfSettingsSet(
        resolution: Int,
        fpsCap: Int,
        marchScale: Float,
        postFx: Boolean,
    )

    /** Fills [out] ([PERF_DESCRIPTOR_INTS]) with native's descriptor, in the order of `PERF_FIELD_*`; false if [out] is short. */
    external fun perfResolutionRead(out: IntArray): Boolean

    /** Mirrors GEODE_PERF_DESCRIPTOR_INTS and GEODE_PERF_FIELD_*: [perfResolutionRead]'s ints. */
    const val PERF_DESCRIPTOR_INTS = 17
    const val PERF_FIELD_RESOLUTION = 0
    const val PERF_FIELD_PRESET = 1
    const val PERF_FIELD_SELECTED = 2
    const val PERF_FIELD_BUFFER_WIDTH = 3
    const val PERF_FIELD_BUFFER_HEIGHT = 4
    const val PERF_FIELD_DRAWN_WIDTH = 5
    const val PERF_FIELD_DRAWN_HEIGHT = 6
    const val PERF_FIELD_MODE_WIDTH = 7
    const val PERF_FIELD_MODE_HEIGHT = 8
    const val PERF_FIELD_STEP_COUNT = 9
    const val PERF_FIELD_STEPS = 10

    /** Android's onTrimMemory level, as is; native acts on UI_HIDDEN and BACKGROUND and ignores the rest (#854). */
    external fun memoryTrim(level: Int)
}
