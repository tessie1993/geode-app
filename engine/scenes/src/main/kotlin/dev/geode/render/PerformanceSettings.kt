package dev.geode.render

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
    /** The frame-rate cap handed to the pacer. */
    val fpsCap: Int,
    /** Multiplier on a 3D style's ray-march step budget (`uSteps`). */
    val marchScale: Float,
    /** Flow distortion, ripple overlay and fluid bloom: the optional passes. */
    val postFx: Boolean,
    /** Measured frame time steps quality down when frames run slow, and back up when they keep up. */
    val adaptive: Boolean,
) {
    /** The preset these values match, or null when the user has customised them. */
    val mode: PerformanceMode?
        get() = PerformanceMode.entries.firstOrNull { it.settings == this }

    companion object {
        val SAVER = PerformanceSettings(renderScale = 0.5f, fpsCap = 30, marchScale = 0.5f, postFx = false, adaptive = true)
        val BALANCED = PerformanceSettings(renderScale = 0.75f, fpsCap = 60, marchScale = 0.75f, postFx = true, adaptive = true)
        val HIGH = PerformanceSettings(renderScale = 1f, fpsCap = 60, marchScale = 1f, postFx = true, adaptive = false)

        val RENDER_SCALES = listOf(0.5f, 0.75f, 1f)
        val FPS_CAPS = listOf(30, 60)
        val MARCH_SCALES = listOf(0.5f, 0.75f, 1f)

        private const val MB = 1024L * 1024L
        private const val SAVER_BELOW_RAM = 3_000L * MB
        private const val HIGH_FROM_RAM = 6_000L * MB

        /** Android 12's media performance class: the first one that promises a capable GPU. */
        private const val HIGH_PERFORMANCE_CLASS = 31

        /**
         * The first-run pick. RAM is the one signal every API level reports, and it tracks GPU
         * class well enough on Android; the media performance class (0 when undeclared) can only
         * promote. A wrong guess costs little: adaptive mode corrects it within seconds.
         */
        fun modeForDevice(
            totalRamBytes: Long,
            lowRamDevice: Boolean,
            mediaPerformanceClass: Int,
        ): PerformanceMode =
            when {
                lowRamDevice || totalRamBytes in 1 until SAVER_BELOW_RAM -> PerformanceMode.SAVER
                mediaPerformanceClass >= HIGH_PERFORMANCE_CLASS || totalRamBytes >= HIGH_FROM_RAM -> PerformanceMode.HIGH
                else -> PerformanceMode.BALANCED
            }
    }
}

enum class PerformanceMode(
    val settings: PerformanceSettings,
) {
    SAVER(PerformanceSettings.SAVER),
    BALANCED(PerformanceSettings.BALANCED),
    HIGH(PerformanceSettings.HIGH),
}
