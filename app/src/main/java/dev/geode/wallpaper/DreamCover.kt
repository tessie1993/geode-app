package dev.geode.wallpaper

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager

/**
 * Whether something covers the screen the dream is on: the proximity sensor reads near.
 *
 * A dream runs on the charger, and a charging phone is often face down on the table or in a
 * pocket on a car mount's cable. Its window stays "visible" there, so without this the visual
 * renders at full rate onto a screen pressed against wood. This is the dream's version of the
 * wallpaper's shade cover (`Engine.onZoomChanged`), which a dream does not get.
 *
 * The non-wakeup proximity sensor where the phone has one, registered only while dreaming: it is
 * a low-power sensor the phone already runs for calls, and it never wakes the CPU on its own.
 * Proximity is declared wake-up by default, so many phones have only the wake-up one; there it is
 * the fallback, and since it is registered only while the dream shows (screen on, CPU awake on the
 * charger) its wake-ups cost next to nothing. A phone with neither is never covered. Binary sensors report 0 or their maximum range; continuous ones report
 * centimetres, so near is anything under [NEAR_CM] or under the range, whichever is smaller.
 */
class DreamCover(
    context: Context,
    /** Called on the main thread when [covered] flips. */
    private val onChanged: () -> Unit,
) : SensorEventListener {
    private val sensors = context.getSystemService(SensorManager::class.java)
    // Asked for by wake-up flag: the one-argument getDefaultSensor returns the wake-up proximity
    // sensor, which wakes the CPU on every near/far change (#388). The non-wakeup one is optional,
    // so fall back to the wake-up one rather than never covering.
    private val proximity: Sensor? =
        sensors?.getDefaultSensor(Sensor.TYPE_PROXIMITY, false)
            ?: sensors?.getDefaultSensor(Sensor.TYPE_PROXIMITY)
    private var listening = false

    var covered: Boolean = false
        private set

    fun start() {
        val sensor = proximity ?: return
        if (listening) return
        listening = sensors?.registerListener(this, sensor, SensorManager.SENSOR_DELAY_NORMAL) == true
    }

    fun stop() {
        if (listening) sensors?.unregisterListener(this)
        listening = false
        // Uncovered at rest, so the next start never begins paused on a stale reading.
        covered = false
    }

    override fun onSensorChanged(event: SensorEvent) {
        val sensor = proximity ?: return
        val distance = event.values.firstOrNull() ?: return
        val near = distance < minOf(NEAR_CM, sensor.maximumRange)
        if (near == covered) return
        covered = near
        onChanged()
    }

    override fun onAccuracyChanged(
        sensor: Sensor?,
        accuracy: Int,
    ) = Unit

    private companion object {
        const val NEAR_CM = 3f
    }
}
