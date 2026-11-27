package dev.geode.engine.bridge

// The JNI declarations of geode_render_plan_jni.cpp.
object RenderPlanNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // A render's plan (core/api/geode_render_plan_api.h).

    /**
     * The part between slider fractions [start] and [end] of [trackMs] into [out]: start ms, duration ms, and end ms
     * when it holds three. False: the whole track.
     */
    external fun exportRangeOf(
        start: Float,
        end: Float,
        trackMs: Long,
        out: LongArray,
    ): Boolean

    /** A range ([durationMs] 0: the whole track) under the free tier's [capMs] (<= 0: none) into [out]; out[1] 0 is still the whole track. */
    external fun exportRangeCapped(
        startMs: Long,
        durationMs: Long,
        capMs: Long,
        out: LongArray,
    )

    /** The shortest part a render is offered. */
    external fun exportRangeMinMs(): Long

    /** The indices of [uris] a batch renders, in order, each URI once. */
    external fun renderBatchOrder(uris: Array<String>): IntArray

    /** A batch's whole progress, 0..1: [done] of [total] tracks plus the share [within] of the one rendering. */
    external fun renderBatchProgress(
        done: Int,
        total: Int,
        within: Float,
    ): Float
}
