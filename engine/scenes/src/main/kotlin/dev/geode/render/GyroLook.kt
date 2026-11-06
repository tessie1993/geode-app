package dev.geode.render

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager

/**
 * The phone's tilt, handed to the renderer for `core/viz/GyroLook`, which turns it into a camera
 * look relative to how the phone was held when it was switched on (baseline, yaw wrap, clamp).
 *
 * The game rotation vector, not the plain one: it has no magnetometer in it, so a speaker or a
 * metal table cannot swing the view. Only live view uses this; an export never sees a gyro and
 * flies the recorded path.
 */
class GyroLook(
    context: Context,
    private val renderer: VisualizerRenderer,
) : SensorEventListener {
    private val sensors = context.getSystemService(SensorManager::class.java)
    private val sensor = sensors?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
    private val rotation = FloatArray(9)
    private val angles = FloatArray(3)
    private var listening = false

    /** Registers or unregisters; native recaptures on the next switch on and recentres on off. */
    fun setActive(on: Boolean) {
        if (on == listening || sensor == null || sensors == null) return
        listening = on
        renderer.gyroAzimuth = Float.NaN
        if (on) {
            renderer.gyroSession++
            sensors.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME)
        } else {
            sensors.unregisterListener(this)
        }
    }

    override fun onSensorChanged(event: SensorEvent) {
        SensorManager.getRotationMatrixFromVector(rotation, event.values)
        SensorManager.getOrientation(rotation, angles)
        renderer.gyroPitch = angles[1]
        renderer.gyroAzimuth = angles[0]
    }

    override fun onAccuracyChanged(
        sensor: Sensor?,
        accuracy: Int,
    ) = Unit
}
