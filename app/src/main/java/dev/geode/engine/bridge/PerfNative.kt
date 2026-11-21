package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for performance tiers (CLAUDE.md).
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
    const val LIVE_FPS_MAX_CHOICES = 5

    /**
     * Mirrors GEODE_LIVE_FPS_FOLLOW: Settings › Performance's "Follow device", stored as the live cap and handed
     * to the pacer as one. The stage runs at the rate the display runs now and states no rate of its own.
     */
    const val LIVE_FPS_FOLLOW = -1

    /**
     * Settings › Performance's live frame-rate caps (core/viz/frame/LiveFps) on a display that can switch to
     * [refreshRates] seamlessly, ascending into [out]; returns how many. 30 and 60 always, 90 and 120 where
     * the display shows them, then [LIVE_FPS_FOLLOW]. The in-app stage only: the wallpaper, dream, export and
     * Cast keep their own.
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

    /** Mirrors GEODE_PERF_ADAPT_NOW_FLOATS: the size of the array [perfAdaptNow] fills. */
    const val PERF_ADAPT_NOW_FLOATS = 5

    /**
     * "Adapt to frame time" as the live surface that drew last runs now (native `viz/frame/FrameGovernor`): the paced
     * fps (0 = free-running), render scale, march scale, post FX (0/1) and the steps taken (-1 = no live frame yet).
     * False when [out] is too small.
     */
    external fun perfAdaptNow(out: FloatArray): Boolean

    /** Android's onTrimMemory level, as is; native acts on UI_HIDDEN and BACKGROUND and ignores the rest (#854). */
    external fun memoryTrim(level: Int)
}
