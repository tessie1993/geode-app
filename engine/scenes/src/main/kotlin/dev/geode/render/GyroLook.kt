package dev.geode.render

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import kotlin.math.PI

/**
 * Phone tilt as a camera look, relative to how the phone was held when it was switched on.
 *
 * The game rotation vector, not the plain one: it has no magnetometer in it, so a speaker or a
 * metal table cannot swing the view. Only live view uses this; an export never sees a gyro and
 * flies the recorded path.
 */
class GyroLook(
    context: Context,
    private val onLook: (yaw: Float, pitch: Float) -> Unit,
) : SensorEventListener {
    private val sensors = context.getSystemService(SensorManager::class.java)
    private val sensor = sensors?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
    private val rotation = FloatArray(9)
    private val angles = FloatArray(3)
    private var baseYaw = Float.NaN
    private var basePitch = 0f
    private var listening = false

    /** Registers or unregisters; turning it off recentres the look. */
    fun setActive(on: Boolean) {
        if (on == listening || sensor == null || sensors == null) return
        listening = on
        if (on) {
            baseYaw = Float.NaN
            sensors.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME)
        } else {
            sensors.unregisterListener(this)
            onLook(0f, 0f)
        }
    }

    override fun onSensorChanged(event: SensorEvent) {
        SensorManager.getRotationMatrixFromVector(rotation, event.values)
        SensorManager.getOrientation(rotation, angles)
        if (baseYaw.isNaN()) {
            baseYaw = angles[0]
            basePitch = angles[1]
        }
        onLook(
            (wrap(angles[0] - baseYaw) / FULL_LOOK_RADIANS).coerceIn(-1f, 1f),
            ((angles[1] - basePitch) / FULL_LOOK_RADIANS).coerceIn(-1f, 1f),
        )
    }

    override fun onAccuracyChanged(
        sensor: Sensor?,
        accuracy: Int,
    ) = Unit

    private fun wrap(a: Float): Float {
        var x = a
        while (x > PI) x -= TWO_PI
        while (x < -PI) x += TWO_PI
        return x
    }

    private companion object {
        /** Tilting this far reads as a full look to one side. */
        const val FULL_LOOK_RADIANS = 0.6f
        const val TWO_PI = (2 * PI).toFloat()
    }
}
