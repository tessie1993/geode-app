package dev.geode.render

import android.content.Context
import android.os.Build
import android.os.PowerManager
import dev.geode.render.fluid.PerformanceMonitor
import dev.geode.util.bestEffort
import java.util.concurrent.atomic.AtomicInteger

/**
 * How much of the frame budget the engine may spend right now.
 *
 * The tiers shed load in the order `docs/quality/bar-visualizer.md` §4.4 prescribes — internal
 * render scale first, then the optional per-frame passes, then the frame rate — because that is
 * the order that costs the least visible quality per degree of heat avoided. Dropping a
 * glow-heavy scene to 70% of its pixel count and upscaling is close to invisible; dropping to
 * 30 fps is the most visible thing on the list, so it is the last lever pulled and only ever the
 * last one.
 *
 * Declared ordinal order IS severity order: [ThermalGovernor] compares tiers with `maxOf` and
 * relaxes by stepping one ordinal at a time. Inserting a tier out of order silently changes the
 * hysteresis.
 */
enum class ThermalTier(
    val renderScale: Float,
    val optionalPasses: Boolean,
    val fpsCap: Float?,
) {
    /** Nothing is withheld. The only tier an export may ever see — see [ThermalGovernor]. */
    FULL(renderScale = 1f, optionalPasses = true, fpsCap = null),

    /**
     * Lever one, alone. 0.85 is the largest reduction that survives the upscale unnoticed on the
     * text-free, high-frequency-poor content this engine draws; much below it the thin strokes
     * of the oscilloscope and beam styles start to crawl.
     */
    EASED(renderScale = 0.85f, optionalPasses = true, fpsCap = null),

    /**
     * Lever two joins it: the optional simulations that feed the compositor — the flow field and
     * the ripple overlay — stop running. Each is a whole extra simulation per frame, so this is
     * the largest single saving available, and its absence reads as "the warp settled down"
     * rather than as a broken visualizer.
     */
    REDUCED(renderScale = 0.7f, optionalPasses = false, fpsCap = null),

    /**
     * Lever three, last: 60→30 fps. The render scale stops at 0.6 rather than going lower
     * because bandwidth, not ALU, is what a throttled mobile GPU runs out of first, and halving
     * the presented frames has already halved the bandwidth.
     */
    MINIMAL(renderScale = 0.6f, optionalPasses = false, fpsCap = 30f),
}

/**
 * Decides how hard the engine may push the GPU: from heat where the device will say, and from the
 * frame-time trend where it will not.
 *
 * The problem this exists to solve is that the frame-time machinery already in the engine
 * ([PerformanceMonitor], and the fluid auto-quality ladder on top of it) is *reactive*. By the
 * time a sustained deficit is measurable the phone is already throttled and the user has already
 * watched the visuals come apart. `docs/quality/bar-visualizer.md` §4.4 asks for load to be shed
 * BEFORE the OS throttles, which needs a signal that leads frame time instead of following it.
 * `PowerManager.getThermalHeadroom` is exactly that signal — a forecast — and the thermal-status
 * listener is the coarse confirmation behind it.
 *
 * Both are API 29/30 and this app's minSdk is 26; worse, the devices that most need throttling
 * protection are precisely the old ones without the API. So there are two producers and one
 * product: on API 29+ the platform's readings decide, and below that the measured frame-time
 * trend decides. Callers read [tier] and never learn which one answered.
 *
 * Singleton because heat is a property of the DEVICE, not of a surface: a wallpaper with a
 * preview engine and a home-screen engine lives in one process on one SoC and must not keep two
 * opinions of it, and the platform listener is then registered once for the process lifetime
 * rather than churned on every surface recreation, which would leak a listener per rotation.
 *
 * Threading: [attach], [onFrame] and [onSurfaceRecreated] come from a render thread — possibly
 * more than one, since a wallpaper's preview and home-screen engines each have their own, and
 * their frame times are deliberately pooled because they are loading one GPU. [tier] and
 * [pacedFps] are safe from any thread. No polling thread and no wakelock: the status arrives by
 * callback, and the headroom forecast is sampled off frames the renderer was drawing anyway.
 */
