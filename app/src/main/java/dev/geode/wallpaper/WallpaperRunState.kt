package dev.geode.wallpaper

import dev.geode.engine.bridge.GeodeNative

/** What the wallpaper's (or the dream's) stage does right now. The order is the native code. */
enum class WallpaperRunMode {
    /** Paused: nothing on screen to draw to, or nobody can see it. */
    STOPPED,

    /** One frame drawn, then no pacing: a picture that costs nothing while it sits there. */
    STILL,

    /** Paced frames, the live visual. */
    ANIMATED,

    /** No visual: the dream in silence, covered in black, hearing so sound wakes it. */
    DARK,
}

/** Why the surface is not moving, the native rule's first reason. The order is the native code. */
enum class WallpaperStillReason { NONE, HIDDEN, ENTITLEMENT, REDUCED_MOTION, BATTERY_SAVER, SILENCE }

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
     * It holds a still frame on the wallpaper and the dream (owner, ADR 0008; #389).
     */
    val reducedMotion: Boolean,
    val settings: WallpaperSettings,
    /** Wall-clock ms the entitlement lets the visual move until ([dev.geode.billing.ProSnapshot.animateUntilMs]). */
    val animateUntilMs: Long,
    /** The dream: silence shows no visual rather than a still frame (owner: no sound, no visuals). */
    val darkWhenSilent: Boolean = false,
) {
    /** The observations as `GeodeNative.SURFACE_*` bits, for the stage and the rule alike. */
    val flags: Int
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
            if (darkWhenSilent) f = f or GeodeNative.SURFACE_DARK_WHEN_SILENT
            return f
        }

    fun runMode(nowMs: Long): WallpaperRunMode = WallpaperRunMode.entries[GeodeNative.surfaceRunMode(flags, animateUntilMs, nowMs)]

    fun stillReason(nowMs: Long): WallpaperStillReason =
        WallpaperStillReason.entries[GeodeNative.surfaceReason(flags, animateUntilMs, nowMs)]

    fun listening(nowMs: Long): Boolean = GeodeNative.surfaceListening(flags, animateUntilMs, nowMs)

    fun entitled(nowMs: Long): Boolean = GeodeNative.surfaceEntitled(animateUntilMs, nowMs)

    /** When to restate because a trial ends, or null when nothing will change on its own. */
    fun recheckInMs(nowMs: Long): Long? = GeodeNative.surfaceRecheckInMs(animateUntilMs, nowMs).takeIf { it >= 0 }
}
