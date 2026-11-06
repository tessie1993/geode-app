package dev.geode.render

import android.content.Context
import android.os.Build
import android.os.PowerManager
import dev.geode.engine.bridge.GeodeNative

/**
 * The platform's thermal readings, handed raw to the process's one native thermal state
 * (`core/viz/frame/Thermal`), which settles the tier every renderer reads and the pacer's fps cap.
 *
 * [attach] resolves PowerManager once per process; [sample] reads it when native says a reading is
 * due (`FRAME_THERMAL_DUE` from the renderer's one per-frame call).
 */
object ThermalGovernor {
    /**
     * The rate the renderer is currently asking the display for, or 0 when it free-runs.
     *
     * Publish it from whoever owns the frame pacer, so a deliberate cap is never mistaken for a
     * device that cannot keep up.
     */
    @Volatile
    var pacedFps: Float = 0f

    @Volatile
    private var power: PowerManager? = null

    @Synchronized
    fun attach(context: Context) {
        // Application context: this reference outlives every Activity and surface by design.
        if (power == null) power = context.applicationContext.getSystemService(PowerManager::class.java)
    }

    /**
     * Called on a live renderer's GL thread only on the frame native said a reading is due, once a
     * second for the whole process: PowerManager may answer NaN to a second ask inside its one-second
     * window, which is how a second renderer used to read no forecast and stay at Full (#387). The
     * status is read here too, at that cadence, rather than kept by a listener. Native keeps the last
     * real headroom across a NaN and settles the one tier every renderer reads.
     */
    fun sample() {
        val service = power
        if (service == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return GeodeNative.thermalPush(-1, Float.NaN)
        val status = runCatching { service.currentThermalStatus }.getOrDefault(-1)
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return GeodeNative.thermalPush(status, Float.NaN)
        GeodeNative.thermalPush(status, runCatching { service.getThermalHeadroom(FORECAST_SECONDS) }.getOrDefault(Float.NaN))
    }
}

private const val FORECAST_SECONDS = 30