object ThermalGovernor {
    /**
     * The current verdict. Cheap enough to read per frame.
     *
     * Pinned to [ThermalTier.FULL] for the duration of an offscreen render — see
     * [beginOffscreenRender].
     */
    val tier: ThermalTier
        get() = if (offscreenDepth.get() > 0) ThermalTier.FULL else settled

    /**
     * The rate the renderer is currently ASKING the display for, or 0 when it free-runs.
     *
     * Publish it from whoever owns the frame pacer, and re-publish it whenever the policy
     * changes — including when the pacer applies this governor's own [ThermalTier.fpsCap].
     * Without it a deliberate cap is indistinguishable from a device that cannot keep up: ask a
     * wallpaper for 30 fps and every frame-time monitor in the engine reads its own cap as a
     * permanent deficit and ratchets quality down until it bottoms out. That is the bug
     * `VisualizerWallpaperService.WALLPAPER_FPS` documents; this field is what closes it, and it
     * is also why the fps cap is never driven by a frame-time measurement (see [onFrame]).
     */
    @Volatile
    var pacedFps: Float = 0f

    /** Written on the render thread, in [settle]; read from anywhere. */
    @Volatile
    private var settled: ThermalTier = ThermalTier.FULL

    /** Written on the main executor by the platform listener, read on the render thread. */
    @Volatile
    private var osStatus: Int = STATUS_NONE

    /**
     * Atomic rather than a plain flag because the two exports that could overlap do so on two
     * threads, and a lost increment there means an export silently rendered at a degraded tier.
     */
    private val offscreenDepth = AtomicInteger(0)

    @Volatile
    private var power: PowerManager? = null

    @Volatile
    private var attached = false

    /**
     * The measured-trend producer, used only when the platform has nothing to say. Deliberately
     * the same controller class the fluid ladder uses, rather than a second opinion with its own
     * thresholds, so that "sustained deficit" means one thing across the whole engine.
     */
    private val monitor = PerformanceMonitor()

    /**
     * One-way floor from the measured path. It ratchets and never relaxes, matching
     * [PerformanceMonitor]'s existing philosophy: a frame-time trend cannot tell "the phone
     * cooled down" from "the scene got cheap for a second", so letting it hand quality back would
     * produce exactly the oscillation the ratchet exists to prevent. The platform path, which
     * measures actual heat, is the only one allowed to step back up.
     */
    private var measuredFloor = ThermalTier.FULL

    /** See [trackMeasuredTrend]: the frame-time path may never reach the fps-cap tier. */
    private val measuredCeiling = ThermalTier.REDUCED

    private var sampleClock = 0f
    private var hotDwellSeconds = 0f
    private var coolDwellSeconds = 0f

    /**
     * Registers the platform listener, once per process. Safe to call from every
     * `onSurfaceCreated`; every call after the first is a no-op.
     */
    @Synchronized
    fun attach(context: Context) {
        if (attached) return
        attached = true
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        // Application context, not the one passed in: this reference outlives every Activity,
        // wallpaper Engine and surface in the process by design.
        val app = context.applicationContext
        val service = app.getSystemService(PowerManager::class.java) ?: return
        power = service
        osStatus = service.currentThermalStatus
        // Delivered on the main executor rather than the render thread: a status change that
        // fires while the GL thread is parked inside eglSwapBuffers would arrive a frame late at
        // the one moment it matters. The callback only stores an Int, so the main thread pays
        // nothing for carrying it.
        bestEffort(TAG, "addThermalStatusListener") {
            service.addThermalStatusListener(app.mainExecutor) { status -> osStatus = status }
        }
    }

    /**
     * One call per drawn frame, from the render thread.
     *
     * [dtSeconds] is the measured frame interval — the same value the scenes step on, so a
     * stalled or clamped frame reaches the trend exactly as it reached the simulation.
     */
    fun onFrame(dtSeconds: Float) {
        if (offscreenDepth.get() > 0) return
        if (!platformKnows()) trackMeasuredTrend(dtSeconds)
        sampleClock += dtSeconds
        if (sampleClock < SAMPLE_PERIOD_SECONDS) return
        sampleClock = 0f
        settle(observe())
    }

