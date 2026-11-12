package dev.geode.render

import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.view.Choreographer
import android.view.Surface
import dev.geode.engine.bridge.GeodeNative

/**
 * Receives one call per paced frame, on the pacer's own thread (see [FramePacer]), never the main
 * thread. `GLSurfaceView.requestRender` is documented as callable from any thread, which is all
 * the sinks here do.
 *
 * Declared as a `fun interface` rather than `(Float) -> Unit` because a Kotlin function type
 * boxes its `Float` argument: at 120 Hz that is 120 short-lived `java.lang.Float` objects a
 * second, which is exactly the kind of steady garbage that turns into a dropped frame when a GC
 * lands mid-beat.
 */
fun interface FrameSink {
    fun onFrame(dtSeconds: Float)
}

/**
 * The rate one surface's frames are asked for ([fps], 0 when it free-runs) and its own cap before the thermal
 * step ([capFps]; a fixed rate is its own). Each renderer is handed its own ([VisualizerRenderer.pacedRate]), so a
 * deliberate cap is never mistaken for a device that cannot keep up, nor one surface's for another's (Cast's 30).
 */
fun interface PacedRate {
    fun fps(): Float

    fun capFps(): Float = fps()
}

/**
 * What the engine asks the display for.
 *
 * These are the two answers `docs/quality/bar-visualizer.md` §4.3 accepts on a 90/120 Hz panel:
 * pace to a chosen rate, or run the native rate with dt-correct animation. The third option —
 * free-running against `eglSwapBuffers` back-pressure — is what this engine did before and is
 * what produced pacing that varied with whatever the GPU happened to be doing.
 */
sealed interface FrameRatePolicy {
    /** Draw on every vsync the panel offers. Safe here only because every step is dt-driven. */
    data object Native : FrameRatePolicy

    /**
     * Never draw faster than [fps].
     *
     * Honoured by dividing the vsync cadence by a whole number, so drawn frames stay evenly
     * spaced. A fractional divide is deliberately refused: asking a 90 Hz panel for 60 fps by
     * alternating 11 ms and 22 ms frames averages out on paper and reads as judder in the eye,
     * and uneven pacing is more visible to users than a steady rate. When the divide is not
     * whole the pacer takes the next whole divide under the cap (45 fps on a 90 Hz panel), and
     * [Surface.setFrameRate] asks the system for a mode change that would make it whole.
     */
    data class Capped(
        val fps: Float,
    ) : FrameRatePolicy
}

/**
 * Choreographer-driven tick source for a visualizer surface.
 *
 * The renderer used to free-run: `RENDERMODE_CONTINUOUSLY` draws as fast as `eglSwapBuffers`
 * lets it, so the cadence was set by whatever the GPU and SurfaceFlinger queue were doing that
 * second, and on a 120 Hz panel there was no rate policy at all. This class makes vsync the
 * clock instead — one callback per display refresh, a whole-number divide down to the target
 * rate, and a measured dt handed to the sink — per `docs/quality/bar-visualizer.md` §4.3.
 *
 * Threading: the vsync callbacks arrive on one process-wide pacer thread ([PacerThread]), a
 * `HandlerThread` with its own Choreographer, not on the main thread. They used to arrive on the
 * main thread, so every Compose hitch (a recomposition, a layout pass, a GC on the UI thread)
 * also cost the visual its vsync, and UI jank read as visual stutter even with the GPU idle. A
 * Choreographer is per Looper and hears the same display vsync on any of them, so moving it
 * changes the clock's thread and nothing else.
 *
 * [start] and [stop] may be called from any thread (in practice the main thread, for both
 * `GLSurfaceView` and `WallpaperService.Engine`); they flip [running] at once, so the existing
 * idempotence stays synchronous, and hand the callback itself to the pacer thread in order.
 * Every frame-timing field below is confined to the pacer thread. [applyTo] is a Surface call
 * and runs anywhere. [fps] and [capFps] are safe to read from any thread; the dt goes to the sink.
 */
