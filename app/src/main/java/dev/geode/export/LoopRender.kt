package dev.geode.export

import android.content.Context
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.net.Uri
import android.util.Log
import android.view.Surface
import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.RingLog
import dev.geode.billing.ProSnapshot
import dev.geode.engine.bridge.GeodeNative
import dev.geode.pro.ProFeature
import dev.geode.render.SceneFactory
import dev.geode.render.look.LookSnapshot
import dev.geode.render.offscreen.OffscreenRenderSpec
import dev.geode.render.offscreen.OffscreenSceneRenderer
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.Closeable
import java.io.File
import java.io.IOException

/**
 * The shape of one visual loop: how long it runs, how long its seam takes to dissolve, and at
 * what rate it was rendered.
 *
 * A long-form render never draws three hours of frames. It draws this much, once, and
 * [LoopExtend] repeats the encoded result. Everything downstream — the seam preview, the
 * repeat count, the palette stops — is derived from these three numbers, so they are settled
 * once, at the boundary, and passed around as a value.
 */
data class LoopSpec(
    val loopMs: Long,
    val crossfadeMs: Long,
    val fps: Int,
) {
    init {
        require(loopMs > 0) { "loopMs must be positive, was $loopMs" }
        require(crossfadeMs > 0) { "crossfadeMs must be positive, was $crossfadeMs" }
        require(crossfadeMs * 2 <= loopMs) { "crossfadeMs ($crossfadeMs) must not exceed half of loopMs ($loopMs)" }
        require(fps in MIN_FPS..MAX_FPS) { "fps must be within $MIN_FPS..$MAX_FPS, was $fps" }
    }

    // Frame counts and duration are native (`core/output/LoopPlan`, tested by `core/tests/output_test.cpp`),
    // derived once per spec.
    private val derived: LongArray by lazy {
        LongArray(GeodeNative.LOOP_SPEC_LONGS).also { GeodeNative.loopSpecDerive(loopMs, crossfadeMs, fps, it) }
    }

    /** Frames the finished loop contains — what the viewer sees once per repeat. */
    val loopFrames: Int get() = derived[GeodeNative.LOOP_SPEC_LOOP_FRAMES].toInt()

    /** Frames the dissolve spans. Always at least one, never more than half the loop. */
    val crossfadeFrames: Int get() = derived[GeodeNative.LOOP_SPEC_CROSSFADE_FRAMES].toInt()

    /**
     * Frames the renderer actually draws. The extra tail past [loopFrames] is the natural
     * continuation of the loop, which is what the opening frames dissolve out of.
     */
    val sourceFrames: Int get() = derived[GeodeNative.LOOP_SPEC_SOURCE_FRAMES].toInt()

    val loopDurationUs: Long get() = derived[GeodeNative.LOOP_SPEC_LOOP_DURATION_US]

    companion object {
        const val MIN_LOOP_MS: Long = 30_000
        const val MAX_LOOP_MS: Long = 300_000
        const val MIN_CROSSFADE_MS: Long = 200
        const val MAX_CROSSFADE_MS: Long = 5_000
        const val DEFAULT_CROSSFADE_MS: Long = 1_000
        const val MIN_FPS: Int = 24
        const val MAX_FPS: Int = 60

        /** Parses loose UI numbers into a spec, clamping instead of failing (natively, `core/output/LoopPlan`). */
        fun of(
            loopMs: Long,
            crossfadeMs: Long = DEFAULT_CROSSFADE_MS,
            fps: Int = MAX_FPS,
        ): LoopSpec {
            val out = LongArray(GeodeNative.LOOP_SPEC_LONGS)
            GeodeNative.loopSpecOf(loopMs, crossfadeMs, fps, out)
            return LoopSpec(
                out[GeodeNative.LOOP_SPEC_LOOP_MS],
                out[GeodeNative.LOOP_SPEC_CROSSFADE_MS],
                out[GeodeNative.LOOP_SPEC_FPS].toInt(),
            )
        }
    }
}