    /**
     * The frame-time window describes a surface that no longer exists — a different resolution,
     * possibly a different scene — so it is discarded. The tier is NOT: the device is exactly as
     * hot as it was a millisecond ago, and a rotation is not a reason to promise full quality
     * again.
     */
    fun onSurfaceRecreated() {
        monitor.reset()
        sampleClock = 0f
        hotDwellSeconds = 0f
        coolDwellSeconds = 0f
    }

    /**
     * Suppresses the governor for the duration of an offscreen (export) render. Must be paired
     * with [endOffscreenRender].
     *
     * An export renders every frame as fast as the encoder will take it, which is nothing like
     * real time: a 30 fps sequence produced at 12 fps of wall clock reads as a catastrophic and
     * permanent deficit to any frame-time trend, and heat an export generates is heat the user
     * asked for rather than a reason to degrade the file they are waiting on. The offscreen path
     * already forces `fluidAutoQuality = false` for exactly this reason; this is the same
     * suppression for the same failure, made explicit rather than left resting on the fact that
     * the offscreen renderer happens not to read [tier] today.
     *
     * Counted rather than a flag so overlapping runs cannot un-suppress each other.
     */
    fun beginOffscreenRender() {
        offscreenDepth.incrementAndGet()
    }

    /** See [beginOffscreenRender]. Floors at zero, because `release()` may be called twice. */
    fun endOffscreenRender() {
        offscreenDepth.updateAndGet { depth -> if (depth > 0) depth - 1 else 0 }
    }

    private fun platformKnows(): Boolean = power != null

    private fun trackMeasuredTrend(dtSeconds: Float) {
        // Forwarded rather than read by the monitor itself: the pacer publishes to one place, and
        // "keeping up" then means the same thing here as it does to the fluid ladder.
        monitor.pacedFps = pacedFps
        val relief = monitor.onFrame(dtSeconds)
        if (relief <= 0) return
        // Capped at REDUCED on purpose: MINIMAL is the fps cap, and driving an fps cap from an
        // fps measurement is a feedback loop — cap to 30, measure 30, call it a deficit, cap
        // again. Only a real heat reading is allowed to pull that lever.
        val ceiling = measuredCeiling.ordinal
        measuredFloor = ThermalTier.entries[(measuredFloor.ordinal + relief).coerceAtMost(ceiling)]
        // The relief has been banked, so the window starts clean rather than re-triggering on the
        // same slow seconds. This is how the fluid ladder consumes the monitor too.
        monitor.reset()
    }

    /** The worst of everything currently known. Sampled at [SAMPLE_PERIOD_SECONDS], never per frame. */
    private fun observe(): ThermalTier {
        val platform = maxOf(statusTier(osStatus), headroomTier())
        return if (platformKnows()) platform else maxOf(platform, measuredFloor)
    }

    /**
     * Hysteresis, and the whole reason this is not a plain `settled = observe()`.
     *
     * The dwells are deliberately lopsided. Heat that has held for [ESCALATE_DWELL_SECONDS] is
     * heat that will keep being there, and shedding early is the entire point — so escalation
     * jumps straight to whatever was observed instead of climbing a tier at a time. Recovery is
     * the opposite: one tier per [RELAX_DWELL_SECONDS], because a phone that has just cooled past
     * a threshold is a phone about to cross back over it, and a scaler that oscillates between
     * render scales is more objectionable than one that simply stays low.
     */
    private fun settle(observed: ThermalTier) {
        val current = settled
        when {
            observed > current -> {
                coolDwellSeconds = 0f
                hotDwellSeconds += SAMPLE_PERIOD_SECONDS
                if (hotDwellSeconds >= ESCALATE_DWELL_SECONDS) {
                    settled = observed
                    hotDwellSeconds = 0f
                }
            }

            observed < current -> {
                hotDwellSeconds = 0f
                coolDwellSeconds += SAMPLE_PERIOD_SECONDS
                if (coolDwellSeconds >= RELAX_DWELL_SECONDS) {
                    settled = ThermalTier.entries[current.ordinal - 1]
                    coolDwellSeconds = 0f
                }
            }

            else -> {
                hotDwellSeconds = 0f
                coolDwellSeconds = 0f
            }
        }
    }

