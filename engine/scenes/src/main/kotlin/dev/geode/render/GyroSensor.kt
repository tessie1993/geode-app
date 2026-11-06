package dev.geode.render

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager

/**
 * The sensor half of phone tilt as a camera look: registers the game rotation vector (no
 * magnetometer, so a speaker or a metal table cannot swing the view) and hands [onLook] the raw
 * yaw and pitch in radians, NaN while off, with how many times it has registered. Native
 * (`viz::GyroLook` in core/viz/CameraRig) makes them a look relative to how the phone was held when
 * it came on, so a new registration is a new base even if no frame saw the NaN. Live view only.
 */
class GyroSensor(
    context: Context,
    private val onLook: (yaw: Float, pitch: Float, registration: Int) -> Unit,
) : SensorEventListener {
    private val sensors = context.getSystemService(SensorManager::class.java)
    private val sensor = sensors?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
    private val rotation = FloatArray(9)
    private val angles = FloatArray(3)
    private var listening = false
    private var registration = 0

    /** Registers or unregisters; turning it off recentres the look, and on again takes a new base. */
    fun setActive(on: Boolean) {
        if (on == listening || sensor == null || sensors == null) return
        listening = on
        if (on) {
            registration++
            sensors.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME)
        } else {
            sensors.unregisterListener(this)
            onLook(Float.NaN, Float.NaN, registration)
        }
    }

    override fun onSensorChanged(event: SensorEvent) {
        SensorManager.getRotationMatrixFromVector(rotation, event.values)
        SensorManager.getOrientation(rotation, angles)
        onLook(angles[0], angles[1], registration)
    }

    override fun onAccuracyChanged(
        sensor: Sensor?,
        accuracy: Int,
    ) = Unit
}
