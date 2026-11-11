package dev.geode.render

import android.content.Context
import android.os.PowerManager
import dev.geode.engine.bridge.GeodeNative

/**
 * The platform's thermal readings, handed raw to the process's one native thermal state
 * (`core/viz/frame/Thermal`), which settles the tier every renderer reads and the pacer's fps cap.
 *
 * [attach] resolves PowerManager once per process and has it push the status, now and on every change
 * (on the main thread); [sample] asks for the headroom when native says one is due
 * (`FRAME_THERMAL_DUE` from the renderer's one per-frame call).
 */
object ThermalGovernor {
    @Volatile
    private var power: PowerManager? = null

    @Synchronized
    fun attach(context: Context) {
        if (power != null) return
        // Application context: this reference and its listener outlive every Activity and surface by design.
        val service = context.applicationContext.getSystemService(PowerManager::class.java) ?: return
        power = service
        runCatching { service.addThermalStatusListener { GeodeNative.thermalPush(it, Float.NaN) } }
    }

    /**
     * Called on a live renderer's GL thread only on the frame native said a headroom is due, every ten seconds
     * for the whole process: PowerManager answers NaN when asked more often, and native holds the last real one.
     */
    fun sample() {
        val headroom = runCatching { power?.getThermalHeadroom(FORECAST_SECONDS) }.getOrNull() ?: Float.NaN
        GeodeNative.thermalPush(-1, headroom)
    }
}

private const val FORECAST_SECONDS = 30
