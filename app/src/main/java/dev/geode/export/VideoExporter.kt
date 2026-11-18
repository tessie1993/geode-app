package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.content.res.AssetFileDescriptor
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.RingLog
import dev.geode.billing.ProSnapshot
import dev.geode.data.LookSnapshot
import dev.geode.data.pendingRows
import dev.geode.data.publishPending
import dev.geode.engine.bridge.ExportAudioNative
import dev.geode.engine.bridge.ExportNative
import dev.geode.engine.bridge.VideoExportNative
import dev.geode.model.LoudnessAdvice
import dev.geode.model.LoudnessTarget
import dev.geode.model.WatermarkChoice
import dev.geode.pro.ProFeature
import dev.geode.util.bestEffort
import dev.geode.viz.BackgroundExportSpec
import dev.geode.viz.BackgroundImage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class ExportCodec(
    val mimeType: String,
) {
    H264(MediaFormat.MIMETYPE_VIDEO_AVC),
    HEVC(MediaFormat.MIMETYPE_VIDEO_HEVC),
    ;

    /** This codec when the device has an encoder for it, otherwise H.264, which every device has. */
    fun available(): ExportCodec = if (this == H264 || hasEncoder(mimeType)) this else H264

    /** The codecs to try, in order: this one if the device has it, then H.264. */
    fun candidates(): List<ExportCodec> = listOf(available(), H264).distinct()

    /** The top frame rate the device lists for this codec at [aspect]'s size, 0 when it does not say. */
    fun topFps(aspect: ExportAspect): Int =
        runCatching { videoCaps(mimeType)?.getSupportedFrameRatesFor(aspect.width, aspect.height)?.upper?.toInt() }.getOrNull() ?: 0

    companion object {
        fun hasEncoder(mimeType: String): Boolean = videoCaps(mimeType) != null

        private fun videoCaps(mimeType: String): MediaCodecInfo.VideoCapabilities? =
            MediaCodecList(MediaCodecList.REGULAR_CODECS)
                .codecInfos
                .firstOrNull { info ->
                    info.isEncoder && info.supportedTypes.any { it.equals(mimeType, ignoreCase = true) }
                }?.getCapabilitiesForType(mimeType)
                ?.videoCapabilities
    }
}

/** The part of a track a render covers, as native picked it (`RenderPlanNative.exportRangeOf`, `exportRangeCapped`). */
data class ExportRange(
    val startMs: Long,
    val durationMs: Long,
)

enum class ExportQuality(
    val shortSide: Int,
    val bitRate: Int,
) {
    HD720(720, 6_000_000),
    FHD1080(1080, 12_000_000),
    UHD4K(2160, 40_000_000),
}

enum class ExportRatio(
    val label: String,
    val wRatio: Int,
    val hRatio: Int,
) {
    R16_9("16:9", 16, 9),
    R9_16("9:16", 9, 16),
    R1_1("1:1", 1, 1),
    R4_5("4:5", 4, 5),
    R4_3("4:3", 4, 3),
    R21_9("21:9", 21, 9),
}

class ExportAspect(
    val width: Int,
    val height: Int,
    val bitRate: Int,
) {
    companion object {
        /**
         * The encoded frame for a quality and a ratio: the long side follows the ratio, both shrink
         * together past 4096 (the longest side H.264 level 5.1 takes), each rounded up to even.
         * Native (`core/output/EncodePlan`, tested by `core/tests/output_test.cpp`).
         */
        fun of(
            quality: ExportQuality,
            ratio: ExportRatio,
        ): ExportAspect {
            val size = IntArray(2)
            ExportNative.exportFrameSize(quality.shortSide, ratio.wRatio, ratio.hRatio, size)
            return ExportAspect(size[0], size[1], quality.bitRate)
        }
    }
}

