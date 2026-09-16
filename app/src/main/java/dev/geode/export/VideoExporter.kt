package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.media.MediaMuxer
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.RingLog
import dev.geode.analysis.FeatureTimeline
import dev.geode.render.SceneFactory
import dev.geode.render.offscreen.OffscreenRenderSpec
import dev.geode.render.offscreen.OffscreenSceneRenderer
import dev.geode.render.offscreen.OffscreenUnderlay
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort
import dev.geode.viz.BackgroundExportSpec
import dev.geode.viz.BackgroundImage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer

enum class ExportCodec(
    val mimeType: String,
) {
    H264(MediaFormat.MIMETYPE_VIDEO_AVC),
    HEVC(MediaFormat.MIMETYPE_VIDEO_HEVC),
    ;

    /** This codec when the device has an encoder for it, otherwise H.264, which every device has. */
    fun available(): ExportCodec = if (this == H264 || hasEncoder(mimeType)) this else H264

    companion object {
        fun hasEncoder(mimeType: String): Boolean =
            MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.any { info ->
                info.isEncoder && info.supportedTypes.any { it.equals(mimeType, ignoreCase = true) }
            }
    }
}

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
        fun of(
            quality: ExportQuality,
            ratio: ExportRatio,
        ): ExportAspect {
            val short = quality.shortSide
            val landscape = ratio.wRatio >= ratio.hRatio
            var longSide = (short.toLong() * maxOf(ratio.wRatio, ratio.hRatio) / minOf(ratio.wRatio, ratio.hRatio)).toInt()
            var shortSide = short
            if (longSide > MAX_AVC_DIM) {
                shortSide = (short.toLong() * MAX_AVC_DIM / longSide).toInt()
                longSide = MAX_AVC_DIM
            }
            val w = if (landscape) longSide else shortSide
            val h = if (landscape) shortSide else longSide
            return ExportAspect(even(w), even(h), quality.bitRate)
        }

        private const val MAX_AVC_DIM = 4096

        private fun even(v: Int): Int = if (v % 2 == 0) v else v + 1
    }
}

