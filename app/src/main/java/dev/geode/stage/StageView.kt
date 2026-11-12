package dev.geode.stage

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import dev.geode.data.PerformanceSettings
import dev.geode.engine.bridge.GeodeNative

/**
 * The surface a [Stage] draws into. It tells native what Android tells it (the surface, its size, whether the
 * view is attached, visible and opaque, the host's pause) and decides nothing: when the stage draws, sheds or
 * listens to the tilt sensor is native's answer.
 *
 * The stage outlives the surface and a detach (a second screen takes the view and gives it back), so whoever
 * made the view ends it with `stage.close()`.
 */
class StageView(
    context: Context,
    private val kind: Int = GeodeNative.STAGE_KIND_VIEW,
    capFps: Float = DEFAULT_CAP_FPS,
) : SurfaceView(context),
    SurfaceHolder.Callback2,
    SensorEventListener {
    val stage = Stage(context, kind, capFps)

    private val sensors = context.getSystemService(SensorManager::class.java)

    // The game rotation vector, not the plain one: no magnetometer, so a speaker cannot swing the view.
    private val tilt = sensors?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
    private var tiltOn = false

    /** The camera's "Tilt phone to look" setting; native says when the sensor runs. */
    var gyroWanted: Boolean = false
        set(on) {
            field = on
            syncTilt()
        }

    init {
        holder.addCallback(this)
    }

    /** Settings › Performance for this surface: quality and the fps cap. */
    fun applyPerformance(settings: PerformanceSettings) {
        stage.performance = settings
        stage.capFps = settings.fpsCap.toFloat()
    }

    /**
     * Frosted glass for a Compose sheet floating over this visual. The sheet lives in another layer, so a
     * `RenderEffect` blur there never sees the visual's pixels; the visual blurs the region under the sheet
     * itself (`core/viz/UiBlurPass`) and the sheet draws its translucent glass over that.
     *
     * [left], [top], [right], [bottom] are the sheet's bounds over this view's width and height, 0..1 with y
     * down. [strength] 0..1 mixes the blur in (0 = off, no GPU work). [cornerPx] is the sheet's corner radius
     * in this view's pixels. Call when one of them changes; the stage keeps the region.
     */
    fun setUiBlurRegion(
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    ) = stage.setUiBlurRegion(left, top, right, bottom, strength, cornerPx)

    /** The host's pause and resume (the activity's, or the dream's). */
    fun onPause() {
        stage.pause()
        syncTilt()
    }

    fun onResume() {
        stage.resume()
        syncTilt()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val frame = holder.surfaceFrame
        stage.setWindow(holder.surface, frame.width(), frame.height())
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int,
    ) = stage.setSize(width, height)

    override fun surfaceRedrawNeeded(holder: SurfaceHolder) = stage.redraw()

    override fun surfaceDestroyed(holder: SurfaceHolder) = stage.setWindow(null, 0, 0)

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        restate(attached = true)
    }

    override fun onDetachedFromWindow() {
        // isAttachedToWindow is still true here, so the detach is stated outright.
        restate(attached = false)
        super.onDetachedFromWindow()
    }

    override fun onWindowVisibilityChanged(visibility: Int) {
        super.onWindowVisibilityChanged(visibility)
        restate(isAttachedToWindow)
    }

    // The two below also run inside the super constructor, before `stage` exists: only an attached view restates.
    override fun onVisibilityChanged(
        changedView: View,
        visibility: Int,
    ) {
        super.onVisibilityChanged(changedView, visibility)
        if (isAttachedToWindow) restate(attached = true)
    }

    /** Off Visuals the app hides this view with alpha 0: `INVISIBLE` would give the surface back. */
    override fun setAlpha(alpha: Float) {
        super.setAlpha(alpha)
        if (isAttachedToWindow) restate(attached = true)
    }

    /** What the view sees of itself, as is; a surface stage's rule is its host's ([Stage.setSurface]). */
    private fun restate(attached: Boolean) {
        if (kind != GeodeNative.STAGE_KIND_VIEW) return
        val flags =
            (if (attached) GeodeNative.STAGE_VIEW_ATTACHED else 0) or
                (if (windowVisibility == VISIBLE) GeodeNative.STAGE_VIEW_WINDOW_VISIBLE else 0) or
                (if (isShown) GeodeNative.STAGE_VIEW_SHOWN else 0) or
                (if (alpha > 0f) GeodeNative.STAGE_VIEW_OPAQUE else 0)
        stage.setView(flags)
        syncTilt()
    }

    private fun syncTilt() {
        val sensor = tilt ?: return
        val on = stage.gyroListens(gyroWanted)
        if (on == tiltOn) return
        tiltOn = on
        if (on) sensors?.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME) else sensors?.unregisterListener(this)
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (!tiltOn) return // one queued as the sensor stopped would start a new session
        val v = event.values
        stage.gyroPush(v[0], v[1], v[2], v[3], display?.rotation ?: 0)
    }

    override fun onAccuracyChanged(
        sensor: Sensor?,
        accuracy: Int,
    ) = Unit

    private companion object {
        const val DEFAULT_CAP_FPS = 60f
    }
}
