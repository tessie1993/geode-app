package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for performance tiers.
object PerfNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // Settings › Performance (core/viz/frame/Quality): mode 0 Saver, 1 Balanced, 2 High.

    /** Floats [perfTier] writes: resolution (a short side in pixels, or [PERF_NATIVE]), fixed-30 ABI slot, march scale, post FX (0/1). */
    const val PERF_TIER_FLOATS = 4

    /** Mirrors GEODE_PERF_NATIVE: the render resolution that is the panel's own. */
    const val PERF_NATIVE = 0

    /** Fills [out] with [mode]'s tier; false for an unknown mode. */
    external fun perfTier(
        mode: Int,
        out: FloatArray,
    ): Boolean

    /** Legacy ABI capacity. [liveFpsChoices] now fills only the one fixed rate, 30 FPS. */
    const val LIVE_FPS_MAX_CHOICES = 12

    /**
     * Retired stored value, kept for ABI compatibility. Native normalizes it (and every other legacy cap) to 30 FPS;
     * it is never offered or returned by the fixed-rate live path.
     */
    const val LIVE_FPS_FOLLOW = -1

    /**
     * Compatibility ABI: writes the one fixed live rate (30) into [out] and returns 1. [refreshRates] is ignored.
     * The in-app stage only: export and Cast keep their own clocks.
     */
    external fun liveFpsChoices(
        refreshRates: FloatArray,
        out: IntArray,
    ): Int

    /** Compatibility ABI: every legacy stored live cap normalizes to the fixed 30 FPS; [refreshRates] is ignored. */
    external fun liveFpsCap(
        storedFps: Int,
        refreshRates: FloatArray,
    ): Int

    /**
     * Retired compatibility ABI: always returns 0 (no manual display-mode preference). The legacy [fps] and
     * [refreshRates] arguments are ignored; native StageThread makes the standing fixed-30 FPS window vote.
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

    /** The user's resolution and shader work; the retained fps slot is normalized to fixed 30 FPS. */
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
