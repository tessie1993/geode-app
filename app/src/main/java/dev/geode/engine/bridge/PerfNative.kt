package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for performance tiers.
object PerfNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // Settings › Performance (core/viz/frame/Quality): mode 0 Saver, 1 Balanced, 2 High.

    /** Floats [perfTier] writes: render scale, fps cap, march scale, post FX (0/1). */
    const val PERF_TIER_FLOATS = 4

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

    /** The first-run mode for this phone; [totalRamBytes] 0 = unknown. */
    external fun perfModeForDevice(
        totalRamBytes: Long,
        lowRam: Boolean,
        mediaPerformanceClass: Int,
    ): Int

    /** The mode these values are, or -1 when none is ("Custom"). */
    external fun perfModeOf(
        renderScale: Float,
        fpsCap: Int,
        marchScale: Float,
        postFx: Boolean,
    ): Int

    /** The render and march scales Settings › Performance offers, ascending. */
    external fun perfScaleChoices(): FloatArray

    /**
     * The window buffer for a view of [viewWidth] x [viewHeight] pixels at [renderScale], width then height into
     * [out] (2 ints): the size the stage's surface is fixed to, which the display scales up. False for no view.
     */
    external fun perfBufferSize(
        renderScale: Float,
        viewWidth: Int,
        viewHeight: Int,
        out: IntArray,
    ): Boolean

    /** Android's onTrimMemory level, as is; native acts on UI_HIDDEN and BACKGROUND and ignores the rest (#854). */
    external fun memoryTrim(level: Int)
}
