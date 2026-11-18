package dev.geode.engine.bridge

// The JNI declarations of geode_loop_job_jni.cpp (CLAUDE.md).
object LoopJobNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // A loop render (core/output/LoopRender, core/api/geode_loop_job_api.h): the encoder it settles on, each
    // drift stop's frames, seam and presentation times and the scratch file each stop goes into are native.
    // The shell fills the job in, runs it on a worker thread and polls it from another; native never calls back.

    /** A job to fill in, 0 when out of memory. Freed with [loopJobDestroy] once no call is in flight. */
    external fun loopJobCreate(): Long

    external fun loopJobDestroy(job: Long)

    /**
     * The frame, the bit rate asked for, the loop and its crossfade, how long the extended file will run
     * ([longFormMs] 0: unknown) and the directory the scratch files go in.
     */
    external fun loopJobSetOutput(
        job: Long,
        cacheDir: String,
        width: Int,
        height: Int,
        bitRate: Int,
        loopMs: Long,
        crossfadeMs: Long,
        fps: Int,
        longFormMs: Long,
    )

    /** The palette drift across the reel: [stops] loops, the last one [hueTurns] and [warmth] from the first. */
    external fun loopJobSetDrift(
        job: Long,
        hueTurns: Float,
        warmth: Float,
        stops: Int,
    )

    /** A codec to try, in the order added: its MIME type and the device's top frame rate for it (<= 0: unsaid). */
    external fun loopJobAddCodec(
        job: Long,
        mime: String,
        topFps: Int,
    )

    /** The track every stop hears: [fd] is native's from here on; [length] < 0 is the whole descriptor. */
    external fun loopJobSetTrack(
        job: Long,
        fd: Int,
        offset: Long,
        length: Long,
    )

    /** The renderer's inputs. [assets] is the AssetManager (typed as Any because this module has no Android SDK). */
    external fun loopJobSetScene(
        job: Long,
        assets: Any,
        milkTextureDir: String,
        sceneId: String,
        milkPath: String?,
        entitled: Boolean,
    )

    external fun loopJobSetParams(
        job: Long,
        values: FloatArray,
    )

    external fun loopJobSetLfos(
        job: Long,
        lfos: String,
    )

    external fun loopJobSetAdsrs(
        job: Long,
        adsrs: String,
    )

    /** [saved] null: the default tuning. */
    external fun loopJobSetSoundTuning(
        job: Long,
        saved: FloatArray?,
    )

    /** [reactions] null: Auto, the style's own play. */
    external fun loopJobSetReactions(
        job: Long,
        reactions: String?,
    )

    external fun loopJobSetMotion(
        job: Long,
        reducedMotion: Boolean,
        transitionId: String,
        transitionMs: Long,
    )

    external fun loopJobSetUnderlay(
        job: Long,
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    )

    /** The wordmark's tile, a direct buffer of [tileWidth] * [tileHeight] * 4 bytes; false when it is not one. */
    external fun loopJobSetMark(
        job: Long,
        rgba: java.nio.ByteBuffer,
        tileWidth: Int,
        tileHeight: Int,
        placement: Int,
        corner: Int,
    ): Boolean

    /** Renders every stop on the calling thread; returns once the job is rendered, cancelled or failed. */
    external fun loopJobRun(job: Long)

    /** From any thread. */
    external fun loopJobCancel(job: Long)

    /** 0..1 across the whole reel; from any thread. */
    external fun loopJobProgress(job: Long): Float

    /** One of the `LOOP_JOB_*` states; from any thread. */
    external fun loopJobState(job: Long): Int

    /** Why a failed job failed, one of `LOOP_FAILURE_*`; [loopJobDetail] is the text it is shown with. */
    external fun loopJobFailure(job: Long): Int

    external fun loopJobDetail(job: Long): String?

    /** The frame rate the encoder took. */
    external fun loopJobFps(job: Long): Int

    /** The crossfade the stops kept. */
    external fun loopJobCrossfadeMs(job: Long): Long

    /** One file per stop, in order, once the job is rendered. */
    external fun loopJobFileCount(job: Long): Int

    external fun loopJobFile(
        job: Long,
        index: Int,
    ): String?

    const val LOOP_JOB_RENDERED = 2
    const val LOOP_JOB_CANCELLED = 3
    const val LOOP_FAILURE_ENCODER_REJECTED = 1
    const val LOOP_FAILURE_SHADER_COMPILE = 2
    const val LOOP_FAILURE_LOW_GPU_MEMORY = 3
    const val LOOP_FAILURE_NO_OUTPUT = 4
    const val LOOP_FAILURE_STALLED = 5
    const val LOOP_FAILURE_ENCODER_FAILED = 6
    const val LOOP_FAILURE_CACHE_WRITE = 7
}
