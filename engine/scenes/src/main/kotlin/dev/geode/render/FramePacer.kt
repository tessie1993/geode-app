package dev.geode.render

import android.os.Build
import android.view.Choreographer
import android.view.Surface

/**
 * Receives one call per paced frame, on the thread that owns the Choreographer.
 *
 * Declared as a `fun interface` rather than `(Float) -> Unit` because a Kotlin function type
 * boxes its `Float` argument: at 120 Hz that is 120 short-lived `java.lang.Float` objects a
 * second on the main thread, which is exactly the kind of steady garbage that turns into a
 * dropped frame when a GC lands mid-beat.
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
 * Threading: [start], [stop] and [applyTo] must be called from a thread with a Looper (the main
 * thread for both `GLSurfaceView` and `WallpaperService.Engine`), because that is the thread the
 * Choreographer callbacks arrive on. [dtSeconds] is safe to read from any thread.
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

    private var choreographer: Choreographer? = null
    private var running = false

    private var lastDrawnNanos = 0L
    private var lastVsyncNanos = 0L
    private var vsyncPeriodNanos = 0L
    private var vsyncSamples = 0
    private var vsyncsSinceDrawn = 0
    private var divisor = 1

    /** Idempotent: a second call while running is a no-op, so lifecycle callbacks can overlap. */
    fun start() {
        if (running) return
        val instance = Choreographer.getInstance()
        choreographer = instance
        running = true
        // A resume must not inherit the gap it was paused across.
        lastDrawnNanos = 0L
        lastVsyncNanos = 0L
        vsyncsSinceDrawn = 0
        // A pacer that starts twice without stopping (start() is idempotent, so this only runs
        // once per start) must not end up twice in the stack ahead of itself.
        synchronized(runningStack) {
            runningStack.remove(this)
            runningStack.add(this)
        }
        publishActiveCap()
        instance.postFrameCallback(callback)
    }

    /**
     * Removes the pending vsync callback outright rather than letting it fire and skip work —
     * §4.4 of the quality bar asks for the callback itself to stop, and a wallpaper toggles
     * visibility every time the screen sleeps.
     */
    fun stop() {
        if (!running) return
        running = false
        choreographer?.removeFrameCallback(callback)
        choreographer = null
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
        choreographer?.postFrameCallback(callback)
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
        // Outside the range of any real panel this is a main-thread hitch, not a refresh period;
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
