package dev.geode.engine.bridge

// The JNI declarations of geode_loop_mux_jni.cpp (CLAUDE.md).
object LoopMuxNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // A loop extend's copy (core/output/LoopMux, core/api/geode_loop_mux_api.h): the loops' samples read back with
    // AMediaExtractor and written into one MP4 once per repeat, the soundtrack interleaved by timestamp. The shell
    // fills it in, runs it on a worker thread and polls it from another; native never calls back.

    /** A copy to fill in, 0 when out of memory. Freed with [loopMuxDestroy] once no call is in flight. */
    external fun loopMuxCreate(): Long

    external fun loopMuxDestroy(mux: Long)

    /** A rendered loop's file, in the order the repeat plan indexes the loops. */
    external fun loopMuxAddLoop(
        mux: Long,
        path: String,
    )

    /**
     * The next soundtrack clip: the finished [ExportAudioNative] transcode [aacJob] and its scratch file at [path].
     * One of `CLIP_*`; the job may be destroyed once this returns.
     */
    external fun loopMuxAddClip(
        mux: Long,
        aacJob: Long,
        path: String,
    ): Int

    /** How long the soundtrack runs (the clips so far, back to back), in µs. */
    external fun loopMuxTotalUs(mux: Long): Long

    /** Copies the whole file into [outputFd] (stays the caller's); returns once it is done, cancelled or failed. */
    external fun loopMuxRun(
        mux: Long,
        outputFd: Int,
        loopDurationUs: Long,
    )

    /** From any thread. */
    external fun loopMuxCancel(mux: Long)

    /** 0..1 across the whole build, the soundtrack's share first; from any thread. */
    external fun loopMuxProgress(mux: Long): Float

    /** One of the `MUX_*` states; from any thread. */
    external fun loopMuxState(mux: Long): Int

    /** Why a failed copy failed, one of `FAILURE_*`; [loopMuxDetail] is the text it is shown with. */
    external fun loopMuxFailure(mux: Long): Int

    external fun loopMuxDetail(mux: Long): String?

    const val CLIP_UNREADABLE = 0
    const val CLIP_DIFFERS = -1
    const val MUX_DONE = 2
    const val MUX_CANCELLED = 3
    const val FAILURE_UNUSABLE = 1
    const val FAILURE_READ = 2
}
