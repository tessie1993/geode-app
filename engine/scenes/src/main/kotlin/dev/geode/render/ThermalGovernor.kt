package dev.geode.render

import android.content.Context
import android.os.Build
import android.os.PowerManager
import dev.geode.util.bestEffort

/**
 * The platform's thermal readings, handed to the native governor that settles the render tier.
 *
 * [attach] registers once per process; the readings are cheap enough for the render thread to
 * sample every second.
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

    /** Written on the main executor by the platform listener, read on the render thread. */
    @Volatile
    private var osStatus: Int = STATUS_NONE

    @Volatile
    private var power: PowerManager? = null

    @Volatile
    private var attached = false

    @Synchronized
    fun attach(context: Context) {
        if (attached) return
        attached = true
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        // Application context: this reference outlives every Activity and surface by design.
        val app = context.applicationContext
        val service = app.getSystemService(PowerManager::class.java) ?: return
        power = service
        osStatus = service.currentThermalStatus
        bestEffort(TAG, "addThermalStatusListener") {
            service.addThermalStatusListener(app.mainExecutor) { status -> osStatus = status }
        }
    }

    /** The platform's last thermal status, or -1 while no PowerManager has answered. */
    val platformStatus: Int
        get() = if (power != null) osStatus else -1

    /** The forecast headroom the platform reports; NaN when it has none. */
    fun thermalHeadroom(): Float {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return Float.NaN
        val service = power ?: return Float.NaN
        return runCatching { service.getThermalHeadroom(FORECAST_SECONDS) }.getOrDefault(Float.NaN)
    }
}

private const val TAG = "ThermalGovernor"

/** `PowerManager.THERMAL_STATUS_NONE`. */
private const val STATUS_NONE = 0

private const val FORECAST_SECONDS = 30