/**
 * Slow palette movement across the whole finished video, so a three-hour render is not the same
 * picture for three hours.
 *
 * The extension step reuses one encoded loop verbatim, so pixels cannot change from one repeat
 * to the next without decoding and re-encoding them — which is exactly the cost long-form mode
 * exists to avoid. The drift is therefore quantised: [stops] loops are rendered, each with its
 * own palette offset, and each fills its share of the running time. A step lands on a loop seam,
 * where the motion has already dissolved, and a few degrees of hue an hour reads as drift rather
 * than as a cut.
 */
data class TimeOfDayDrift(
    val hueTurns: Float,
    val warmth: Float,
    val stops: Int,
) {
    init {
        require(stops in 1..MAX_STOPS) { "stops must be within 1..$MAX_STOPS, was $stops" }
        require(hueTurns.isFinite()) { "hueTurns must be finite, was $hueTurns" }
        require(warmth.isFinite()) { "warmth must be finite, was $warmth" }
    }

    /**
     * The palette offset for each stop, from none at the start to the full drift at the end.
     * One stop means one render and no drift. The phases are native (`core/output/LoopPlan`,
     * pinned by `core/tests/output_test.cpp`).
     */
    fun stopPhases(): List<DriftStop> {
        val pairs = GeodeNative.loopDriftStops(hueTurns, warmth, stops)
        return List(pairs.size / 2) { index -> DriftStop(index, pairs[index * 2], pairs[index * 2 + 1]) }
    }

    companion object {
        val None: TimeOfDayDrift = TimeOfDayDrift(0f, 0f, 1)

        const val MAX_STOPS: Int = 8
    }
}

/** One palette position along the drift, and the parameter change that realises it. */
data class DriftStop(
    val index: Int,
    val hueOffset: Float,
    val warmthOffset: Float,
) {
    /** colorShift moved and wrapped into a turn, temperature moved and held to its range, natively. */
    fun applyTo(params: SceneParams): SceneParams {
        val out = FloatArray(2)
        GeodeNative.loopDriftApply(params.colorShift, params.temperature, hueOffset, warmthOffset, out)
        return params.copy(colorShift = out[0], temperature = out[1])
    }
}

/** One rendered loop: a self-contained, video-only MP4 that starts on a keyframe. */
class RenderedLoop(
    val file: File,
    val stop: DriftStop,
) {
    fun delete() {
        bestEffort(TAG, "file.delete()") { file.delete() }
    }
}

/**
 * Everything [LoopExtend] needs to build the long-form file: the loops, and the shape they were
 * rendered to.
 */
class LoopReel(
    val loops: List<RenderedLoop>,
    val spec: LoopSpec,
    val width: Int,
    val height: Int,
) {
    val loopDurationUs: Long get() = spec.loopDurationUs

    val bytes: Long get() = loops.sumOf { it.file.length() }

    fun delete() {
        loops.forEach { it.delete() }
    }
}

/**
 * Renders the visual loop that a long-form video is built from.
 *
 * The renderer draws [LoopSpec.sourceFrames] frames — one loop plus a tail — and dissolves the
 * tail into the stashed opening frames, so the last frame of the encoded loop flows into the
 * first exactly as two consecutive frames would. Repeating that loop is then seamless without
 * anything downstream having to touch pixels again.
 */
