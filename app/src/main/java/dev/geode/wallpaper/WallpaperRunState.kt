package dev.geode.wallpaper

import dev.geode.engine.bridge.GeodeNative

/** What the wallpaper's (or the dream's) GL view does right now. The order is the native code. */
enum class WallpaperRunMode {
    /** Paused: nothing on screen to draw to, or nobody can see it. */
    STOPPED,

    /** One frame drawn, then no pacing: a picture that costs nothing while it sits there. */
    STILL,

    /** Paced frames, the live visual. */
    ANIMATED,
}

/**
 * What the shell observed, handed whole to the native run rule (`core/viz/SurfaceRun`), which owns
 * every decision: the priority order, the preview's exemptions, when to listen, and when a trial's
 * end must restate it (#376). Nothing here decides; it only packs the observations into the bits
 * `geode_api.h` names.
 */
data class WallpaperConditions(
    val surfaceAvailable: Boolean,
    val visible: Boolean,
    /** The system wallpaper picker's preview. */
    val preview: Boolean,
    /** The shade or recents is over the wallpaper (`Engine.onZoomChanged` near 1). */
    val covered: Boolean,
    val batterySaver: Boolean,
    val silent: Boolean,
    /**
     * Reduce motion is on: the system's Remove animations ([SystemAnimations]).
     * Either holds a still frame on the wallpaper and the dream (owner, ADR 0008; #389).
     */
    val reducedMotion: Boolean,
    val settings: WallpaperSettings,
    /** Wall-clock ms the entitlement lets the visual move until ([dev.geode.billing.ProSnapshot.animateUntilMs]). */
    val animateUntilMs: Long,
) {
    private val flags: Int
        get() {
            var f = 0
            if (surfaceAvailable) f = f or GeodeNative.SURFACE_HAS_SURFACE
            if (visible) f = f or GeodeNative.SURFACE_VISIBLE
            if (preview) f = f or GeodeNative.SURFACE_PREVIEW
            if (covered) f = f or GeodeNative.SURFACE_COVERED
            if (batterySaver) f = f or GeodeNative.SURFACE_BATTERY_SAVER
            if (settings.pauseInBatterySaver) f = f or GeodeNative.SURFACE_PAUSE_IN_BATTERY_SAVER
            if (silent) f = f or GeodeNative.SURFACE_SILENT
            if (settings.restWhenSilent) f = f or GeodeNative.SURFACE_REST_WHEN_SILENT
            if (reducedMotion) f = f or GeodeNative.SURFACE_REDUCED_MOTION
            return f
        }

    fun runMode(nowMs: Long): WallpaperRunMode =
        WallpaperRunMode.entries[GeodeNative.surfaceRunMode(flags, animateUntilMs, nowMs)]

    fun listening(nowMs: Long): Boolean = GeodeNative.surfaceListening(flags, animateUntilMs, nowMs)

    fun entitled(nowMs: Long): Boolean = GeodeNative.surfaceEntitled(animateUntilMs, nowMs)

    /** When to restate because a trial ends, or null when nothing will change on its own. */
    fun recheckInMs(nowMs: Long): Long? = GeodeNative.surfaceRecheckInMs(animateUntilMs, nowMs).takeIf { it >= 0 }
}

/**
 * How long to keep pacing after moving from [previous] to [this] before the pacer stops: the native
 * still-frame settle (a scene is built across frames, so one frame could hold a half-built picture),
 * or 0 when one frame, or the plain on and off, is enough. The shell only schedules the stop.
 */
fun WallpaperRunMode.settleMsFrom(previous: WallpaperRunMode): Long = GeodeNative.surfaceSettleMs(previous.ordinal, ordinal)
