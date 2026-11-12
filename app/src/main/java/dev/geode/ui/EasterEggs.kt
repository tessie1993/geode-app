package dev.geode.ui

import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.widget.Toast
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.lifecycle.compose.LifecycleResumeEffect
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.performStoneHaptic

/**
 * The two hidden Visuals gestures, free for everyone. Both decisions are native: a shake is
 * `core/viz/Shake`, a tap on the beat `core/viz/TapAlong`; this only carries the sensor's samples and
 * the taps across and acts on the answer.
 *
 * Shake to screenshot: the accelerometer is listened to only while Visuals is composed in a resumed
 * host ([enabled] off in PiP) and let go on pause, like the gyro look. A shake saves the visual alone
 * (`geode_viz_still_request`: the frame before the UI blur, no app chrome) to Pictures, with a light
 * haptic and a toast. It touches nothing the renderer draws.
 */
@Composable
internal fun ShakeToScreenshot(
    enabled: Boolean,
    visualizerView: VisualizerView,
    viewModel: VisualsViewModel,
) {
    val context = LocalContext.current
    val view = LocalView.current
    val shot by rememberUpdatedState {
        view.performStoneHaptic(StoneHapticCue.TAP)
        visualizerView.visualizerRenderer.nativeViz.requestStill { still ->
            view.post {
                viewModel.saveStill(still) { saved ->
                    val text = if (saved) R.string.visuals_screenshot_saved else R.string.visuals_screenshot_failed
                    Toast.makeText(context, text, Toast.LENGTH_SHORT).show()
                }
            }
        }
    }
    LifecycleResumeEffect(context, enabled) {
        val sensors = context.getSystemService(SensorManager::class.java)
        val sensor = sensors?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        val shake = if (enabled && sensor != null) GeodeNative.shakeCreate() else 0L
        if (sensors == null || shake == 0L) return@LifecycleResumeEffect onPauseOrDispose {}
        val listener =
            object : SensorEventListener {
                override fun onSensorChanged(event: SensorEvent) {
                    val v = event.values
                    if (GeodeNative.shakeSample(shake, event.timestamp, v[0], v[1], v[2])) shot()
                }

                override fun onAccuracyChanged(
                    sensor: Sensor?,
                    accuracy: Int,
                ) = Unit
            }
        // Main-thread callbacks, so the unregister below comes before the handle goes.
        sensors.registerListener(listener, sensor, SensorManager.SENSOR_DELAY_GAME)
        onPauseOrDispose {
            sensors.unregisterListener(listener)
            GeodeNative.shakeDestroy(shake)
        }
    }
}

/**
 * Tap along: every tap's touch-down on the visual goes to the renderer's matcher, which compares it
 * with the beat its sound engine already heard and never feeds it back (no tap tempo). A streak on the
 * beat rolls Customize › Music (new moments and reactions, a random roll, Keep respected), with a light
 * haptic. The tap still does what the gesture map gives it.
 */
@Composable
internal fun rememberTapAlong(
    visualizerView: VisualizerView,
    viewModel: VisualsViewModel,
): (Long) -> Unit {
    val view = LocalView.current
    val roll by rememberUpdatedState {
        view.performStoneHaptic(StoneHapticCue.TAP)
        viewModel.tapAlongRoll()
    }
    return { uptimeMs -> if (visualizerView.visualizerRenderer.nativeViz.tapAlong(uptimeMs * NANOS_PER_MILLI)) roll() }
}

private const val NANOS_PER_MILLI = 1_000_000L