class FramePacer(
    initialPolicy: FrameRatePolicy = FrameRatePolicy.Capped(DEFAULT_TARGET_FPS),
    private val sink: FrameSink,
) : PacedRate {
    /**
     * Settable from any thread — a quality scaler reacting to thermal headroom will not be on the
     * Choreographer thread — and read back on the next vsync, which is where the divide is
     * recomputed. It does not touch the surface: call [applyTo] afterwards to push the new
     * preference to the display as well. [fps] answers the new cap at once, until a vsync says
     * what native paced it to.
     */
    @Volatile
    var policy: FrameRatePolicy = initialPolicy
        set(value) {
            field = value
            pacedMilliHz = -1L
        }

    private val callback = Choreographer.FrameCallback { frameTimeNanos -> onVsync(frameTimeNanos) }

    /** Written by [start] and [stop] on the caller's thread, read by [onVsync] on the pacer thread. */
    @Volatile
    private var running = false

    /**
     * Pacer thread only. The whole frame clock (the vsync period estimate, the divide-down and the
     * dt clamp) is native, `core/viz/frame/Pacer`; this is its state, stepped in place each vsync,
     * so a tick allocates nothing and there is no native handle to free.
     */
    private val clock = LongArray(GeodeNative.PACER_STATE_LONGS)

    /** The last surface [applyTo] reached, re-asked from the pacer thread when the thermal cap moves. */
    @Volatile
    private var surface: Surface? = null

    /** Pacer thread only: the paced cap (millihertz) the display was last asked for after a vsync. */
    private var askedCapMilliHz = -1L

    /** The rate native last paced to (45 fps for a 60 cap on a 90 Hz panel); -1 until a vsync says. */
    @Volatile
    private var pacedMilliHz = -1L

    /**
     * Posted, not run inline: the frame-timing fields and the Choreographer belong to the pacer
     * thread. Tasks run in the order they were posted, so a quick stop/start pair still ends in
     * the state of its last call. A stale start (stopped again before it ran) does nothing.
     */
    private val beginOnPacerThread =
        Runnable {
            if (!running) return@Runnable
            // A resume must not inherit the gap it was paused across.
            GeodeNative.pacerResume(clock)
            // Removed first: two starts can both find the pacer running by the time they run, and
            // a callback posted twice would fire twice per vsync and halve every dt.
            PacerThread.choreographer.removeFrameCallback(callback)
            PacerThread.choreographer.postFrameCallback(callback)
        }

    private val endOnPacerThread = Runnable { PacerThread.choreographer.removeFrameCallback(callback) }

    /** Idempotent: a second call while running is a no-op, so lifecycle callbacks can overlap. */
    fun start() {
        if (running) return
        running = true
        PacerThread.handler.post(beginOnPacerThread)
    }

    /**
     * Removes the pending vsync callback outright rather than letting it fire and skip work —
     * §4.4 of the quality bar asks for the callback itself to stop, and a wallpaper toggles
     * visibility every time the screen sleeps. The removal is posted to the pacer thread; until it
     * runs, a vsync that fires sees [running] false and neither re-posts nor draws. One frame
     * already inside [onVsync] when this is called can still reach the sink; `requestRender` on a
     * paused or exiting GL thread is harmless.
     */
    fun stop() {
        if (!running) return
        running = false
        PacerThread.handler.post(endOnPacerThread)
    }

    /** [FrameRatePolicy.Capped] asks for its rate; anything else (free-running) for 0. */
    override fun capFps(): Float =
        when (val current = policy) {
            FrameRatePolicy.Native -> 0f
            is FrameRatePolicy.Capped -> current.fps
        }

    /** This pacer's own rate, whichever others run: what native last paced it to, or its cap until a vsync says. */
    override fun fps(): Float = pacedMilliHz.let { if (it < 0L) capFps() else it / MILLI_HZ }

    /**
     * Publishes the rate preference to the display. Call it from `surfaceCreated` and
     * `surfaceChanged`, and again after assigning [policy].
     *
     * The hint is what lets a 120 Hz panel drop to a 60 Hz mode for us instead of us throwing
     * away every second frame. We pass the two-argument overload deliberately, so the platform
     * is free to ignore the request when the mode change would not be seamless: a panel that
     * blanks for a moment mid-track is a worse artefact than the frame rate we were trying to fix.
     */
    fun applyTo(surface: Surface) {
        this.surface = surface
        if (surface.isValid) {
            // Zero is the platform's "no preference": it clears any standing request instead of
            // leaving the last cap latched on a surface that has since changed its mind. The rate is
            // the effective cap (this surface's under the thermal one), so a hot phone's 30 fps can
            // drop the panel to a 30/60 Hz mode rather than skipping vsyncs on a 120 Hz one.
            // The surface can be torn down between the lifecycle callback and this line.
            val fps = GeodeNative.pacerEffectiveCap(capFps())
            runCatching { surface.setFrameRate(fps, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT) }
        }
    }

    private fun onVsync(frameTimeNanos: Long) {
        if (!running) return
        // Re-post first: the cadence has to survive a slow sink, and a frame we skip still has to
        // wake us for the next one.
        PacerThread.choreographer.postFrameCallback(callback)
        // The policy is read here, on the pacer thread, each vsync, so a cap change lands next tick.
        val dt = GeodeNative.pacerOnVsync(clock, frameTimeNanos, capFps()).toFloat()
        // The thermal cap moves without a lifecycle callback; native records the cap it paced to.
        val cap = clock[GeodeNative.PACER_CAP_SLOT]
        if (cap != askedCapMilliHz) {
            askedCapMilliHz = cap
            surface?.let { applyTo(it) }
        }
        pacedMilliHz = clock[GeodeNative.PACER_PACED_SLOT]
        if (dt <= 0f) return
        sink.onFrame(dt)
    }

    companion object {
        /**
         * The default cap, where the scenes are tuned. Above it the fragment cost of a full-screen visualizer
         * scales linearly and the phone reaches thermal throttling sooner, so 90 or 120 is the user's own pick
         * (Settings › Performance on a fast panel, `core/viz/frame/LiveFps`), stepped down first when hot.
         */
        const val DEFAULT_TARGET_FPS = 60f

        private const val MILLI_HZ = 1000f
    }
}

/**
 * The one thread every [FramePacer] in the process is clocked on: the in-app view's, each
 * wallpaper engine's and the dream's. One is enough, since a callback only counts vsyncs and
 * calls `requestRender`, and sharing it keeps a preview wallpaper beside the app from costing a
 * second idle thread. Started on first use and never quit: an idle Looper costs nothing, and a
 * thread torn down and rebuilt on every pause would race the next start.
 *
 * Display priority, the same class as the UI and RenderThreads, so a busy background (a render,
 * a library scan) does not delay the tick it exists to protect.
 */
private object PacerThread {
    private val thread = HandlerThread("GeodeFramePacer", Process.THREAD_PRIORITY_DISPLAY).apply { start() }

    val handler = Handler(thread.looper)

    /**
     * Pacer thread only. `Choreographer.getInstance()` returns the calling Looper's instance, so
     * it must first be read on this thread, which the lazy guarantees: only code that runs on it
     * ([FramePacer]'s posted tasks and vsync callbacks) touches this.
     */
    val choreographer: Choreographer by lazy(LazyThreadSafetyMode.NONE) { Choreographer.getInstance() }
}
