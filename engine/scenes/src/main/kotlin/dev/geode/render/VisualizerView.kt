package dev.geode.render

import android.content.Context
import android.opengl.GLSurfaceView
import android.view.SurfaceHolder
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

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        syncPacer()
    }

    override fun onDetachedFromWindow() {
        // Before super, which tears down the GL thread: no point asking it for one more frame.
        framePacer.stop()
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

    private fun syncPacer() {
        val watched = isAttachedToWindow && windowVisibility == VISIBLE
        if (watched) framePacer.start() else framePacer.stop()
    }

    private companion object {
        const val TAG = "VisualizerView"

        /** Long enough for one queued event, short enough that a wedged GL thread is not an ANR. */
        const val RELEASE_WAIT_MS = 200L
    }
}
