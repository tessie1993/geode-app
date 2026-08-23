package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import dev.geode.analysis.FeatureTimeline
import dev.geode.render.SceneFactory
import dev.geode.render.offscreen.OffscreenRenderSpec
import dev.geode.render.offscreen.OffscreenSceneRenderer
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer

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
        ) : Result

        data class Failed(
            val message: String,
        ) : Result

        data object Cancelled : Result
    }

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
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result =
        withContext(Dispatchers.Default) {
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
                    ?: return@withContext Result.Failed(
                        "Your Videos library would not accept a new file. Check that storage is not full, " +
                            "or render to a folder you choose instead.",
                    )
            val pfd = resolver.openFileDescriptor(outUri, "w")
            if (pfd == null) {
                bestEffort(TAG, "resolver.delete(outUri, null, null)") { resolver.delete(outUri, null, null) }
                return@withContext Result.Failed("The new file in your Videos library could not be opened for writing.")
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
                    Result.Saved(outUri)
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
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result {
        val resolver = context.contentResolver
        val pfd =
            resolver.openFileDescriptor(destination, "w")
                ?: return Result.Failed(
                    "The folder you chose would not let the file be written. Some cloud providers refuse " +
                        "this; try your Videos library or a folder on the device.",
                )
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
                Result.Saved(destination)
            }
        } catch (e: Exception) {
            bestEffort(TAG, "DocumentsContract.deleteDocument(resolver, de...") { DocumentsContract.deleteDocument(resolver, destination) }
            throw e
        }
    }

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
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ) {
        fun makeFormat(
            fps: Int,
            bitRate: Int,
        ): MediaFormat =
            MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, aspect.width, aspect.height).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE, bitRate)
                setInteger(MediaFormat.KEY_FRAME_RATE, fps)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            }
        var encoderRef: MediaCodec? = null
        var inputSurfaceRef: android.view.Surface? = null
        var muxerRef: MediaMuxer? = null
        var aacRef: AudioTranscoder.Result? = null
        var eglRef: EncoderSurface? = null
        var rendererRef: OffscreenSceneRenderer? = null
        var audioFeedRef: AudioFeed? = null
        var muxerStarted = false
        var muxerStopped = false
        try {
            var encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC).also { encoderRef = it }
            var fps = requestedFps.coerceIn(24, 60)
            try {
                encoder.configure(makeFormat(fps, aspect.bitRate), null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            } catch (e: Exception) {
                bestEffort(TAG, "encoder.release()") { encoder.release() }
                encoderRef = null
                encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC).also { encoderRef = it }
                fps = 30
                encoder.configure(makeFormat(30, aspect.bitRate * 2 / 3), null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            }
            val inputSurface = encoder.createInputSurface().also { inputSurfaceRef = it }
            encoder.start()

            val muxer = MediaMuxer(pfd.fileDescriptor, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4).also { muxerRef = it }
            val rangeStartMs = range?.startMs ?: 0L
            val aac =
                AudioTranscoder(context)
                    .transcode(
                        uri = audioUri,
                        maxDurationMs = range?.durationMs ?: 0L,
                        startMs = rangeStartMs,
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
                        videoTrack = muxer.addTrack(encoder.outputFormat)
                        audioTrack = muxer.addTrack(aac.format)
                        muxer.start()
                        muxerStarted = true
                    } else if (outIndex >= 0) {
                        val buf = checkNotNull(encoder.getOutputBuffer(outIndex)) { "video encoder buffer null (codec error state)" }
                        writeSample(muxer, videoTrack, buf, info, muxerStarted)
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
                        videoTrack = muxer.addTrack(encoder.outputFormat)
                        audioTrack = muxer.addTrack(aac.format)
                        muxer.start()
                        muxerStarted = true
                    }
                    outIndex >= 0 -> {
                        val buf = checkNotNull(encoder.getOutputBuffer(outIndex)) { "video encoder buffer null (codec error state)" }
                        writeSample(muxer, videoTrack, buf, info, muxerStarted)
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
            check(muxerStarted || isCancelled()) { "Video encoder produced no output (encoder/format unsupported?)" }
            check(sawEos || isCancelled()) { "Video encoder stalled while flushing - export incomplete" }
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
            if (muxerStarted && !muxerStopped) runCatching { muxerRef?.stop() }
            bestEffort(TAG, "muxerRef?.release()") { muxerRef?.release() }
            bestEffort(TAG, "encoderRef?.stop()") { encoderRef?.stop() }
            bestEffort(TAG, "encoderRef?.release()") { encoderRef?.release() }
            bestEffort(TAG, "inputSurfaceRef?.release()") { inputSurfaceRef?.release() }
            bestEffort(TAG, "eglRef?.release()") { eglRef?.release() }
            aacRef?.release()
        }
    }

    private fun writeSample(
        muxer: MediaMuxer,
        track: Int,
        buffer: ByteBuffer,
        info: MediaCodec.BufferInfo,
        started: Boolean,
    ) {
        if (!started || track < 0 || info.size <= 0) return
        if (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0) return
        buffer.position(info.offset)
        buffer.limit(info.offset + info.size)
        muxer.writeSampleData(track, buffer, info)
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
            }
        }

        override fun close() {
            bestEffort(TAG, "raf.close()") { raf.close() }
        }
    }
}

private const val TAG = "VideoExporter"
