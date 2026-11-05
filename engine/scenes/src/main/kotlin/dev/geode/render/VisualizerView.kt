package dev.geode.render

import android.content.Context
import android.opengl.GLSurfaceView
import android.view.SurfaceHolder
import android.view.View
import dev.geode.util.bestEffort
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class VisualizerView(
    context: Context,
) : GLSurfaceView(context) {
    val visualizerRenderer: VisualizerRenderer = VisualizerRenderer(context)

    /**
     * Vsync, not `eglSwapBuffers` back-pressure, is what asks for a frame here. `requestRender`
     * coalesces, so if the GPU falls behind the tick the requests collapse into one instead of
     * queueing a backlog the scene would later have to catch up on.
     *
     * Public because the pacer is also where the frame-time distribution lives and where the
     * frame-rate cap is set: a quality scaler reading `stats()` and writing `policy` needs no
     * other handle into the render loop.
     */
    val framePacer: FramePacer = FramePacer { requestRender() }

    private val gyroSensor =
        GyroSensor(context) { yaw, pitch, registration ->
            visualizerRenderer.gyroRegistration = registration
            visualizerRenderer.gyroYaw = yaw
            visualizerRenderer.gyroPitch = pitch
        }

    /** The camera's "Tilt phone to look" setting; the sensor only runs while this view is also watched. */
    var gyroWanted: Boolean = false
        set(on) {
            field = on
            syncPacer()
        }

    /**
     * Between the host's [onResume] and [onPause]. A new GLSurfaceView runs until it is first
     * paused, so this starts true; it gates only the sensor, the pacer keeps its own rules.
     */
    private var resumed = true

    /**
     * GLSurfaceView installs its own holder callback; a second one is additive. The surface is
     * the only place the frame-rate preference can be stated, and it has to be restated on every
     * `surfaceChanged` because a rotation or a fold hands us a new one.
     */
    private val frameRateCallback =
        object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) = framePacer.applyTo(holder.surface)

            override fun surfaceChanged(
                holder: SurfaceHolder,
                format: Int,
                width: Int,
                height: Int,
            ) = framePacer.applyTo(holder.surface)

            override fun surfaceDestroyed(holder: SurfaceHolder) = Unit
        }

    init {
        setEGLContextClientVersion(3)
        // Asked for before the renderer is attached: the host stops this view whenever it goes
        // invisible, and rebuilding every scene, shader and FBO on each resume would cost a visible
        // black flash. The driver is free to refuse, which is why onSurfaceCreated stays re-entrant.
        preserveEGLContextOnPause = true
        setRenderer(visualizerRenderer)
        // RENDERMODE_WHEN_DIRTY does not mean "draw rarely": the pacer drives it every vsync it
        // decides to keep. It means the GL thread idles the moment the pacer stops, which is what
        // makes the window-invisible path below cost nothing.
        renderMode = RENDERMODE_WHEN_DIRTY
        holder.addCallback(frameRateCallback)
    }

    /**
     * Settings › Performance for this surface: quality to the renderer, the fps cap to the pacer.
     * Main thread, like every other pacer call; the new cap reaches the display at once.
     */
    fun applyPerformance(settings: PerformanceSettings) {
        visualizerRenderer.performance = settings
        framePacer.policy = FrameRatePolicy.Capped(settings.fpsCap.toFloat())
        holder.surface?.takeIf { it.isValid }?.let(framePacer::applyTo)
    }

    /**
     * Frosted glass for a Compose sheet floating over this visual. The sheet lives in another layer
     * (Compose over this GLSurfaceView), so a `RenderEffect` blur there never sees the visual's
     * pixels; instead the visual blurs the region under the sheet itself (`core/viz/UiBlurPass`,
     * after the composite, at quarter resolution), and the sheet draws its translucent matte glass
     * over that.
     *
     * When: whenever the sheet's bounds, corner radius or glass strength change (open, drag,
     * settle, resize, rotate), and with `strength = 0f` when it closes. Each call is one small event
     * on the GL thread, latched for the next frame; the region is kept across surface and native-handle rebuilds, so call again
     * only when something changes. Main thread is fine: the call is posted to the GL thread.
     *
     * Coordinates: [left], [top], [right], [bottom] are the sheet's bounds in this view's own pixel
     * space divided by this view's width and height, so 0..1 with y DOWN (`top = 0` is the view's top
     * edge), e.g. from `boundsInWindow` minus this view's window offset, over `width` / `height`.
     * [strength] 0..1 mixes the blur in (0 = off, no GPU work at all). [cornerPx] is the sheet's
     * corner radius in pixels (this view's pixels, which are the GL surface's).
     *
     * Live views only: exports render offscreen and never draw it.
     */
    fun setUiBlurRegion(
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    ) {
        queueEvent { visualizerRenderer.setUiBlurRegion(left, top, right, bottom, strength, cornerPx) }
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        syncPacer()
    }

    /**
     * The activity forwards its pause here (AppShell). The sensor stops with it: a rotation-vector
     * listener left registered behind a paused activity keeps the sensor hub awake for nothing.
     */
    override fun onPause() {
        resumed = false
        gyroSensor.setActive(false)
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        syncPacer()
    }

    override fun onDetachedFromWindow() {
        // Before super, which tears down the GL thread: no point asking it for one more frame.
        framePacer.stop()
        // The app now hides this view rather than detaching it (see onVisibilityChanged), so this
        // runs only when a second screen takes it or the activity goes. isAttachedToWindow is
        // still true until super returns, so the sensor is stopped outright; re-attaching runs
        // syncPacer again.
        gyroSensor.setActive(false)
        releaseScenesOnGlThread()
        super.onDetachedFromWindow()
    }

    /**
     * Hands the GL thread its last job while it is still taking them.
     *
     * GLSurfaceView's thread stops draining its event queue once it has been asked to exit, so
     * anything queued after super is silently dropped - which is why this runs before it, and
     * why it waits: the thread is gone by the time super returns. The wait is bounded because a
     * wedged GL thread must not turn a teardown into an ANR; the leak it prevents is finite and
     * an ANR is not.
     *
     * Still taking jobs is NOT the same as still having a context. By the time a view is detached
     * the surface is gone and GLSurfaceView has already unbound the context
     * (`eglMakeCurrent(.., EGL_NO_CONTEXT)`), so the job below runs contextless: the native
     * projectM destroy it exists for still frees the native heap, but every GL delete along the
     * way is a no-op and the driver logs one `E/libEGL: call to OpenGL ES API with no current
     * context` per thread. That log line is expected here. See SceneRegistry.releaseAll.
     */
    private fun releaseScenesOnGlThread() {
        val done = CountDownLatch(1)
        queueEvent {
            bestEffort(TAG, "visualizerRenderer.releaseScenes()") { visualizerRenderer.releaseScenes() }
            done.countDown()
        }
        bestEffort(TAG, "await scene release") { done.await(RELEASE_WAIT_MS, TimeUnit.MILLISECONDS) }
    }

    /**
     * The activity keeps this view alive across rotation (`configChanges` in the manifest) and
     * across a trip to another screen, so window visibility, not the view's own lifecycle, is
     * the signal that nobody is looking. Backgrounded, the surface can outlive the moment it was
     * last seen, and a free-running visualizer would keep drawing into it.
     */
    override fun onWindowVisibilityChanged(visibility: Int) {
        super.onWindowVisibilityChanged(visibility)
        syncPacer()
    }

    /**
     * The app keeps this view attached on every screen and hides it (`INVISIBLE`) off Visuals,
     * because detaching it cost a main-thread wait for the scene release, a GL thread join, and on
     * the way back a new EGL context, every program recompiled and a black first frame. Hidden,
     * SurfaceView gives its surface back and GLSurfaceView keeps its thread and context, so only
     * the pacer and the gyro have to stop here; showing it again is a new surface and one
     * `onSurfaceChanged`. Called for this view and for any ancestor; the attach check keeps it
     * out of the constructor, where a View can already be told about its visibility.
     */
    override fun onVisibilityChanged(
        changedView: View,
        visibility: Int,
    ) {
        super.onVisibilityChanged(changedView, visibility)
        if (isAttachedToWindow) syncPacer()
    }

    private fun syncPacer() {
        // isShown: this view and every ancestor VISIBLE, in a window. Window visibility on its own
        // misses the in-app hide, which keeps the window showing.
        val watched = isAttachedToWindow && windowVisibility == VISIBLE && isShown
        if (watched) framePacer.start() else framePacer.stop()
        gyroSensor.setActive(gyroListens(watched, resumed, gyroWanted))
    }

    private companion object {
        const val TAG = "VisualizerView"

        /** Long enough for one queued event, short enough that a wedged GL thread is not an ANR. */
        const val RELEASE_WAIT_MS = 200L
    }
}

/**
 * The gyro runs only for a view someone can see, in a resumed host, with the setting on. Top level
 * rather than in [VisualizerView]'s companion so a JVM test can call it without loading GLSurfaceView.
 */
internal fun gyroListens(
    watched: Boolean,
    resumed: Boolean,
    wanted: Boolean,
): Boolean = watched && resumed && wanted