class VideoExporter(
    private val context: Context,
) {
    companion object {
        private const val FPS: Int = 60
        private const val TIMEOUT_US: Long = 10_000

        private const val FLUSH_ATTEMPT_LIMIT = 1_000
    }

    sealed interface Result {
        data class Saved(
            val uri: Uri,
            /**
             * How the finished file's loudness compares to [loudnessTarget] *after* export — the
             * gain toward that target was already applied while transcoding (see
             * [AudioTranscoder.sourceGain]), so for a [LoudnessTarget.Normalising] target this is
             * confirmation the file landed where it should, not a pending correction.
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

    suspend fun export(
        audioUri: Uri,
        timeline: FeatureTimeline,
        sceneFactory: SceneFactory,
        aspect: ExportAspect,
        fileName: String,
        sceneParams: SceneParams,
        lfoConfigs: List<dev.geode.render.LfoConfig> = emptyList(),
        adsrConfigs: List<dev.geode.render.AdsrConfig> = emptyList(),
        reducedMotion: Boolean = false,
        requestedFps: Int = FPS,
        range: ExportRange? = null,
        paramsAt: ((Long) -> SceneParams)? = null,
        loopSafe: Boolean = false,
        destination: Uri? = null,
        codec: ExportCodec = ExportCodec.H264,
        loudnessTarget: LoudnessTarget = LoudnessTarget.LeaveAsIs,
        /**
         * Full-frame ARGB overlay (cover art/title/lyrics) sized to [aspect], as a function from a
         * track position (ms) to that frame's pixels (`Bitmap.getPixels` shape); null draws none
         * for that frame. Null overlay draws nothing for the whole export.
         */
        overlay: ((positionMs: Long) -> IntArray?)? = null,
        background: BackgroundExportSpec? = null,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result =
        withContext(Dispatchers.Default) {
            // Decoded once, at the export's own pixel size, and reused for whichever destination
            // path below runs - see BackgroundImage for why this never touches Main.
            val underlay = background?.let { BackgroundImage.decodeForExport(context, it, aspect.width, aspect.height) }
            if (destination != null) {
                return@withContext exportToDestination(
                    destination,
                    audioUri,
                    timeline,
                    sceneFactory,
                    aspect,
                    sceneParams,
                    lfoConfigs,
                    adsrConfigs,
                    reducedMotion,
                    requestedFps,
                    paramsAt,
                    loopSafe,
                    range,
                    codec,
                    loudnessTarget,
                    overlay,
                    underlay,
                    onProgress,
                    isCancelled,
                )
            }
            val resolver = context.contentResolver
            val values =
                ContentValues().apply {
                    put(MediaStore.Video.Media.DISPLAY_NAME, fileName)
                    put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                    if (android.os.Build.VERSION.SDK_INT >= 29) {
                        put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/Geode")
                        put(MediaStore.Video.Media.IS_PENDING, 1)
                    }
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
                pfd.use {
                    encodeInto(
                        it,
                        audioUri,
                        timeline,
                        sceneFactory,
                        aspect,
                        sceneParams,
                        lfoConfigs,
                        adsrConfigs,
                        reducedMotion,
                        requestedFps,
                        paramsAt,
                        loopSafe,
                        range,
                        codec,
                        loudnessTarget,
                        overlay,
                        underlay,
                        onProgress,
                        isCancelled,
                    )
                }
                if (isCancelled()) {
                    bestEffort(TAG, "resolver.delete(outUri, null, null)") { resolver.delete(outUri, null, null) }
                    Result.Cancelled
                } else {
                    if (android.os.Build.VERSION.SDK_INT >= 29) {
                        val done = ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }
                        resolver.update(outUri, done, null, null)
                    }
                    Result.Saved(outUri, measureLoudness(outUri, loudnessTarget))
                }
            } catch (e: Exception) {
                bestEffort(TAG, "resolver.delete(outUri, null, null)") { resolver.delete(outUri, null, null) }
                throw e
            }
        }

    private suspend fun exportToDestination(
        destination: Uri,
        audioUri: Uri,
        timeline: FeatureTimeline,
        sceneFactory: SceneFactory,
        aspect: ExportAspect,
        sceneParams: SceneParams,
        lfoConfigs: List<dev.geode.render.LfoConfig>,
        adsrConfigs: List<dev.geode.render.AdsrConfig>,
        reducedMotion: Boolean,
        requestedFps: Int,
        paramsAt: ((Long) -> SceneParams)?,
        loopSafe: Boolean,
        range: ExportRange?,
        codec: ExportCodec,
        loudnessTarget: LoudnessTarget,
        overlay: ((positionMs: Long) -> IntArray?)?,
        underlay: OffscreenUnderlay?,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result {
        val resolver = context.contentResolver
        val pfd =
            resolver.openFileDescriptor(destination, "w")
                ?: return failed(R.string.export_error_destination_write)
        return try {
            pfd.use {
                encodeInto(
                    it,
                    audioUri,
                    timeline,
                    sceneFactory,
                    aspect,
                    sceneParams,
                    lfoConfigs,
                    adsrConfigs,
                    reducedMotion,
                    requestedFps,
                    paramsAt,
                    loopSafe,
                    range,
                    codec,
                    loudnessTarget,
                    overlay,
                    underlay,
                    onProgress,
                    isCancelled,
                )
            }
            if (isCancelled()) {
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
     * This deliberately re-decodes the finished file rather than the source: [LoudnessMeter] is
     * built to measure "the thing that will actually be uploaded", and by the time [encodeInto]
     * has run, the AAC track already lives inside a container [LoudnessMeter] can open directly —
     * unlike [AudioTranscoder]'s intermediate `.bin`, which is a raw elementary stream with no
     * container framing for [android.media.MediaExtractor] to parse. [encodeInto] already applied
     * a normalising gain via [AudioTranscoder.sourceGain] before this runs, so for a
     * [LoudnessTarget.Normalising] target this is confirmation of where the file landed, not the
     * only place a gain gets computed.
     *
     * A file the encoder just finished writing successfully is not allowed to be reported as a
     * failed export over a problem in this purely-informational re-read, so any exception here is
     * swallowed to "no advice" rather than left to propagate into the caller's `catch`, which would
     * delete the file that was just saved.
     */
    private suspend fun measureLoudness(
        uri: Uri,
        target: LoudnessTarget,
    ): LoudnessAdvice? {
        val result =
            try {
                LoudnessMeter(context).measure(uri)
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (e: Exception) {
                android.util.Log.w(TAG, "Post-export loudness measurement failed", e)
                return null
            }
        return (result as? LoudnessResult.Measured)?.let { LoudnessTargets.advise(it.report, target) }
    }

    @Suppress("NestedBlockDepth")
    private fun encodeInto(
        pfd: ParcelFileDescriptor,
        audioUri: Uri,
        timeline: FeatureTimeline,
        sceneFactory: SceneFactory,
        aspect: ExportAspect,
        sceneParams: SceneParams,
        lfoConfigs: List<dev.geode.render.LfoConfig>,
        adsrConfigs: List<dev.geode.render.AdsrConfig>,
        reducedMotion: Boolean,
        requestedFps: Int,
        paramsAt: ((Long) -> SceneParams)?,
        loopSafe: Boolean,
        range: ExportRange?,
        codec: ExportCodec,
        loudnessTarget: LoudnessTarget,
        overlay: ((positionMs: Long) -> IntArray?)?,
        underlay: OffscreenUnderlay?,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ) {
        var encoderRef: MediaCodec? = null
        var inputSurfaceRef: android.view.Surface? = null
        var muxerRef: MediaMuxer? = null
        var aacRef: AudioTranscoder.Result? = null
        var eglRef: EncoderSurface? = null
        var rendererRef: OffscreenSceneRenderer? = null
        var audioFeedRef: AudioFeed? = null
        var muxerStarted = false
        var muxerStopped = false
        var sampleWritten = false
        try {
            val requestedFpsBounded = requestedFps.coerceIn(24, 60)
            // The requested codec at the requested rate first, then its reduced form, then the same pair on H.264.
            val attempts =
                listOf(codec.available(), ExportCodec.H264).distinct().flatMap { c ->
                    listOf(EncoderAttempt(c, requestedFpsBounded, aspect.bitRate), EncoderAttempt(c, 30, aspect.bitRate * 2 / 3))
                }
            val (encoder, fps) = openEncoder(aspect, attempts)
            encoderRef = encoder
            val inputSurface = encoder.createInputSurface().also { inputSurfaceRef = it }
            encoder.start()

            val muxer = MediaMuxer(pfd.fileDescriptor, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4).also { muxerRef = it }
            val rangeStartMs = range?.startMs ?: 0L
            val audioTranscoder = AudioTranscoder(context)
            // A second, dedicated decode of the source to read its level before any gain is baked
            // into the AAC track — the cost of actually applying [loudnessTarget] instead of only
            // measuring the result afterward. Bounded to the same range that gets exported, so the
            // gain is computed against the clip that ships rather than the whole source file it may
            // be trimmed from. See AudioTranscoder.sourceGain for why this can't be folded into the
            // transcode pass below.
            val gain =
                audioTranscoder.sourceGain(
                    uri = audioUri,
                    target = loudnessTarget,
                    startMs = rangeStartMs,
                    maxDurationMs = range?.durationMs ?: 0L,
                    isCancelled = isCancelled,
                )
            val aac =
                audioTranscoder
                    .transcode(
                        uri = audioUri,
                        maxDurationMs = range?.durationMs ?: 0L,
                        startMs = rangeStartMs,
                        gain = gain,
                        isCancelled = isCancelled,
                    ) { onProgress(it * 0.1f) }
                    .also { aacRef = it }
            val egl = EncoderSurface(inputSurface).also { eglRef = it }
            egl.makeCurrent()

            val sourceDurationUs = if (aac.durationUs > 0) aac.durationUs else timeline.durationMs * 1000
            val exportDurationUs =
                if (loopSafe) {
                    dev.geode.analysis.BarTrim
                        .trimToBars(sourceDurationUs, timeline.bpm)
                } else {
                    sourceDurationUs
                }
            val totalFrames = (exportDurationUs * fps / 1_000_000L).toInt().coerceAtLeast(1)
            val frameDurationNs = 1_000_000_000L / fps

            val renderer =
                OffscreenSceneRenderer(
                    context = context,
                    sceneFactory = sceneFactory,
                    timeline = timeline,
                    spec =
                        OffscreenRenderSpec(
                            width = aspect.width,
                            height = aspect.height,
                            fps = fps,
                            totalFrames = totalFrames,
                            rangeStartMs = rangeStartMs,
                            baseParams = sceneParams,
                            lfoConfigs = lfoConfigs,
                            adsrConfigs = adsrConfigs,
                            reducedMotion = reducedMotion,
                            paramsAt = paramsAt,
                            overlay = overlay,
                            underlay = underlay,
                        ),
                ).also { rendererRef = it }
            renderer.prepare()

            var videoTrack = -1
            var audioTrack = -1
            val info = MediaCodec.BufferInfo()

            for (frame in 0 until totalFrames) {
                if (isCancelled()) break
                renderer.renderFrame(frame)
                val timeMs = frame * 1000L / fps
                egl.setPresentationTimeNs(frame * frameDurationNs)
                egl.swapBuffers()

                while (true) {
                    val outIndex = encoder.dequeueOutputBuffer(info, 0)
                    if (outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        if (muxerStarted) {
                            // Some vendors legally re-signal a format change after start(); the muxer
                            // cannot add a track once running, so the original track stays in use.
                            RingLog.note(TAG, "encoder re-signalled INFO_OUTPUT_FORMAT_CHANGED after the muxer had already started")
                        } else {
                            videoTrack = muxer.addTrack(encoder.outputFormat)
                            audioTrack = muxer.addTrack(aac.format)
                            muxer.start()
                            muxerStarted = true
                        }
                    } else if (outIndex >= 0) {
                        val buf =
                            checkNotNull(encoder.getOutputBuffer(outIndex)) { context.getString(R.string.export_error_encoder_buffer_null) }
                        if (writeSample(muxer, videoTrack, buf, info, muxerStarted)) sampleWritten = true
                        encoder.releaseOutputBuffer(outIndex, false)
                    } else {
                        break
                    }
                }
                if (muxerStarted && audioTrack >= 0) {
                    val feed =
                        audioFeedRef ?: AudioFeed(muxer, audioTrack, aac, exportDurationUs).also { audioFeedRef = it }
                    feed.writeUpTo(timeMs * 1000L)
                }
                onProgress(0.1f + frame / totalFrames.toFloat() * 0.85f)
            }
            encoder.signalEndOfInputStream()
            var flushAttempts = 0
            var sawEos = false
            drain@ while (flushAttempts < FLUSH_ATTEMPT_LIMIT) {
                val outIndex = encoder.dequeueOutputBuffer(info, TIMEOUT_US)
                when {
                    outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        if (muxerStarted) {
                            RingLog.note(TAG, "encoder re-signalled INFO_OUTPUT_FORMAT_CHANGED while draining; ignoring")
                        } else {
                            videoTrack = muxer.addTrack(encoder.outputFormat)
                            audioTrack = muxer.addTrack(aac.format)
                            muxer.start()
                            muxerStarted = true
                        }
                    }
                    outIndex >= 0 -> {
                        val buf =
                            checkNotNull(encoder.getOutputBuffer(outIndex)) { context.getString(R.string.export_error_encoder_buffer_null) }
                        if (writeSample(muxer, videoTrack, buf, info, muxerStarted)) sampleWritten = true
                        val eos = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                        encoder.releaseOutputBuffer(outIndex, false)
                        if (eos) {
                            sawEos = true
                            break@drain
                        }
                    }
                    else -> flushAttempts++
                }
            }
            check(muxerStarted || isCancelled()) { context.getString(R.string.export_error_encoder_no_output) }
            check(sawEos || isCancelled()) { context.getString(R.string.export_error_encoder_stalled) }
            if (muxerStarted && !isCancelled() && audioTrack >= 0) {
                val feed =
                    audioFeedRef ?: AudioFeed(muxer, audioTrack, aac, exportDurationUs).also { audioFeedRef = it }
                feed.writeUpTo(Long.MAX_VALUE)
                onProgress(1f)
            }
            if (muxerStarted && !isCancelled()) {
                muxer.stop()
                muxerStopped = true
            }
        } finally {
            bestEffort(TAG, "rendererRef?.release()") { rendererRef?.release() }
            bestEffort(TAG, "audioFeedRef?.close()") { audioFeedRef?.close() }
            if (muxerStarted && !muxerStopped) {
                val wroteSamples = sampleWritten || audioFeedRef?.wroteSample == true
                if (wroteSamples) {
                    bestEffort(TAG, "muxerRef?.stop()") { muxerRef?.stop() }
                } else {
                    // A muxer that started (via INFO_OUTPUT_FORMAT_CHANGED) but never received a
                    // sample — e.g. cancelled before the first frame drained — cannot be stopped:
                    // MediaMuxer.stop() with zero samples always throws IllegalStateException. The
                    // caller deletes the (empty/invalid) output file once it sees the cancellation
                    // or the exception this finally block would otherwise have swallowed.
                    RingLog.note(TAG, "muxer started but wrote no samples; skipping stop() to avoid a guaranteed IllegalStateException")
                }
            }
            bestEffort(TAG, "muxerRef?.release()") { muxerRef?.release() }
            bestEffort(TAG, "encoderRef?.stop()") { encoderRef?.stop() }
            bestEffort(TAG, "encoderRef?.release()") { encoderRef?.release() }
            bestEffort(TAG, "inputSurfaceRef?.release()") { inputSurfaceRef?.release() }
            bestEffort(TAG, "eglRef?.release()") { eglRef?.release() }
            aacRef?.release()
        }
    }

    private class EncoderAttempt(
        val codec: ExportCodec,
        val fps: Int,
        val bitRate: Int,
    )

    private fun videoFormat(
        aspect: ExportAspect,
        attempt: EncoderAttempt,
    ): MediaFormat =
        MediaFormat.createVideoFormat(attempt.codec.mimeType, aspect.width, aspect.height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, attempt.bitRate)
            setInteger(MediaFormat.KEY_FRAME_RATE, attempt.fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
        }

    /** The first attempt the device's encoder accepts, with the frame rate it was configured for. */
    private fun openEncoder(
        aspect: ExportAspect,
        attempts: List<EncoderAttempt>,
    ): Pair<MediaCodec, Int> {
        var failure: Exception? = null
        for (attempt in attempts) {
            val candidate = MediaCodec.createEncoderByType(attempt.codec.mimeType)
            try {
                candidate.configure(videoFormat(aspect, attempt), null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                return candidate to attempt.fps
            } catch (e: Exception) {
                bestEffort(TAG, "candidate.release()") { candidate.release() }
                failure = e
            }
        }
        throw checkNotNull(failure)
    }

    /** Writes one encoded video sample to [muxer], returning whether it actually wrote one. */
    private fun writeSample(
        muxer: MediaMuxer,
        track: Int,
        buffer: ByteBuffer,
        info: MediaCodec.BufferInfo,
        started: Boolean,
    ): Boolean {
        if (!started || track < 0 || info.size <= 0) return false
        if (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0) return false
        buffer.position(info.offset)
        buffer.limit(info.offset + info.size)
        muxer.writeSampleData(track, buffer, info)
        return true
    }

    private class AudioFeed(
        private val muxer: MediaMuxer,
        private val track: Int,
        private val aac: AudioTranscoder.Result,
        private val limitUs: Long,
    ) : java.io.Closeable {
        private val raf = java.io.RandomAccessFile(aac.file, "r")
        private val info = MediaCodec.BufferInfo()
        private var scratch = ByteBuffer.allocate(64 * 1024)
        private var next = 0

        /** Whether any audio sample has actually reached the muxer via [writeUpTo]. */
        var wroteSample: Boolean = false
            private set

        fun writeUpTo(upToUs: Long) {
            val channel = raf.channel
            while (next < aac.sampleInfos.size) {
                val sample = aac.sampleInfos[next]
                if (sample.presentationTimeUs >= upToUs) return
                if (sample.presentationTimeUs >= limitUs) {
                    next = aac.sampleInfos.size
                    return
                }
                next++
                if (scratch.capacity() < sample.size) scratch = ByteBuffer.allocate(sample.size)
                scratch.clear()
                scratch.limit(sample.size)
                var read = 0
                while (read < sample.size) {
                    val n = channel.read(scratch, sample.offset + read)
                    if (n <= 0) break
                    read += n
                }
                scratch.flip()
                info.set(0, read, sample.presentationTimeUs, sample.flags)
                muxer.writeSampleData(track, scratch, info)
                wroteSample = true
            }
        }

        override fun close() {
            bestEffort(TAG, "raf.close()") { raf.close() }
        }
    }
}

private const val TAG = "VideoExporter"
