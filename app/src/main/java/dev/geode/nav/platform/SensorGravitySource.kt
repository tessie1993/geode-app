package dev.geode.nav.platform

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import dev.geode.nav.connect.Gravity
import dev.geode.nav.connect.GravitySource
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * [GravitySource] over the platform's fused gravity sensor, or the raw accelerometer on a device
 * without one. Emits at UI rate and smooths nothing; a consumer that wants it calmer does that
 * itself. Without any sensor it stays at [Gravity.REST].
 */
class SensorGravitySource(
    context: Context,
) : GravitySource,
    SensorEventListener {
    private val manager: SensorManager? = context.getSystemService(SensorManager::class.java)
    private val sensor: Sensor? =
        manager?.getDefaultSensor(Sensor.TYPE_GRAVITY) ?: manager?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)

    private val _gravity = MutableStateFlow(Gravity.REST)
    override val gravity: StateFlow<Gravity> = _gravity.asStateFlow()

    override fun start() {
        val target = sensor ?: return
        manager?.registerListener(this, target, SensorManager.SENSOR_DELAY_UI)
    }

    override fun stop() {
        manager?.unregisterListener(this)
    }

    override fun onSensorChanged(event: SensorEvent) {
        _gravity.value = Gravity(event.values[0], event.values[1], event.values[2])
    }

    override fun onAccuracyChanged(
        sensor: Sensor?,
        accuracy: Int,
    ) = Unit
}
