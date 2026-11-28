package dev.geode.data

import dev.geode.engine.bridge.PerfNative

/**
 * Settings › Performance: how much GPU a live surface (screen) may spend.
 *
 * A mode is only a named preset of the resolution and shader-work values; the tab shows "Custom" once
 * those no longer match a preset. Live frame rate is fixed at [LIVE_FPS], never selected or reduced
 * automatically. Export never reads this: it renders offline, so it always takes the full-quality path.
 */
data class PerformanceSettings(
    /**
     * The render resolution: the picture's short side in real pixels when it fills the screen (1080 is 1080 x 2400 on
     * a 20:9 phone), or [PerfNative.PERF_NATIVE] for the panel's own. The window buffer is sized to it and the
     * display scales it up (`core/viz/frame/Quality`).
     */
    val resolution: Int,
    /** Retained storage/ABI field. Every live value is normalized to [LIVE_FPS]; legacy 30–120/Follow values are ignored. */
    val fpsCap: Int,
    /** Multiplier on a 3D style's ray-march step budget (`uSteps`). */
    val marchScale: Float,
    /** Shader-style Trails, flow distortion and fluid bloom: the optional passes. */
    val postFx: Boolean,
) {
    companion object {
        /** The live visualizer's one frame rate. Export keeps its own 24–60 FPS ladder. */
        const val LIVE_FPS = 30

        // Tiers, the choices and first-run pick are native (core/viz/frame/Quality, LiveFps).
        val SAVER get() = PerformanceMode.SAVER.settings
        val BALANCED get() = PerformanceMode.BALANCED.settings
        val HIGH get() = PerformanceMode.HIGH.settings

        /** The march scales the tab offers, ascending. */
        val SCALES: List<Float> by lazy { runCatching { PerfNative.perfScaleChoices().toList() }.getOrDefault(emptyList()) }

        internal val tiers by lazy {
            PerformanceMode.entries.map { mode ->
                val v = FloatArray(PerfNative.PERF_TIER_FLOATS)
                check(PerfNative.perfTier(mode.ordinal, v)) { "no native tier for $mode" }
                PerformanceSettings(v[0].toInt(), v[1].toInt(), v[2], postFx = v[3] != 0f)
            }
        }

        /** The first-run pick (RAM, low-RAM flag, media performance class; native has the panel). */
        fun modeForDevice(
            totalRamBytes: Long,
            lowRamDevice: Boolean,
            mediaPerformanceClass: Int,
        ): PerformanceMode =
            PerformanceMode.entries.getOrElse(PerfNative.perfModeForDevice(totalRamBytes, lowRamDevice, mediaPerformanceClass)) {
                PerformanceMode.BALANCED
            }

        /** An old install's stored render scale as the resolution nearest it on the panel native holds. */
        fun resolutionForScale(renderScale: Float): Int = PerfNative.perfResolutionForScale(renderScale)
    }

    /** The fixed-30 contract at the Kotlin boundary, including values restored from an older install. */
    internal fun withFixedLiveFps(): PerformanceSettings = if (fpsCap == LIVE_FPS) this else copy(fpsCap = LIVE_FPS)
}

enum class PerformanceMode {
    SAVER,
    BALANCED,
    HIGH,
    ;

    /** This preset's values, from the native tier table (ordinal = the native mode). */
    val settings: PerformanceSettings get() = PerformanceSettings.tiers[ordinal]
}
