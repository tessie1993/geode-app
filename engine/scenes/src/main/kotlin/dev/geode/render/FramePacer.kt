package dev.geode.render

import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.view.Choreographer
import android.view.Surface

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
     * and uneven pacing is more visible to users than a higher steady rate. When the divide is
     * not whole the pacer stays at the panel rate and leaves the reduction to
     * [Surface.setFrameRate], which can ask the system for a seamless mode change instead.
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
 * idempotence and the [ThermalGovernor] cap publish stay synchronous, and hand the callback
 * itself to the pacer thread in order. Every frame-timing field below is confined to the pacer
 * thread. [applyTo] is a Surface call and runs anywhere. [dtSeconds] is safe to read from any
 * thread.
 */
class FramePacer(
    initialPolicy: FrameRatePolicy = FrameRatePolicy.Capped(DEFAULT_TARGET_FPS),
    private val sink: FrameSink,
) {
    /**
     * Settable from any thread — a quality scaler reacting to thermal headroom will not be on the
     * Choreographer thread — and read back on the next vsync, which is where the divide is
     * recomputed. It does not touch the surface: call [applyTo] afterwards to push the new
     * preference to the display as well.
     *
     * Also re-publishes [ThermalGovernor.pacedFps] while this pacer is running, so a cap change
     * reaches the native governor immediately instead of waiting for the next [start]. A no-op
     * publish when another, more recently started pacer is the one currently active.
     */
    @Volatile
    var policy: FrameRatePolicy = initialPolicy
        set(value) {
            field = value
            if (running) publishActiveCap()
        }

    /**
     * Seconds between the last two drawn frames, clamped to [MIN_DT_SECONDS]..[MAX_DT_SECONDS].
     *
     * The upper bound matches the clamp the field-simulation scenes already apply to their own
     * step (`AcidScene`, `CymaticsScene`, `LifeScene`, `MycoScene`, `SilkScene`
     * all use 1/15 s): past that, an integrator that was tuned at 60 Hz stops being merely slow
     * and starts diverging. A single stalled frame must not be allowed to blow up a simulation
     * that has been running all evening.
     */
    @Volatile
    var dtSeconds: Float = DEFAULT_DT_SECONDS
        private set

    private val callback = Choreographer.FrameCallback { frameTimeNanos -> onVsync(frameTimeNanos) }

    /** Written by [start] and [stop] on the caller's thread, read by [onVsync] on the pacer thread. */
    @Volatile
    private var running = false

    // Pacer thread only, from here down to [divisor].
    private var lastDrawnNanos = 0L
    private var lastVsyncNanos = 0L
    private var vsyncPeriodNanos = 0L
    private var vsyncSamples = 0
    private var vsyncsSinceDrawn = 0
    private var divisor = 1

    /**
     * Posted, not run inline: the frame-timing fields and the Choreographer belong to the pacer
     * thread. Tasks run in the order they were posted, so a quick stop/start pair still ends in
     * the state of its last call. A stale start (stopped again before it ran) does nothing.
     */
    private val beginOnPacerThread =
        Runnable {
            if (!running) return@Runnable
            // A resume must not inherit the gap it was paused across.
            lastDrawnNanos = 0L
            lastVsyncNanos = 0L
            vsyncsSinceDrawn = 0
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
        // A pacer that starts twice without stopping (start() is idempotent, so this only runs
        // once per start) must not end up twice in the stack ahead of itself.
        synchronized(runningStack) {
            runningStack.remove(this)
            runningStack.add(this)
        }
        publishActiveCap()
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
        // Drop out of the running stack before republishing: if another pacer (the view's and the
        // wallpaper's can both be live at once) is still running, its cap — not 0 — is what
        // ThermalGovernor should read next.
        synchronized(runningStack) { runningStack.remove(this) }
        publishActiveCap()
    }

    /** [FrameRatePolicy.Capped] publishes its rate; anything else (free-running) publishes 0. */
    private fun requestedFps(): Float =
        when (val current = policy) {
            FrameRatePolicy.Native -> 0f
            is FrameRatePolicy.Capped -> current.fps
        }

    /**
     * Publishes [ThermalGovernor.pacedFps] from the most recently started pacer that is still
     * running — the view and the wallpaper can each own a running pacer at once, so the last one
     * to start wins, and a pacer that stops without being the most recent starter must not
     * clobber the cap the other one is still asking for. 0 when nothing is running.
     */
    private fun publishActiveCap() {
        val active = synchronized(runningStack) { runningStack.lastOrNull() }
        ThermalGovernor.pacedFps = active?.requestedFps() ?: 0f
    }

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
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && surface.isValid) {
            // Zero is the platform's "no preference": it clears any standing request instead of
            // leaving the last cap latched on a surface that has since changed its mind.
            val requested =
                when (val current = policy) {
                    FrameRatePolicy.Native -> 0f
                    is FrameRatePolicy.Capped -> current.fps
                }
            // The surface can be torn down between the lifecycle callback and this line.
            runCatching {
                surface.setFrameRate(requested, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
            }
        }
    }

    private fun onVsync(frameTimeNanos: Long) {
        if (!running) return
        // Re-post first: the cadence has to survive a slow sink, and a frame we skip still has to
        // wake us for the next one.
        PacerThread.choreographer.postFrameCallback(callback)
        trackVsyncPeriod(frameTimeNanos)
        // Recomputed here, on the Choreographer thread, rather than in the [policy] setter: it
        // reads the vsync estimate, and a setter racing this thread over a 64-bit field is a bug
        // waiting for the one 32-bit device that tears it.
        recomputeDivisor()
        vsyncsSinceDrawn++
        if (vsyncsSinceDrawn < divisor) return
        vsyncsSinceDrawn = 0
        draw(frameTimeNanos)
    }

    private fun draw(frameTimeNanos: Long) {
        val previous = lastDrawnNanos
        lastDrawnNanos = frameTimeNanos
        if (previous == 0L) {
            // First frame of a run has no measurable interval. A nominal dt beats a zero (which
            // freezes every envelope) or a clamp-sized one (which jumps the scene on resume).
            dtSeconds = DEFAULT_DT_SECONDS
            sink.onFrame(DEFAULT_DT_SECONDS)
            return
        }
        val elapsedNanos = frameTimeNanos - previous
        val dt = (elapsedNanos / NANOS_PER_SECOND).coerceIn(MIN_DT_SECONDS, MAX_DT_SECONDS)
        dtSeconds = dt
        sink.onFrame(dt)
    }

    /**
     * Tracks the panel's refresh period from consecutive callbacks — they arrive every vsync
     * whether or not we drew — so the divide-down works without a Display handle, and follows the
     * panel when [applyTo]'s hint actually lands and the mode changes underneath us.
     */
    private fun trackVsyncPeriod(frameTimeNanos: Long) {
        val previous = lastVsyncNanos
        lastVsyncNanos = frameTimeNanos
        if (previous == 0L) return
        val delta = frameTimeNanos - previous
        // Outside the range of any real panel this is a pacer-thread hitch, not a refresh period;
        // folding it in would briefly halve the estimate and stutter the divide.
        if (delta < MIN_VSYNC_NANOS || delta > MAX_VSYNC_NANOS) return
        vsyncPeriodNanos =
            if (vsyncSamples == 0) {
                delta
            } else {
                vsyncPeriodNanos + (delta - vsyncPeriodNanos) / VSYNC_SMOOTHING
            }
        if (vsyncSamples < VSYNC_SETTLE_FRAMES) vsyncSamples++
    }

    private fun recomputeDivisor() {
        divisor =
            when (val current = policy) {
                FrameRatePolicy.Native -> 1
                is FrameRatePolicy.Capped -> cappedDivisor(current.fps)
            }
    }

    private fun cappedDivisor(fps: Float): Int {
        // Until the period estimate settles, draw every vsync: a few frames of extra work costs
        // nothing, while guessing a divisor of 2 on a 60 Hz panel would open on visible judder.
        if (fps <= 0f || vsyncPeriodNanos <= 0L || vsyncSamples < VSYNC_SETTLE_FRAMES) return 1
        val panelHz = NANOS_PER_SECOND / vsyncPeriodNanos
        // The epsilon absorbs measurement noise: a 120 Hz panel that measures 119.6 Hz must still
        // divide by 2, not fall off the truncation edge to 1 and render at full rate.
        return ((panelHz / fps) + DIVISOR_EPSILON).toInt().coerceIn(1, MAX_DIVISOR)
    }

    companion object {
        /**
         * Every currently running pacer, in start order — the view's and the wallpaper's can both
         * be live at once. [publishActiveCap] reads the last entry, so the most recently started
         * pacer's cap is always what [ThermalGovernor.pacedFps] reflects, and a pacer that stops
         * without being that entry leaves the value alone.
         */
        private val runningStack = mutableListOf<FramePacer>()

        /**
         * The rate the scenes are authored and tuned at. Above it the fragment cost of a
         * full-screen visualizer scales linearly for motion that is already smooth, and the
         * phone reaches thermal throttling sooner — which costs more frames than it bought.
         */
        const val DEFAULT_TARGET_FPS = 60f

        /** See [dtSeconds]; matches the per-scene clamp already used across the field sims. */
        const val MAX_DT_SECONDS = 1f / 15f

        /** Mirrors the renderer's existing 1 ms floor, which keeps 1/dt out of the weeds. */
        const val MIN_DT_SECONDS = 0.001f

        private const val DEFAULT_DT_SECONDS = 1f / 60f

        private const val NANOS_PER_SECOND = 1_000_000_000f

        /** 250 Hz and 45 Hz: outside the range of any panel this app will meet. */
        private const val MIN_VSYNC_NANOS = 4_000_000L

        private const val MAX_VSYNC_NANOS = 22_000_000L

        private const val VSYNC_SMOOTHING = 8L

        private const val VSYNC_SETTLE_FRAMES = 8

        private const val DIVISOR_EPSILON = 0.05f

        /**
         * The harshest sane case, a 24 fps cap on a 165 Hz panel, divides by 6. A divisor past
         * this is a broken period estimate rather than a policy, and clamping keeps that bug
         * looking like a fast visualizer instead of a frozen one.
         */
        private const val MAX_DIVISOR = 8
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
