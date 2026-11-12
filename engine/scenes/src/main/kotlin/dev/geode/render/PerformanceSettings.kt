package dev.geode.render

import dev.geode.engine.bridge.GeodeNative

/**
 * Settings › Performance: how much GPU a live surface (screen, wallpaper, dream) may spend.
 *
 * A mode is only a named preset of these values; the user can then change any one of them, and
 * the tab shows "Custom" once the values no longer match a preset. Export never reads this: it
 * renders offline, so it always takes the full-quality path.
 *
 * Every value multiplies what the renderer would otherwise do, under the thermal tier, so a hot
 * phone still steps down from wherever the user put it.
 */
data class PerformanceSettings(
    /** Fraction of the surface's pixels the scene is drawn at, then upscaled. Below 1 also drops supersampling. */
    val renderScale: Float,
    /** The live frame-rate cap handed to the pacer: 30 to 120, snapped to the display where it is applied. */
    val fpsCap: Int,
    /** Multiplier on a 3D style's ray-march step budget (`uSteps`). */
    val marchScale: Float,
    /** Flow distortion, ripple overlay and fluid bloom: the optional passes. */
    val postFx: Boolean,
    /** Measured frame time steps quality down when frames run slow, and back up when they keep up. */
    val adaptive: Boolean,
    /**
     * What Liquid mode does once the phone runs hot. Not part of any preset: it is a taste (keep the
     * look flowing, or protect the battery), not a cost level, so picking Saver or High leaves it
     * alone and it never makes the tab read "Custom".
     */
    val liquidHot: LiquidHotPolicy = LiquidHotPolicy.SIMPLIFY,
) {
    /** The preset these values match, or null when the user has customised them. */
    val mode: PerformanceMode?
        get() = PerformanceMode.entries.firstOrNull { it.settings == copy(liquidHot = it.settings.liquidHot) }

    companion object {
        // Tiers, first-run pick and the fps caps a display is offered are native (core/viz/frame/Quality, LiveFps; ADR 0008).
        val SAVER get() = PerformanceMode.SAVER.settings
        val BALANCED get() = PerformanceMode.BALANCED.settings
        val HIGH get() = PerformanceMode.HIGH.settings

        val RENDER_SCALES = listOf(0.5f, 0.75f, 1f)
        val MARCH_SCALES = listOf(0.5f, 0.75f, 1f)

        internal val tiers by lazy {
            PerformanceMode.entries.map { mode ->
                val v = FloatArray(GeodeNative.PERF_TIER_FLOATS)
                check(GeodeNative.perfTier(mode.ordinal, v)) { "no native tier for $mode" }
                PerformanceSettings(v[0], v[1].toInt(), v[2], postFx = v[3] != 0f, adaptive = v[4] != 0f)
            }
        }

        /** The first-run pick (RAM, low-RAM flag, media performance class); adaptive mode fixes a bad guess. */
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

/**
 * Settings > Performance > Liquid on a hot phone. [native] is the wire value of `liquid::HotPolicy`
 * (core/viz/liquid/LiquidMath.hpp): the thermal tier decides when the phone is hot, this decides
 * what Liquid does about it. Simplify is the default because the flowing look is the point of the
 * mode; Full is for people who would rather throttle than lose it; Pause is the battery-first choice.
 */
enum class LiquidHotPolicy(
    val native: Int,
) {
    SIMPLIFY(0),
    FULL(1),
    PAUSE(2),
    ;

    companion object {
        /** The policy for a stored int; anything unknown (a value from a newer build) falls back to the default. */
        fun fromNative(value: Int): LiquidHotPolicy = entries.firstOrNull { it.native == value } ?: SIMPLIFY
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
