package dev.geode.data

import dev.geode.engine.bridge.GeodeNative

/**
 * Settings › Performance: how much GPU a live surface (screen, wallpaper, dream) may spend.
 *
 * A mode is only a named preset of these values; the user can then change any one of them, and
 * the tab shows "Custom" once the values no longer match a preset. Export never reads this: it
 * renders offline, so it always takes the full-quality path.
 */
data class PerformanceSettings(
    /** Fraction of the surface's pixels the scene is drawn at, then upscaled. Below 1 also drops supersampling. */
    val renderScale: Float,
    /** The live frame-rate cap handed to the pacer: 30 to 120, or Follow device (`GeodeNative.LIVE_FPS_FOLLOW`). */
    val fpsCap: Int,
    /** Multiplier on a 3D style's ray-march step budget (`uSteps`). */
    val marchScale: Float,
    /** Flow distortion and fluid bloom: the optional passes. */
    val postFx: Boolean,
) {
    /** The preset these values match natively, or null when the user has customised them. */
    val mode: PerformanceMode?
        get() =
            PerformanceMode.entries.getOrNull(
                runCatching { GeodeNative.perfModeOf(renderScale, fpsCap, marchScale, postFx) }.getOrDefault(-1),
            )

    companion object {
        // Tiers, the scale choices, first-run pick and the fps caps are native (core/viz/frame/Quality, LiveFps; ADR 0008).
        val SAVER get() = PerformanceMode.SAVER.settings
        val BALANCED get() = PerformanceMode.BALANCED.settings
        val HIGH get() = PerformanceMode.HIGH.settings

        /** The render and march scales the tab offers, ascending. */
        val SCALES: List<Float> by lazy { runCatching { GeodeNative.perfScaleChoices().toList() }.getOrDefault(emptyList()) }

        internal val tiers by lazy {
            PerformanceMode.entries.map { mode ->
                val v = FloatArray(GeodeNative.PERF_TIER_FLOATS)
                check(GeodeNative.perfTier(mode.ordinal, v)) { "no native tier for $mode" }
                PerformanceSettings(v[0], v[1].toInt(), v[2], postFx = v[3] != 0f)
            }
        }

        /** The first-run pick (RAM, low-RAM flag, media performance class). */
        fun modeForDevice(
            totalRamBytes: Long,
            lowRamDevice: Boolean,
            mediaPerformanceClass: Int,
        ): PerformanceMode =
            PerformanceMode.entries.getOrElse(GeodeNative.perfModeForDevice(totalRamBytes, lowRamDevice, mediaPerformanceClass)) {
                PerformanceMode.BALANCED
            }
    }
}

enum class PerformanceMode {
    SAVER,
    BALANCED,
    HIGH,
    ;

    /** This preset's values, from the native tier table (ordinal = the native mode). */
    val settings: PerformanceSettings get() = PerformanceSettings.tiers[ordinal]
}
