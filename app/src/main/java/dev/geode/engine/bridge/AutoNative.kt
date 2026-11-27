package dev.geode.engine.bridge

// The JNI declarations of geode_auto_jni.cpp.
object AutoNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** Auto's timer range in seconds (GEODE_AUTO_INTERVAL_MIN_S, _MAX_S). */
    const val AUTO_INTERVAL_MIN_S = 5
    const val AUTO_INTERVAL_MAX_S = 300

    /** A timer interval held to Auto's range (core/viz/AutoPlan). */
    external fun autoIntervalSeconds(requested: Int): Int

    /** Customize opened or closed: Auto holds while it is open, and both waits start over when it closes. */
    external fun autoEditingSet(
        on: Boolean,
        nowMs: Long,
    )

    /** A style or a look picked by hand: both of Auto's waits start over. */
    external fun autoHandPick(nowMs: Long)

    /** The visual playlist's entry (of [count]) to put up now, or -1 (core/viz/AutoPlan). */
    external fun autoPlaylistTick(
        count: Int,
        enabled: Boolean,
        playing: Boolean,
        intervalSec: Int,
        nowMs: Long,
    ): Int

    /** A playlist step ([step] < 0 back): the entry to put up, or -1 with fewer than two. */
    external fun autoPlaylistStep(
        count: Int,
        step: Int,
        nowMs: Long,
    ): Int

    /** Whether Auto's timed draw is due now. */
    external fun autoRandomTick(
        enabled: Boolean,
        playing: Boolean,
        intervalSec: Int,
        nowMs: Long,
    ): Boolean

    /** What a director moment brings on the look on screen: an [AUTO_MOMENT_NONE]..[AUTO_MOMENT_RANDOM]. */
    external fun autoMoment(
        style: String,
        params: FloatArray,
        paletteId: String,
        playing: Boolean,
        playlistEnabled: Boolean,
    ): Int

    /** Auto's own change is on screen now. */
    external fun autoLeftAs(
        style: String,
        params: FloatArray,
        paletteId: String,
    )

    /**
     * Auto's draw (core/viz/AutoPlan over core/viz/AutoDraw and DirectorStyle) among [sources] ([AUTO_DRAW_STYLES]
     * etc.): the entry's style id, its kind ([AUTO_KIND_STYLE] etc.) in `out[0]` and its index in that list in
     * `out[1]`; null for none. [lockedStyles] (one a line) are what the entitlement refuses; [presetOnScreen] the
     * saved look on screen, -1 for none.
     */
    external fun autoDraw(
        lockedStyles: String,
        current: String,
        presetStyles: String,
        presetOnScreen: Int,
        sources: Int,
        nowMs: Long,
        out: IntArray,
    ): String?

    const val AUTO_MOMENT_NONE: Int = 0
    const val AUTO_MOMENT_PLAYLIST: Int = 1
    const val AUTO_MOMENT_RANDOM: Int = 2
    const val AUTO_DRAW_STYLES: Int = 1
    const val AUTO_DRAW_PRESETS: Int = 2
    const val AUTO_DRAW_KEEP_LOOK: Int = 8
    const val AUTO_KIND_STYLE: Int = 0
    const val AUTO_KIND_PRESET: Int = 1
}
