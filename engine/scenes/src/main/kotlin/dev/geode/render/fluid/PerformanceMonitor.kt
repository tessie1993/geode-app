package dev.geode.render.fluid

/**
 * Watches achieved frame rate and says, in whole quality tiers, how much load to shed.
 *
 * The controller has one bias, and everything here follows from it: it downgrades on a deficit
 * that has *lasted*, and it never upgrades. Frame time is noisy — a scene rebuild, a GC, a
 * notification shade — and a scaler that chases noise costs more in visible pulsing than the
 * frames it buys back. So a downgrade needs [sustainSeconds] of continuous shortfall to earn
 * itself, and giving quality back is left to whoever owns the setting: the fluid ladder clears
 * its accumulated downgrade only when the user picks a tier, and [dev.geode.render.ThermalGovernor]
 * only when the device reports it has actually cooled.
 *
 * Two consumers share it, both feeding it [onFrame] once per drawn frame and calling [reset]
 * after acting on a non-zero result: the fluid auto-quality ladder in [FluidSceneBase], and the
 * engine-wide thermal governor's fallback for devices below API 29, which have no thermal API to
 * ask. Not thread-safe apart from [pacedFps]: the frame path is meant to be driven from one
 * render thread at a time, and the worst a second one can do is blend its intervals into the
 * window — which, for the governor's device-wide instance, is arguably the right answer anyway.
 *
 * @param targetFps the rate below which the renderer is considered to be falling behind, when it
 *   is free-running. See [pacedFps] for what happens when it is not.
 */
internal class PerformanceMonitor(
    private val targetFps: Float = 50f,
    private val sustainSeconds: Float = 2.5f,
    private val windowSize: Int = 30,
) {
    private val samples = FloatArray(windowSize)
    private var count = 0
    private var index = 0
    private var deficitSeconds = 0f
    private var lastTargetFps = 0f

    /**
     * The rate the renderer is currently ASKING the display for, or [FREE_RUNNING] when it is not
     * asking for anything in particular.
     *
     * Without this, a deliberate frame-rate cap is indistinguishable from a device that cannot
     * keep up. Pace a live wallpaper at the 24–30 fps §4.4 of the quality bar wants and every
     * frame it draws is 20 fps below the free-running target, so the monitor reads its own cap as
     * a permanent deficit and walks quality down until it bottoms out — which is why
     * `VisualizerWallpaperService` still asks for the full rate. Publishing the cap here is what
     * makes the reduced rate safe to ask for.
     *
     * Volatile because the frame pacer is not on the render thread; the value is folded in on the
     * render thread, in [onFrame].
     */
    @Volatile
    var pacedFps: Float = FREE_RUNNING

    val averageFps: Float
        get() {
            if (count < windowSize / 2) return 0f
            var sum = 0f
            val n = minOf(count, windowSize)
            for (i in 0 until n) sum += samples[i]
            val meanDt = sum / n
            return if (meanDt > 1e-6f) 1f / meanDt else 0f
        }

    /**
     * Returns how many quality tiers to give up: 0 for none, 1 for a shortfall, 2 for a rout.
     *
     * A non-zero result is a one-shot: the caller applies it and calls [reset], so the same slow
     * seconds cannot be charged twice.
     */
    fun onFrame(dtSeconds: Float): Int {
        val target = effectiveTargetFps()
        if (target != lastTargetFps) {
            // Deficit accrued against the old target says nothing about the new one — half a
            // second of "behind 50" is not half a second of "behind 25".
            lastTargetFps = target
            deficitSeconds = 0f
        }
        val fps = if (dtSeconds > 1e-6f) 1f / dtSeconds else 1000f
        // Outlier gate, not a clamp: a single 300 ms frame is a scene being built or a GC, and
        // folding it into the window would downgrade quality for something that has already
        // finished happening.
        if (fps < 5f || fps > 180f) return 0
        samples[index] = dtSeconds
        index = (index + 1) % windowSize
        count = minOf(count + 1, windowSize)
        val avg = averageFps
        if (avg <= 0f) return 0
        if (avg < target) {
            deficitSeconds += dtSeconds
        } else {
            deficitSeconds = 0f
        }
        if (deficitSeconds < sustainSeconds) return 0
        // A third of the target missing is not a tier's worth of overshoot, it is the wrong tier
        // entirely; stepping once and waiting another 2.5 s to step again would spend eight
        // seconds getting somewhere the first measurement already knew about.
        val severity = (target - avg) / target
        return if (severity > 0.35f) 2 else 1
    }

    fun reset() {
        samples.fill(0f)
        count = 0
        index = 0
        deficitSeconds = 0f
    }

    /**
     * Never above [targetFps], so a cap *higher* than the free-running target cannot make the
     * monitor stricter than it was designed to be; the headroom fraction absorbs pacer jitter, so
     * a 30 fps cap measuring 29.4 fps is a rounding rather than a struggling GPU.
     */
    private fun effectiveTargetFps(): Float {
        val paced = pacedFps
        if (paced <= 0f) return targetFps
        return minOf(targetFps, paced * KEEPING_UP_FRACTION)
    }

    companion object {
        /** [pacedFps] when the renderer draws as fast as the display will take frames. */
        const val FREE_RUNNING = 0f

        private const val KEEPING_UP_FRACTION = 0.85f
    }
}