    /**
     * The confirmation, not the warning. `THERMAL_STATUS_LIGHT` already means the platform has
     * begun throttling, so it maps to a tier that has shed something rather than to
     * [ThermalTier.FULL]; by `MODERATE` the user can see it. Everything at or past `SEVERE` —
     * `CRITICAL`, `EMERGENCY` and `SHUTDOWN` included — is the same answer: give back everything
     * there is to give.
     */
    private fun statusTier(status: Int): ThermalTier =
        when {
            status <= STATUS_NONE -> ThermalTier.FULL
            status == STATUS_LIGHT -> ThermalTier.EASED
            status == STATUS_MODERATE -> ThermalTier.REDUCED
            else -> ThermalTier.MINIMAL
        }

    /**
     * The warning. Headroom is normalised so 1.0 is the throttling threshold, and asking for a
     * forecast [FORECAST_SECONDS] out is what lets the engine go quiet before the phone goes
     * slow — the difference between meeting §4.4 and merely reacting to it.
     *
     * NaN reads as [ThermalTier.FULL]: it is what the platform returns for "no sensor", "not
     * supported on this device" and "you asked again too soon", and an unknown forecast must mean
     * "no reason to shed", never pressure. The call is wrapped because a handful of OEM power
     * HALs throw rather than report unsupported, and a thermal governor must not be the thing
     * that crashes the render thread.
     */
    private fun headroomTier(): ThermalTier {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return ThermalTier.FULL
        val service = power ?: return ThermalTier.FULL
        val headroom = runCatching { service.getThermalHeadroom(FORECAST_SECONDS) }.getOrDefault(Float.NaN)
        return when {
            headroom.isNaN() -> ThermalTier.FULL
            headroom >= HEADROOM_MINIMAL -> ThermalTier.MINIMAL
            headroom >= HEADROOM_REDUCED -> ThermalTier.REDUCED
            headroom >= HEADROOM_EASED -> ThermalTier.EASED
            else -> ThermalTier.FULL
        }
    }
}

private const val TAG = "ThermalGovernor"

/**
 * `PowerManager.THERMAL_STATUS_*` (API 29), mirrored here so the mapping can live outside a
 * version guard at minSdk 26 without lint's NewApi objecting to the platform fields. The values
 * are part of the platform's published contract and cannot change.
 */
private const val STATUS_NONE = 0

private const val STATUS_LIGHT = 1

private const val STATUS_MODERATE = 2

/**
 * `getThermalHeadroom` is rate-limited to one call a second and returns NaN when asked faster, so
 * that limit sets the sampling period for the whole decision — with a deliberate margin over it.
 * At exactly 1f, jitter in the accumulated frame times would clip the limit every so often and
 * the forecast would read NaN; since NaN correctly means "no pressure", an intermittent one lands
 * as a cool sample that resets the escalation dwell, and a governor that can never finish
 * escalating is worse than one that samples slightly slower. Nothing about heat moves at this
 * timescale anyway.
 */
private const val SAMPLE_PERIOD_SECONDS = 1.25f

/**
 * Far enough ahead to shed load and let the SoC coast back under the threshold; short enough that
 * the forecast is still about this track rather than about the rest of the evening. The platform
 * accepts up to 60.
 */
private const val FORECAST_SECONDS = 30

/**
 * Three consecutive samples agreeing, near enough. One sample is a spike; three is a trend, and
 * ~3.75 s is still well ahead of the throttle the forecast is warning about.
 */
private const val ESCALATE_DWELL_SECONDS = 3f

/**
 * A full minute of measured cool before a single tier comes back. Long because the cost of being
 * wrong is asymmetric: staying one tier low is invisible, while bouncing the render scale every
 * few seconds is a visible pulse in sharpness that no user will attribute to heat.
 */
private const val RELAX_DWELL_SECONDS = 60f

private const val HEADROOM_EASED = 0.75f

private const val HEADROOM_REDUCED = 0.85f

private const val HEADROOM_MINIMAL = 0.95f