class LoopRender(
    private val context: Context,
) {
    sealed interface Result {
        data class Rendered(
            val reel: LoopReel,
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

    /**
     * The summed length of [uris] as their containers report it, for [render]'s `longFormMs`. A
     * clip that cannot be read counts as zero: the cap is then looser, never wrong in the other
     * direction, and LoopExtend still refuses a file that would not fit. Blocking; call off Main.
     */
    fun soundtrackMs(uris: List<Uri>): Long =
        uris.sumOf { uri ->
            var ms = 0L
            bestEffort(TAG, "read the duration of a soundtrack clip") {
                val retriever = android.media.MediaMetadataRetriever()
                try {
                    retriever.setDataSource(context, uri)
                    ms =
                        retriever
                            .extractMetadata(android.media.MediaMetadataRetriever.METADATA_KEY_DURATION)
                            ?.toLongOrNull() ?: 0L
                } finally {
                    retriever.release()
                }
            }
            ms
        }

    /** Builds a [Result.Failed] whose [Result.Failed.message] is already resolved from [resId]. */
    private fun failed(
        @StringRes resId: Int,
        vararg args: Any,
    ): Result.Failed = Result.Failed(context.getString(resId, *args), resId, args.toList())

    suspend fun render(
        sceneFactory: SceneFactory,
        aspect: ExportAspect,
        spec: LoopSpec,
        // The look the caller pinned when the render started; each drift stop shifts its params.
        look: LookSnapshot,
        drift: TimeOfDayDrift = TimeOfDayDrift.None,
        paramsAt: ((Long) -> SceneParams)? = null,
        codec: ExportCodec = ExportCodec.H264,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
        // The free tier's corner wordmark; see WatermarkOverlay.
        watermark: Boolean = false,
        // The track the loop is cut from: the export's sound engine hears it.
        audioUri: Uri? = null,
        // How long the long-form file LoopExtend will repeat this reel into, or 0 when unknown.
        longFormMs: Long = 0L,
    ): Result =
        withContext(Dispatchers.Default) {
            val choice =
                negotiate(aspect, spec.fps, codec, longFormMs)
                    ?: return@withContext failed(R.string.export_error_loop_encoder_rejected, aspect.width, aspect.height)
            val budgeted = budgetedSpec(spec.copy(fps = choice.fps), aspect)
            renderStops(
                RenderJob(
                    sceneFactory = sceneFactory,
                    aspect = aspect,
                    choice = choice,
                    spec = budgeted,
                    look = look,
                    drift = drift,
                    paramsAt = paramsAt,
                    watermarkText = if (watermark) context.getString(R.string.pro_watermark_text) else null,
                    audioUri = audioUri,
                ),
                onProgress,
                isCancelled,
            )
        }

    /** Everything one reel render needs, gathered so the stop loop stays readable. */
    private class RenderJob(
        val sceneFactory: SceneFactory,
        val aspect: ExportAspect,
        val choice: EncoderChoice,
        val spec: LoopSpec,
        val look: LookSnapshot,
        val drift: TimeOfDayDrift,
        val paramsAt: ((Long) -> SceneParams)?,
        val watermarkText: String?,
        val audioUri: Uri?,
    )

    private data class EncoderChoice(
        val fps: Int,
        val bitRate: Int,
        val codec: ExportCodec,
    )

    private sealed interface StopOutcome {
        data class Done(
            val crossfadeFrames: Int,
        ) : StopOutcome

        data object Cancelled : StopOutcome
    }

    @Suppress("ReturnCount")
    private fun renderStops(
        job: RenderJob,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result {
        val phases = job.drift.stopPhases()
        val rendered = mutableListOf<RenderedLoop>()
        var seamFrames = job.spec.crossfadeFrames
        var pending: File? = null
        try {
            for ((index, stop) in phases.withIndex()) {
                val file = File.createTempFile("geode_loop_${stop.index}_", ".mp4", context.cacheDir).also { pending = it }
                val outcome =
                    renderStop(
                        job = job,
                        stop = stop,
                        file = file,
                        crossfadeFrames = seamFrames,
                        stopIndex = index,
                        stopCount = phases.size,
                        onProgress = onProgress,
                        isCancelled = isCancelled,
                    )
                val cancelled =
                    when (outcome) {
                        is StopOutcome.Done -> {
                            // A later stop cannot hold more seam frames than an earlier one did,
                            // so the reel reports the shortest seam any of its loops used.
                            seamFrames = minOf(seamFrames, outcome.crossfadeFrames)
                            rendered += RenderedLoop(file, stop)
                            pending = null
                            false
                        }
                        StopOutcome.Cancelled -> true
                    }
                if (cancelled) return discard(rendered, pending, Result.Cancelled)
            }
        } catch (e: MediaCodec.CodecException) {
            RingLog.note(TAG, "loop render hit a codec exception", e)
            return discard(rendered, pending, codecFailure(e))
        } catch (e: IllegalStateException) {
            RingLog.note(TAG, "loop render stopped unexpectedly", e)
            val resolved = e.message?.let { Result.Failed(it) } ?: failed(R.string.export_error_loop_stopped)
            return discard(rendered, pending, resolved)
        } catch (e: IOException) {
            RingLog.note(TAG, "loop render failed to write to cache", e)
            return discard(rendered, pending, failed(R.string.export_error_loop_cache_write, e.message.orEmpty()))
        } catch (e: SeamBuildException) {
            RingLog.note(TAG, "loop render's seam shader failed to compile", e)
            return discard(rendered, pending, failed(R.string.export_error_loop_shader_compile, e.message.orEmpty()))
        }
        onProgress(1f)
        val effective = job.spec.copy(crossfadeMs = GeodeNative.exportLoopReportedCrossfadeMs(seamFrames, job.spec.fps))
        return Result.Rendered(
            LoopReel(
                loops = rendered.toList(),
                spec = effective,
                width = job.aspect.width,
                height = job.aspect.height,
            ),
        )
    }

    private fun renderStop(
        job: RenderJob,
        stop: DriftStop,
        file: File,
        crossfadeFrames: Int,
        stopIndex: Int,
        stopCount: Int,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): StopOutcome {
        var writer: StopWriter? = null
        var egl: EncoderSurface? = null
        var renderer: OffscreenSceneRenderer? = null
        var stash: SeamStash? = null
        var mark: WatermarkOverlay? = null
        try {
            val encoded =
                StopWriter.open(context, file, videoFormat(job.aspect, job.choice), job.choice.codec.mimeType).also { writer = it }
            val surface = EncoderSurface(encoded.surface).also { egl = it }
            surface.makeCurrent()

            val seam = SeamStash(context, job.aspect.width, job.aspect.height).also { stash = it }
            val seamFrames = seam.allocate(crossfadeFrames)
            check(seamFrames > 0) {
                context.getString(R.string.export_error_loop_low_gpu_memory, job.aspect.width, job.aspect.height)
            }
            val loopFrames = job.spec.loopFrames
            val sourceFrames = loopFrames + seamFrames
            val scene = prepareRenderer(job, stop, sourceFrames).also { renderer = it }
            scene.prepare()
            job.watermarkText?.let { text ->
                mark = WatermarkOverlay(text, context.assets).apply { prepare(job.aspect.width, job.aspect.height) }
            }

            val outcome =
                drawFrames(
                    scene = scene,
                    writer = encoded,
                    egl = surface,
                    seam = seam,
                    loopFrames = loopFrames,
                    seamFrames = seamFrames,
                    fps = job.choice.fps,
                    stopIndex = stopIndex,
                    stopCount = stopCount,
                    onProgress = onProgress,
                    isCancelled = isCancelled,
                    mark = mark,
                )
            if (outcome is StopOutcome.Done) encoded.finish()
            return outcome
        } finally {
            bestEffort(TAG, "mark?.release()") { mark?.release() }
            bestEffort(TAG, "renderer?.release()") { renderer?.release() }
            bestEffort(TAG, "stash?.close()") { stash?.close() }
            bestEffort(TAG, "writer?.close()") { writer?.close() }
            bestEffort(TAG, "egl?.release()") { egl?.release() }
        }
    }

    /**
     * Draws one loop plus its tail. Which frames are stashed, blended and presented, and at what
     * time, is the native seam driver's (`core/viz/frame/LoopSeam`): this only presents.
     */
    private fun drawFrames(
        scene: OffscreenSceneRenderer,
        writer: StopWriter,
        egl: EncoderSurface,
        seam: SeamStash,
        loopFrames: Int,
        seamFrames: Int,
        fps: Int,
        stopIndex: Int,
        stopCount: Int,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
        mark: WatermarkOverlay?,
    ): StopOutcome {
        val sourceFrames = loopFrames + seamFrames
        var frame = 0
        var cancelled = false
        while (frame < sourceFrames) {
            if (isCancelled()) {
                cancelled = true
                break
            }
            scene.renderFrame(frame)
            val ptsNs = seam.frame(frame, loopFrames, seamFrames, fps)
            if (ptsNs >= 0) {
                mark?.draw()
                egl.setPresentationTimeNs(ptsNs)
                egl.swapBuffers()
                writer.drain(untilEndOfStream = false)
            }
            onProgress(GeodeNative.exportLoopProgress(stopIndex, stopCount, frame, loopFrames, seamFrames))
            frame++
        }
        return if (cancelled) StopOutcome.Cancelled else StopOutcome.Done(seamFrames)
    }

    private fun prepareRenderer(
        job: RenderJob,
        stop: DriftStop,
        sourceFrames: Int,
    ): OffscreenSceneRenderer {
        // Keyframed parameters drift with the stop they are rendered for, so an automated hue
        // still lands on top of this stop's palette rather than resetting it every repeat.
        val driftedParamsAt: ((Long) -> SceneParams)? =
            job.paramsAt?.let { source -> { timeMs: Long -> stop.applyTo(source(timeMs)) } }
        return OffscreenSceneRenderer(
            context = context,
            sceneFactory = job.sceneFactory,
            spec =
                OffscreenRenderSpec(
                    width = job.aspect.width,
                    height = job.aspect.height,
                    fps = job.choice.fps,
                    totalFrames = sourceFrames,
                    rangeStartMs = 0L,
                    look = job.look.copy(params = stop.applyTo(job.look.params)),
                    paramsAt = driftedParamsAt,
                    audioUri = job.audioUri,
                    // Pinned at prepare: live edits and the player moving on never reach the render.
                    pinDurationMs = sourceFrames * 1000L / job.choice.fps,
                    entitled = ProSnapshot.gate(context).allows(ProFeature.AUTO_DIRECTOR),
                ),
        )
    }

    private fun discard(
        rendered: List<RenderedLoop>,
        pending: File?,
        result: Result,
    ): Result {
        rendered.forEach { it.delete() }
        pending?.let { file -> bestEffort(TAG, "file.delete()") { file.delete() } }
        return result
    }

    /**
     * Settles the encoder format before any stop starts.
     *
     * Every stop's loop ends up in one track of one file, so they must agree on frame rate and
     * codec configuration; discovering a fallback halfway through the reel would mean throwing
     * away the loops already rendered.
     */
    private fun negotiate(
        aspect: ExportAspect,
        fps: Int,
        codec: ExportCodec,
        longFormMs: Long,
    ): EncoderChoice? {
        // The reel is short, but LoopExtend copies its samples into the long-form file once per
        // repeat, so that file's size is this rate times the long form's length. Choosing the rate
        // here is the only point it can still change: past MediaMuxer's 4 GB the extend would fail
        // after the whole reel had rendered.
        val longFormUs = longFormMs * 1000L
        val bitRate = MuxerBudget.cappedVideoBps(aspect.bitRate, longFormUs, MuxerBudget.audioBytes(longFormUs))
        if (bitRate < aspect.bitRate) RingLog.note(TAG, "loop bit rate capped to $bitRate for a $longFormMs ms long form")
        return codec.attempts(aspect, fps, bitRate).map { EncoderChoice(it.fps, it.bitRate, it.codec) }.firstOrNull { accepts(aspect, it) }
    }

    private fun accepts(
        aspect: ExportAspect,
        choice: EncoderChoice,
    ): Boolean {
        var codec: MediaCodec? = null
        try {
            return runCatching {
                codec =
                    MediaCodec.createEncoderByType(choice.codec.mimeType).also {
                        it.configure(videoFormat(aspect, choice), null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                    }
                true
            }.getOrElse {
                Log.w(TAG, "encoder rejected ${aspect.width}x${aspect.height}@${choice.fps}", it)
                false
            }
        } finally {
            bestEffort(TAG, "codec?.release()") { codec?.release() }
        }
    }

    /**
     * Shortens the seam to what the GPU can be expected to hold: the stash's 192 MB budget, one
     * full-resolution frame per crossfade frame (`core/viz/frame/LoopSeam`).
     */
    private fun budgetedSpec(
        spec: LoopSpec,
        aspect: ExportAspect,
    ): LoopSpec {
        val out = LongArray(GeodeNative.LOOP_SPEC_LONGS)
        GeodeNative.loopSpecBudgeted(spec.loopMs, spec.crossfadeMs, spec.fps, aspect.width, aspect.height, out)
        val crossfadeMs = out[GeodeNative.LOOP_SPEC_CROSSFADE_MS]
        return if (crossfadeMs == spec.crossfadeMs) spec else spec.copy(crossfadeMs = crossfadeMs)
    }

    private fun videoFormat(
        aspect: ExportAspect,
        choice: EncoderChoice,
    ): MediaFormat =
        MediaFormat.createVideoFormat(choice.codec.mimeType, aspect.width, aspect.height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, choice.bitRate)
            setInteger(MediaFormat.KEY_FRAME_RATE, choice.fps)
            // A keyframe every second keeps a seam preview cheap to scrub, and guarantees the
            // sample copy in LoopExtend can start every repeat on an IDR.
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
        }

    private fun codecFailure(e: MediaCodec.CodecException): Result.Failed =
        if (e.isRecoverable || e.isTransient) {
            failed(R.string.export_error_loop_encoder_busy)
        } else {
            failed(R.string.export_error_loop_encoder_failed, e.message.orEmpty())
        }

    /**
     * The encoder and muxer for one loop file.
     *
     * The loop is video only. Its soundtrack is laid over the repeats later, once, at full
     * length — encoding audio into a loop that is about to be repeated hundreds of times would
     * mean encoding the same seconds hundreds of times.
     */
    private class StopWriter private constructor(
        private val context: Context,
        private val encoder: MediaCodec,
        private val muxer: MediaMuxer,
    ) : Closeable {
        val surface: Surface = encoder.createInputSurface()

        private val info = MediaCodec.BufferInfo()
        private var track = -1
        private var muxing = false
        private var muxed = false
        private var sawEndOfStream = false
        private var sampleWritten = false

        fun start() {
            encoder.start()
        }

        fun drain(untilEndOfStream: Boolean) {
            var idleAttempts = 0
            var done = false
            while (!done && idleAttempts < FLUSH_ATTEMPT_LIMIT) {
                val index = encoder.dequeueOutputBuffer(info, if (untilEndOfStream) TIMEOUT_US else 0L)
                when {
                    index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> openTrack()
                    index >= 0 -> done = writeSample(index)
                    else -> {
                        done = !untilEndOfStream
                        idleAttempts++
                    }
                }
            }
        }

        fun finish() {
            encoder.signalEndOfInputStream()
            drain(untilEndOfStream = true)
            check(muxing) { context.getString(R.string.export_error_loop_no_output) }
            check(sawEndOfStream) { context.getString(R.string.export_error_loop_stalled) }
            muxer.stop()
            muxed = true
        }

        override fun close() {
            if (muxing && !muxed) {
                if (sampleWritten) {
                    bestEffort(TAG, "muxer.stop()") { muxer.stop() }
                } else {
                    // Same reasoning as VideoExporter's finally block: a muxer that started but never
                    // received a sample (e.g. cancelled before the first frame drained) cannot be
                    // stopped — MediaMuxer.stop() with zero samples always throws.
                    RingLog.note(TAG, "muxer started but wrote no samples; skipping stop() to avoid a guaranteed IllegalStateException")
                }
            }
            bestEffort(TAG, "muxer.release()") { muxer.release() }
            bestEffort(TAG, "encoder.stop()") { encoder.stop() }
            bestEffort(TAG, "encoder.release()") { encoder.release() }
            bestEffort(TAG, "surface.release()") { surface.release() }
        }

        private fun openTrack() {
            track = muxer.addTrack(encoder.outputFormat)
            muxer.start()
            muxing = true
        }

        private fun writeSample(index: Int): Boolean {
            val endOfStream = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
            // A codec-config buffer carries the SPS/PPS the muxer already took from the output
            // format, and an empty buffer carries nothing; neither is a frame worth writing.
            val hasMediaPayload = info.size > 0 && info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0
            if (muxing && track >= 0 && hasMediaPayload) {
                val buffer =
                    checkNotNull(encoder.getOutputBuffer(index)) { context.getString(R.string.export_error_encoder_buffer_null) }
                buffer.position(info.offset)
                buffer.limit(info.offset + info.size)
                muxer.writeSampleData(track, buffer, info)
                sampleWritten = true
            }
            encoder.releaseOutputBuffer(index, false)
            if (endOfStream) sawEndOfStream = true
            return endOfStream
        }

        companion object {
            fun open(
                context: Context,
                file: File,
                format: MediaFormat,
                mimeType: String,
            ): StopWriter {
                var encoder: MediaCodec? = null
                var muxer: MediaMuxer? = null
                val opened =
                    runCatching {
                        val codec =
                            MediaCodec.createEncoderByType(mimeType).also {
                                encoder = it
                                it.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                            }
                        val writer =
                            MediaMuxer(file.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4).also { muxer = it }
                        StopWriter(context, codec, writer).also { it.start() }
                    }
                return opened.getOrElse { failure ->
                    bestEffort(TAG, "muxer?.release()") { muxer?.release() }
                    bestEffort(TAG, "encoder?.release()") { encoder?.release() }
                    throw failure
                }
            }
        }
    }

    /**
     * Holds the loop's opening frames so the closing frames can dissolve into them.
     *
     * A handle over the native seam pass (`core/viz/frame/ExportPasses`, `SeamPass`, #442): the
     * frames are copied straight off the encoder surface into textures and blended back at the
     * dissolve's weight (`exportmath::seam::weight`: none on the first closing frame, all on the
     * last, so the frames either side of the wrap are untouched renders). Nothing crosses to the
     * CPU. One full-resolution texture per crossfade frame, so the stash takes what it can get and
     * reports how much that was.
     */
    private class SeamStash(
        context: Context,
        width: Int,
        height: Int,
    ) : Closeable {
        private var handle = GeodeNative.exportSeamCreate(context.assets, width, height)

        /** Throws [SeamBuildException] when the pass did not build, as the Kotlin shader's compile did. */
        fun allocate(requested: Int): Int {
            val error = if (handle == 0L) "out of memory" else GeodeNative.exportPassError(handle)
            if (error.isNotEmpty()) throw SeamBuildException(error)
            return GeodeNative.exportSeamAllocate(handle, requested)
        }

        /** Stashes or blends source [frame]; its presentation time in ns, or -1 when it is not presented. */
        fun frame(
            frame: Int,
            loopFrames: Int,
            seamFrames: Int,
            fps: Int,
        ): Long = GeodeNative.exportSeamFrame(handle, frame, loopFrames, seamFrames, fps)

        override fun close() {
            val owned = handle
            handle = 0L
            if (owned != 0L) GeodeNative.exportPassDestroy(owned)
        }
    }

    /** The seam pass did not build; reported as the shader-compile failure it replaced. */
    private class SeamBuildException(
        message: String,
    ) : RuntimeException(message)

    private companion object {
        const val TIMEOUT_US = 10_000L
        const val FLUSH_ATTEMPT_LIMIT = 1_000
    }
}

private const val TAG = "LoopRender"
