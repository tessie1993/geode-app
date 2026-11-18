package dev.geode.engine.bridge

// The JNI declarations of geode_video_export_jni.cpp (CLAUDE.md).
object VideoExportNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // A video export (core/output/VideoExport, core/api/geode_video_export_api.h): the bit rate, the encoder it
    // settles on, every frame's draw and timestamp, the drain into the MP4 and the audio's interleave are native.
    // The shell fills the job in, runs it on a worker thread and polls it from another; native never calls back.

    /** A job to fill in, 0 when out of memory. Freed with [videoExportDestroy] once no call is in flight. */
    external fun videoExportCreate(): Long

    external fun videoExportDestroy(job: Long)

    /** The file to write ([outputFd] stays the caller's, open until the run returns), the frame and the rates asked for. */
    external fun videoExportSetOutput(
        job: Long,
        outputFd: Int,
        cacheDir: String,
        width: Int,
        height: Int,
        bitRate: Int,
        fps: Int,
    )

    /** A codec to try, in the order added: its MIME type and the device's top frame rate for it (<= 0: unsaid). */
    external fun videoExportAddCodec(
        job: Long,
        mime: String,
        topFps: Int,
    )

    /** The track the renderer hears: [fd] is native's from here on; [length] < 0 is the whole descriptor. */
    external fun videoExportSetTrack(
        job: Long,
        fd: Int,
        offset: Long,
        length: Long,
        rangeStartMs: Long,
    )

    /** The renderer's inputs. [assets] is the AssetManager (typed as Any because this module has no Android SDK). */
    external fun videoExportSetScene(
        job: Long,
        assets: Any,
        sceneId: String,
        entitled: Boolean,
    )

    /** The pinned look, as [VizNative.vizSetLook] takes it, on the scene [videoExportSetScene] named. */
    external fun videoExportSetLook(
        job: Long,
        params: FloatArray,
        lfos: String,
        adsrs: String,
        tuning: FloatArray?,
        reactions: String?,
        reducedMotion: Boolean,
        transitionId: String,
        transitionMs: Long,
    )

    external fun videoExportSetUnderlay(
        job: Long,
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    )

    /** The wordmark's tile, a direct buffer of [tileWidth] * [tileHeight] * 4 bytes; false when it is not one. */
    external fun videoExportSetMark(
        job: Long,
        rgba: java.nio.ByteBuffer,
        tileWidth: Int,
        tileHeight: Int,
        placement: Int,
        corner: Int,
    ): Boolean

    /** The encoded audio: its scratch file and its samples as `[offset, size, ptsUs, flags]` quadruples; false when the file cannot be opened. */
    external fun videoExportSetAudio(
        job: Long,
        path: String,
        samples: LongArray,
    ): Boolean

    /** One key of the audio's format, typed as MediaFormat.getValueTypeForKey types it. */
    external fun videoExportAddAudioKey(
        job: Long,
        key: String,
        type: Int,
        whole: Long,
        real: Float,
        text: String?,
        bytes: ByteArray?,
    )

    /** Renders the file on the calling thread; returns once it is rendered, cancelled or failed. */
    external fun videoExportRun(job: Long)

    /** From any thread. */
    external fun videoExportCancel(job: Long)

    /** 0..1 across the render; from any thread. */
    external fun videoExportProgress(job: Long): Float

    /** The bar while the shell transcodes the audio, [done] 0..1 of that step. */
    external fun videoExportLeadIn(done: Float): Float

    /** One of the `VIDEO_EXPORT_*` states; from any thread. */
    external fun videoExportState(job: Long): Int

    /** Why a failed job failed, one of `VIDEO_FAILURE_*`; [videoExportDetail] is the text it is shown with. */
    external fun videoExportFailure(job: Long): Int

    external fun videoExportDetail(job: Long): String?

    const val VIDEO_EXPORT_RENDERED = 2
    const val VIDEO_EXPORT_CANCELLED = 3
    const val VIDEO_FAILURE_ENCODER_REJECTED = 1
    const val VIDEO_FAILURE_NO_OUTPUT = 2
    const val VIDEO_FAILURE_STALLED = 3
    const val VIDEO_FAILURE_EMPTY_BUFFER = 5
}
