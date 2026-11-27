package dev.geode.dream

import dev.geode.engine.bridge.SoundNative

/** What the dream's stage does right now. The order is the native code. */
enum class DreamRunMode {
    /** Paused: nothing on screen to draw to, or nobody can see it. */
    STOPPED,

    /** One frame drawn, then no pacing: a picture that costs nothing while it sits there. */
    STILL,

    /** Paced frames, the live visual. */
    ANIMATED,

    /** No visual: the dream in silence, covered in black, hearing so sound wakes it. */
    DARK,
}

/**
 * What the dream observed, handed whole to the native run rule (`core/viz/SurfaceRun`), which owns
 * every decision: the priority order, when to listen, and when a trial's end must restate it (#376).
 * Nothing here decides; it only packs the observations into the bits `geode_api.h` names.
 *
 * A dream has no options page (`res/xml/dream_info.xml`), so it always pauses in battery saver and
 * rests when silent, and a silent rest shows no visual rather than a still frame (owner: no sound,
 * no visuals).
 */
data class DreamConditions(
    val visible: Boolean,
    /** The proximity sensor reads near ([DreamCover]). */
    val covered: Boolean,
    val batterySaver: Boolean,
    val silent: Boolean,
    /**
     * Reduce motion is on: the system's Remove animations ([SystemAnimations]).
     * It holds a still frame on the dream (owner; #389).
     */
    val reducedMotion: Boolean,
    /** Wall-clock ms the entitlement lets the visual move until ([dev.geode.billing.ProSnapshot.animateUntilMs]). */
    val animateUntilMs: Long,
) {
    /** The observations as `SoundNative.SURFACE_*` bits, for the stage and the rule alike. */
    val flags: Int
        get() {
            var f =
                SoundNative.SURFACE_HAS_SURFACE or SoundNative.SURFACE_PAUSE_IN_BATTERY_SAVER or
                    SoundNative.SURFACE_REST_WHEN_SILENT or SoundNative.SURFACE_DARK_WHEN_SILENT
            if (visible) f = f or SoundNative.SURFACE_VISIBLE
            if (covered) f = f or SoundNative.SURFACE_COVERED
            if (batterySaver) f = f or SoundNative.SURFACE_BATTERY_SAVER
            if (silent) f = f or SoundNative.SURFACE_SILENT
            if (reducedMotion) f = f or SoundNative.SURFACE_REDUCED_MOTION
            return f
        }

    fun listening(nowMs: Long): Boolean = SoundNative.surfaceListening(flags, animateUntilMs, nowMs)

    fun entitled(nowMs: Long): Boolean = SoundNative.surfaceEntitled(animateUntilMs, nowMs)

    /** When to restate because a trial ends, or null when nothing will change on its own. */
    fun recheckInMs(nowMs: Long): Long? = SoundNative.surfaceRecheckInMs(animateUntilMs, nowMs).takeIf { it >= 0 }
}