/**
 * The one-tap render's shell. The render itself is one native job (`core/output/VideoExport`): the bit rate,
 * the encoder ladder, every frame's draw, time and timestamp, the drain into the MP4, the audio's interleave,
 * the progress and the failure. This owns what needs Android: the MediaStore row (inserted `IS_PENDING`,
 * published or deleted) or the chosen document, the descriptors, the entitlement, the wordmark's text, and
 * the words a failure is shown with. It hands the job its values, polls it and passes a cancel on.
 */
class VideoExporter(
    private val context: Context,
) {
    companion object {
        private const val FPS: Int = 60

        // The double[] exportLoudnessMeasure fills.
        private const val LOUDNESS_DOUBLES = 9

        /** How often the job is asked for its progress and told of a cancel. */
        private const val POLL_MS = 50L
    }

    sealed interface Result {
        data class Saved(
            val uri: Uri,
            /**
             * How the finished file's loudness compares to [loudnessTarget], or null when it could
             * not be measured (e.g. no audio track). Geode cannot yet apply the resulting gain — see
             * [measureLoudness] — so this is read-only telemetry for now, not an applied correction.
             */
            val loudnessAdvice: LoudnessAdvice? = null,
        ) : Result

        data class Failed(
            val message: String,
            @StringRes val messageRes: Int? = null,
            val messageArgs: List<Any> = emptyList(),
        ) : Result {
            /** Re-resolves [messageRes] against a live [context], for a UI layer that wants localisation. */
            @Suppress("SpreadOperator")
            fun describe(context: Context): String = messageRes?.let { context.getString(it, *messageArgs.toTypedArray()) } ?: message
        }

        data object Cancelled : Result
    }

    /** Builds a [Result.Failed] whose [Result.Failed.message] is already resolved from [resId]. */
    private fun failed(
        @StringRes resId: Int,
        vararg args: Any,
    ): Result.Failed = Result.Failed(context.getString(resId, *args), resId, args.toList())

    /** [onProgress] takes the bar and whether it is steady (the frames; the audio's transcode before them is not). */
    suspend fun export(
        audioUri: Uri,
        sceneFactory: SceneFactory,
        aspect: ExportAspect,
        fileName: String,
        // The look the caller pinned when the render started; its underlay comes from [background].
        look: LookSnapshot,
        requestedFps: Int = FPS,
        range: ExportRange? = null,
        destination: Uri? = null,
        codec: ExportCodec = ExportCodec.H264,
        loudnessTarget: LoudnessTarget = LoudnessTarget.LeaveAsIs,
        background: BackgroundExportSpec? = null,
        onProgress: (Float, Boolean) -> Unit,
        isCancelled: () -> Boolean,
        // The wordmark, as the render pinned it at its start; see WatermarkOverlay.
        watermark: WatermarkChoice = WatermarkChoice.NONE,
    ): Result =
        withContext(Dispatchers.Default) {
            // Decoded once, at the export's own pixel size, and reused for whichever destination
            // path below runs - see BackgroundImage for why this never touches Main.
            val decoded = background?.let { BackgroundImage.decodeForExport(context, it, aspect.width, aspect.height) }
            val render = Render(audioUri, sceneFactory, aspect, look.copy(underlay = decoded), requestedFps, range, codec, watermark)
            if (destination != null) {
                return@withContext exportToDestination(destination, render, loudnessTarget, onProgress, isCancelled)
            }
            val resolver = context.contentResolver
            val values =
                ContentValues().apply {
                    put(MediaStore.Video.Media.DISPLAY_NAME, fileName)
                    put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                    put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/BeatRealms")
                    put(MediaStore.Video.Media.IS_PENDING, 1)
                }
            val outUri =
                resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)
                    ?: return@withContext failed(R.string.export_error_library_insert)
            val pfd = resolver.openFileDescriptor(outUri, "w")
            if (pfd == null) {
                bestEffort(TAG, "resolver.delete(outUri, null, null)") { resolver.delete(outUri, null, null) }
                return@withContext failed(R.string.export_error_library_open)
            }
            try {
                val kept = pfd.use { encodeInto(it, render, onProgress, isCancelled) }
                if (!kept) {
                    bestEffort(TAG, "resolver.delete(outUri, null, null)") { resolver.delete(outUri, null, null) }
                    Result.Cancelled
                } else if (publishPending(resolver.pendingRows(), outUri)) {
                    Result.Saved(outUri, measureLoudness(outUri, loudnessTarget))
                } else {
                    failed(R.string.export_error_library_publish)
                }
            } catch (e: Exception) {
                bestEffort(TAG, "resolver.delete(outUri, null, null)") { resolver.delete(outUri, null, null) }
                throw e
            }
        }

    /** What one render draws and how it is encoded, as the caller pinned it. */
    private class Render(
        val audioUri: Uri,
        val sceneFactory: SceneFactory,
        val aspect: ExportAspect,
        val look: LookSnapshot,
        val requestedFps: Int,
        val range: ExportRange?,
        val codec: ExportCodec,
        val watermark: WatermarkChoice,
    )

    private suspend fun exportToDestination(
        destination: Uri,
        render: Render,
        loudnessTarget: LoudnessTarget,
        onProgress: (Float, Boolean) -> Unit,
        isCancelled: () -> Boolean,
    ): Result {
        val resolver = context.contentResolver
        val pfd =
            resolver.openFileDescriptor(destination, "w")
                ?: return failed(R.string.export_error_destination_write)
        return try {
            val kept = pfd.use { encodeInto(it, render, onProgress, isCancelled) }
            if (!kept) {
                bestEffort(
                    TAG,
                    "DocumentsContract.deleteDocument(resolver, de...",
                ) { DocumentsContract.deleteDocument(resolver, destination) }
                Result.Cancelled
            } else {
                Result.Saved(destination, measureLoudness(destination, loudnessTarget))
            }
        } catch (e: Exception) {
            bestEffort(TAG, "DocumentsContract.deleteDocument(resolver, de...") { DocumentsContract.deleteDocument(resolver, destination) }
            throw e
        }
    }

    /**
     * Measures the audio Geode just muxed into [uri] and, if it decoded, turns that measurement
     * into advice for [target].
     *
     * This deliberately re-decodes the finished file rather than the source: it measures "the thing
     * that will actually be uploaded", and by the time [encodeInto] has run, the AAC track lives
     * inside a container the platform extractor opens, unlike [AudioTranscoder]'s raw `.bin`. Geode
     * does not yet apply the resulting gain: [AudioTranscoder] owns the only PCM Geode ever holds for
     * this file, so today this only reports what a platform would do to the file as exported. The
     * decode, the meter and the advice are native (`ExportAudioNative.exportLoudnessMeasure`).
     *
     * A file the encoder just finished writing successfully is not allowed to be reported as a
     * failed export over a problem in this purely-informational re-read, so any exception here is
     * swallowed to "no advice" rather than left to propagate into the caller's `catch`, which would
     * delete the file that was just saved.
     */
    private suspend fun measureLoudness(
        uri: Uri,
        target: LoudnessTarget,
    ): LoudnessAdvice? =
        try {
            withContext(Dispatchers.IO) {
                context.contentResolver.openAssetFileDescriptor(uri, "r")?.let { afd ->
                    val length = afd.declaredLength
                    val offset = if (length == AssetFileDescriptor.UNKNOWN_LENGTH) 0L else afd.startOffset
                    val out = DoubleArray(LOUDNESS_DOUBLES)
                    val fd = afd.parcelFileDescriptor.detachFd()
                    val sentence = ExportAudioNative.exportLoudnessMeasure(fd, offset, length, target.nativeIndex, out)
                    if (sentence < 0) {
                        null
                    } else {
                        LoudnessAdvice(target, sentence, out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], out[8] != 0.0)
                    }
                }
            }
        } catch (e: kotlinx.coroutines.CancellationException) {
            throw e
        } catch (e: Exception) {
            android.util.Log.w(TAG, "Post-export loudness measurement failed", e)
            null
        }

    /**
     * Writes the render into [pfd]: true when the file is finished and kept, false when the user cancelled
     * (the caller discards it). A failure throws, worded, so the caller discards the file and shows why.
     */
    private suspend fun encodeInto(
        pfd: ParcelFileDescriptor,
        render: Render,
        onProgress: (Float, Boolean) -> Unit,
        isCancelled: () -> Boolean,
    ): Boolean {
        // Seam for the native transcoder (#1229, the audio half): the shell's AudioTranscoder still makes the AAC
        // track, and setAudio hands its scratch file, samples and format to the job. A native transcoder fills
        // the same request (core/output/VideoExport's Request::audio) and this call and setAudio go.
        val aac =
            AudioTranscoder(context).transcode(
                uri = render.audioUri,
                maxDurationMs = render.range?.durationMs ?: 0L,
                startMs = render.range?.startMs ?: 0L,
                isCancelled = isCancelled,
            ) { onProgress(VideoExportNative.videoExportLeadIn(it), false) }
        val job = VideoExportNative.videoExportCreate()
        try {
            check(job != 0L) { "export job not created" }
            val aspect = render.aspect
            VideoExportNative.videoExportSetOutput(
                job,
                pfd.fd,
                context.cacheDir.absolutePath,
                aspect.width,
                aspect.height,
                aspect.bitRate,
                render.requestedFps,
            )
            render.codec.candidates().forEach { VideoExportNative.videoExportAddCodec(job, it.mimeType, it.topFps(aspect)) }
            setScene(job, render)
            setTrack(job, render)
            setMark(job, render)
            setAudio(job, aac)
            coroutineScope {
                val poll =
                    launch {
                        try {
                            while (isActive) {
                                if (isCancelled()) VideoExportNative.videoExportCancel(job)
                                onProgress(VideoExportNative.videoExportProgress(job), true)
                                delay(POLL_MS)
                            }
                        } finally {
                            // A cancel of the caller ends this poll while the run below still blocks its
                            // thread: only this stops the job. After the run has ended it changes nothing.
                            VideoExportNative.videoExportCancel(job)
                        }
                    }
                // Blocks this worker thread for the whole render; the poll above runs on another.
                VideoExportNative.videoExportRun(job)
                poll.cancelAndJoin()
            }
            return keptOrThrow(job, aspect, isCancelled)
        } finally {
            VideoExportNative.videoExportDestroy(job)
            aac.release()
        }
    }

    /** The renderer's inputs and the pinned look, on the scene the render draws. */
    private fun setScene(
        job: Long,
        render: Render,
    ) {
        VideoExportNative.videoExportSetScene(
            job,
            context.assets,
            render.sceneFactory.sceneId,
            ProSnapshot.gate(context).allows(ProFeature.AUTO_DIRECTOR),
        )
        val look = render.look
        VideoExportNative.videoExportSetLook(
            job,
            look.params.frame(),
            look.lfos,
            look.adsrs,
            look.soundTuning?.toFloatArray(),
            look.reactions,
            look.reducedMotion,
            look.transitionId,
            look.transitionMs,
        )
        val u = look.underlay
        VideoExportNative.videoExportSetUnderlay(job, u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
    }

    // What MediaExtractor and the AIFF reader were each handed for a content URI: the declared span, or the whole
    // descriptor when none is declared. Detached: native owns the descriptor and closes it.
    private fun setTrack(
        job: Long,
        render: Render,
    ) {
        val afd =
            requireNotNull(context.contentResolver.openAssetFileDescriptor(render.audioUri, "r")) { "the track cannot be opened" }
        val length = afd.declaredLength
        val offset = if (length == AssetFileDescriptor.UNKNOWN_LENGTH) 0L else afd.startOffset
        VideoExportNative.videoExportSetTrack(job, afd.parcelFileDescriptor.detachFd(), offset, length, render.range?.startMs ?: 0L)
    }

    /** The wordmark's tile, when these frames carry one; a free render never ships unmarked. */
    private fun setMark(
        job: Long,
        render: Render,
    ) {
        val tile = WatermarkOverlay(render.watermark, context).tile(render.aspect.width, render.aspect.height) ?: return
        check(VideoExportNative.videoExportSetMark(job, tile.pixels, tile.width, tile.height, tile.placement, tile.corner)) {
            "watermark pass failed: out of memory"
        }
    }

    /** The transcoder's track as it made it: its scratch file, its samples and every key of its format. */
    private fun setAudio(
        job: Long,
        aac: AudioTranscoder.Result,
    ) {
        val samples = LongArray(aac.sampleInfos.size * SAMPLE_LONGS)
        aac.sampleInfos.forEachIndexed { i, s ->
            samples[i * SAMPLE_LONGS] = s.offset
            samples[i * SAMPLE_LONGS + 1] = s.size.toLong()
            samples[i * SAMPLE_LONGS + 2] = s.presentationTimeUs
            samples[i * SAMPLE_LONGS + 3] = s.flags.toLong()
        }
        check(VideoExportNative.videoExportSetAudio(job, aac.file.absolutePath, samples)) { "the audio track cannot be read back" }
        val f = aac.format
        for (key in f.keys) {
            val type = f.getValueTypeForKey(key)
            val whole =
                when (type) {
                    MediaFormat.TYPE_INTEGER -> f.getInteger(key).toLong()
                    MediaFormat.TYPE_LONG -> f.getLong(key)
                    else -> 0L
                }
            val real = if (type == MediaFormat.TYPE_FLOAT) f.getFloat(key) else 0f
            val text = if (type == MediaFormat.TYPE_STRING) f.getString(key) else null
            val bytes = if (type == MediaFormat.TYPE_BYTE_BUFFER) f.getByteBuffer(key)?.duplicate()?.let(::bytesOf) else null
            VideoExportNative.videoExportAddAudioKey(job, key, type, whole, real, text, bytes)
        }
    }

    private fun bytesOf(buffer: java.nio.ByteBuffer): ByteArray = ByteArray(buffer.remaining()).also { buffer.get(it) }

    /** What the finished job says: kept, cancelled, or a failure thrown in the shell's words. */
    private fun keptOrThrow(
        job: Long,
        aspect: ExportAspect,
        isCancelled: () -> Boolean,
    ): Boolean {
        val state = VideoExportNative.videoExportState(job)
        // A cancel that lands after the last frame still discards the file, as the button promised.
        if (state == VideoExportNative.VIDEO_EXPORT_RENDERED) return !isCancelled()
        if (state == VideoExportNative.VIDEO_EXPORT_CANCELLED) return false
        val detail = VideoExportNative.videoExportDetail(job).orEmpty()
        val failure = VideoExportNative.videoExportFailure(job)
        RingLog.note(TAG, "video export failed ($failure): $detail")
        val message =
            when (failure) {
                VideoExportNative.VIDEO_FAILURE_ENCODER_REJECTED ->
                    context.getString(R.string.export_error_loop_encoder_rejected, aspect.width, aspect.height)
                VideoExportNative.VIDEO_FAILURE_NO_OUTPUT -> context.getString(R.string.export_error_encoder_no_output)
                VideoExportNative.VIDEO_FAILURE_STALLED -> context.getString(R.string.export_error_encoder_stalled)
                VideoExportNative.VIDEO_FAILURE_EMPTY_BUFFER -> context.getString(R.string.export_error_encoder_buffer_null)
                else -> detail.ifEmpty { context.getString(R.string.export_error_no_message) }
            }
        error(message)
    }
}

// The long[] videoExportSetAudio takes per sample: offset, size, ptsUs, flags.
private const val SAMPLE_LONGS = 4

private const val TAG = "VideoExporter"
