package dev.geode.export

import android.content.Context
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.opengl.GLES30
import android.util.Log
import android.view.Surface
import dev.geode.analysis.BarTrim
import dev.geode.analysis.FeatureTimeline
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.SceneFactory
import dev.geode.render.offscreen.OffscreenRenderSpec
import dev.geode.render.offscreen.OffscreenSceneRenderer
import dev.geode.render.scene.GlUtil
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

    /** Frames the finished loop contains — what the viewer sees once per repeat. */
    val loopFrames: Int get() = (loopMs * fps / 1000L).toInt().coerceAtLeast(2)

    /** Frames the dissolve spans. Always at least one, never more than half the loop. */
    val crossfadeFrames: Int get() = (crossfadeMs * fps / 1000L).toInt().coerceIn(1, loopFrames / 2)

    /**
     * Frames the renderer actually draws. The extra tail past [loopFrames] is the natural
     * continuation of the loop, which is what the opening frames dissolve out of.
     */
    val sourceFrames: Int get() = loopFrames + crossfadeFrames

    val loopDurationUs: Long get() = loopFrames * 1_000_000L / fps

    /**
     * Where a preview should start playing to land on the seam.
     *
     * The seam is the wrap from the last frame to the first, so a useful preview begins a beat
     * or two before the dissolve starts and runs past the wrap.
     */
    fun seamPreviewStartMs(leadMs: Long = SEAM_PREVIEW_LEAD_MS): Long = (loopMs - crossfadeMs - leadMs).coerceAtLeast(0L)

    companion object {
        const val MIN_LOOP_MS: Long = 30_000
        const val MAX_LOOP_MS: Long = 300_000
        const val MIN_CROSSFADE_MS: Long = 200
        const val MAX_CROSSFADE_MS: Long = 5_000
        const val DEFAULT_CROSSFADE_MS: Long = 1_000
        const val MIN_FPS: Int = 24
        const val MAX_FPS: Int = 60
        const val SEAM_PREVIEW_LEAD_MS: Long = 2_000

        /**
         * Parses loose UI numbers into a spec, clamping instead of failing.
         *
         * When [bpm] is known the loop is trimmed to whole bars, so a repeat lands on a
         * downbeat and the motion stays in phase with the beat it was rendered against.
         */
        fun of(
            loopMs: Long,
            crossfadeMs: Long = DEFAULT_CROSSFADE_MS,
            fps: Int = MAX_FPS,
            bpm: Float = 0f,
        ): LoopSpec {
            val bounded = loopMs.coerceIn(MIN_LOOP_MS, MAX_LOOP_MS)
            val barTrimmed = BarTrim.trimToBars(bounded * 1000L, bpm) / 1000L
            val length = if (barTrimmed >= MIN_LOOP_MS) barTrimmed else bounded
            val fade = crossfadeMs.coerceIn(MIN_CROSSFADE_MS, minOf(MAX_CROSSFADE_MS, length / 2))
            return LoopSpec(length, fade, fps.coerceIn(MIN_FPS, MAX_FPS))
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

    val drifts: Boolean get() = stops > 1 && (hueTurns != 0f || warmth != 0f)

    /**
     * The palette offset for each stop, from none at the start to the full drift at the end.
     * One stop means one render and no drift.
     */
    fun stopPhases(): List<DriftStop> {
        if (!drifts) return listOf(DriftStop(0, 0f, 0f))
        val last = (stops - 1).toFloat()
        return List(stops) { index ->
            val progress = index / last
            DriftStop(index, hueTurns * progress, warmth * progress)
        }
    }

    companion object {
        val None: TimeOfDayDrift = TimeOfDayDrift(0f, 0f, 1)

        const val MAX_STOPS: Int = 8
        const val MIN_STOP_MS: Long = 20 * 60_000

        /**
         * Spreads a drift over [totalMs] at no more than one step every [MIN_STOP_MS].
         *
         * Each stop is a whole extra loop render, so the count stays small: eight stops across a
         * three-hour video cost eight minutes of rendering rather than three hours of it, and a
         * palette step every twenty minutes is already slower than anyone watches for.
         */
        fun over(
            totalMs: Long,
            hueTurns: Float,
            warmth: Float = 0f,
        ): TimeOfDayDrift {
            if (hueTurns == 0f && warmth == 0f) return None
            val stops = (totalMs / MIN_STOP_MS).toInt().coerceIn(1, MAX_STOPS)
            return TimeOfDayDrift(hueTurns, warmth, stops)
        }
    }
}

/** One palette position along the drift, and the parameter change that realises it. */
data class DriftStop(
    val index: Int,
    val hueOffset: Float,
    val warmthOffset: Float,
) {
    fun applyTo(params: SceneParams): SceneParams =
        params.copy(
            colorShift = wrapTurn(params.colorShift + hueOffset),
            temperature = (params.temperature + warmthOffset).coerceIn(-1f, 1f),
        )

    private fun wrapTurn(value: Float): Float {
        val fraction = value - kotlin.math.floor(value)
        return fraction.coerceIn(0f, 1f)
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
    val requestedCrossfadeMs: Long,
    val width: Int,
    val height: Int,
) {
    val loopDurationUs: Long get() = spec.loopDurationUs

    /** True when the seam had to be shortened to fit the frames the GPU would hold. */
    val crossfadeShortened: Boolean get() = spec.crossfadeMs < requestedCrossfadeMs

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
        ) : Result

        data object Cancelled : Result
    }

    suspend fun render(
        timeline: FeatureTimeline,
        sceneFactory: SceneFactory,
        aspect: ExportAspect,
        spec: LoopSpec,
        sceneParams: SceneParams,
        drift: TimeOfDayDrift = TimeOfDayDrift.None,
        loopStartMs: Long = 0L,
        lfoConfigs: List<LfoConfig> = emptyList(),
        adsrConfigs: List<AdsrConfig> = emptyList(),
        reducedMotion: Boolean = false,
        paramsAt: ((Long) -> SceneParams)? = null,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result =
        withContext(Dispatchers.Default) {
            val choice =
                negotiate(aspect, spec.fps)
                    ?: return@withContext Result.Failed(
                        "This device's video encoder would not accept ${aspect.width}×${aspect.height}. " +
                            "Try a smaller size or a different aspect ratio.",
                    )
            val budgeted = budgetedSpec(spec.copy(fps = choice.fps), aspect)
            val start = clampLoopStart(loopStartMs, timeline.durationMs, budgeted.loopMs)
            renderStops(
                RenderJob(
                    timeline = timeline,
                    sceneFactory = sceneFactory,
                    aspect = aspect,
                    choice = choice,
                    spec = budgeted,
                    requestedCrossfadeMs = spec.crossfadeMs,
                    sceneParams = sceneParams,
                    drift = drift,
                    loopStartMs = start,
                    lfoConfigs = lfoConfigs,
                    adsrConfigs = adsrConfigs,
                    reducedMotion = reducedMotion,
                    paramsAt = paramsAt,
                ),
                onProgress,
                isCancelled,
            )
        }

    /** Everything one reel render needs, gathered so the stop loop stays readable. */
    private class RenderJob(
        val timeline: FeatureTimeline,
        val sceneFactory: SceneFactory,
        val aspect: ExportAspect,
        val choice: EncoderChoice,
        val spec: LoopSpec,
        val requestedCrossfadeMs: Long,
        val sceneParams: SceneParams,
        val drift: TimeOfDayDrift,
        val loopStartMs: Long,
        val lfoConfigs: List<LfoConfig>,
        val adsrConfigs: List<AdsrConfig>,
        val reducedMotion: Boolean,
        val paramsAt: ((Long) -> SceneParams)?,
    )

    private data class EncoderChoice(
        val fps: Int,
        val bitRate: Int,
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
                        progressBase = index / phases.size.toFloat(),
                        progressSpan = 1f / phases.size,
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
            return discard(rendered, pending, Result.Failed(codecMessage(e)))
        } catch (e: IllegalStateException) {
            return discard(rendered, pending, Result.Failed(e.message ?: "The loop render stopped unexpectedly."))
        } catch (e: IOException) {
            return discard(rendered, pending, Result.Failed("The loop could not be written to this device's cache: ${e.message}"))
        } catch (e: GlUtil.ShaderCompileException) {
            return discard(rendered, pending, Result.Failed("The seam blend could not be compiled on this GPU: ${e.message}"))
        }
        onProgress(1f)
        val effective = job.spec.copy(crossfadeMs = seamFrames * 1000L / job.spec.fps)
        return Result.Rendered(
            LoopReel(
                loops = rendered.toList(),
                spec = effective,
                requestedCrossfadeMs = job.requestedCrossfadeMs,
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
        progressBase: Float,
        progressSpan: Float,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): StopOutcome {
        var writer: StopWriter? = null
        var egl: EncoderSurface? = null
        var renderer: OffscreenSceneRenderer? = null
        var stash: SeamStash? = null
        try {
            val encoded = StopWriter.open(file, videoFormat(job.aspect, job.choice)).also { writer = it }
            val surface = EncoderSurface(encoded.surface).also { egl = it }
            surface.makeCurrent()

            val seam = SeamStash(job.aspect.width, job.aspect.height).also { stash = it }
            val seamFrames = seam.allocate(crossfadeFrames)
            check(seamFrames > 0) {
                "There is not enough graphics memory to hold even one crossfade frame at " +
                    "${job.aspect.width}×${job.aspect.height} — render the loop at a smaller size."
            }
            val loopFrames = job.spec.loopFrames
            val sourceFrames = loopFrames + seamFrames
            val scene = prepareRenderer(job, stop, sourceFrames).also { renderer = it }
            scene.prepare()

            val outcome =
                drawFrames(
                    scene = scene,
                    writer = encoded,
                    egl = surface,
                    seam = seam,
                    loopFrames = loopFrames,
                    seamFrames = seamFrames,
                    fps = job.choice.fps,
                    progressBase = progressBase,
                    progressSpan = progressSpan,
                    onProgress = onProgress,
                    isCancelled = isCancelled,
                )
            if (outcome is StopOutcome.Done) encoded.finish()
            return outcome
        } finally {
            bestEffort(TAG, "renderer?.release()") { renderer?.release() }
            bestEffort(TAG, "stash?.close()") { stash?.close() }
            bestEffort(TAG, "writer?.close()") { writer?.close() }
            bestEffort(TAG, "egl?.release()") { egl?.release() }
        }
    }

    /**
     * Draws one loop plus its tail.
     *
     * The opening [seamFrames] frames are rendered but not presented: they are copied into the
     * stash and blended back over the closing frames, so the loop's last frame dissolves into
     * what the viewer is about to see again. Output frame `j` is source frame `j + seamFrames`,
     * which keeps the presented timestamps monotonic and the encoder in a single pass.
     */
    private fun drawFrames(
        scene: OffscreenSceneRenderer,
        writer: StopWriter,
        egl: EncoderSurface,
        seam: SeamStash,
        loopFrames: Int,
        seamFrames: Int,
        fps: Int,
        progressBase: Float,
        progressSpan: Float,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): StopOutcome {
        val sourceFrames = loopFrames + seamFrames
        val frameDurationNs = 1_000_000_000L / fps
        var frame = 0
        var cancelled = false
        while (frame < sourceFrames) {
            if (isCancelled()) {
                cancelled = true
                break
            }
            scene.renderFrame(frame)
            if (frame < seamFrames) {
                seam.capture(frame)
            } else {
                val tail = frame - loopFrames
                if (tail >= 0) seam.blendOver(tail, seamWeight(tail, seamFrames))
                egl.setPresentationTimeNs((frame - seamFrames) * frameDurationNs)
                egl.swapBuffers()
                writer.drain(untilEndOfStream = false)
            }
            onProgress(progressBase + progressSpan * (frame + 1) / sourceFrames)
            frame++
        }
        return if (cancelled) StopOutcome.Cancelled else StopOutcome.Done(seamFrames)
    }

    /**
     * The dissolve's weight on the stashed opening frame.
     *
     * It reaches the ends exactly — no weight on the first closing frame, all of it on the last —
     * so the frames either side of the wrap are untouched renders rather than mixtures, and the
     * seam has nothing left to give itself away with.
     */
    private fun seamWeight(
        tail: Int,
        seamFrames: Int,
    ): Float = if (seamFrames > 1) tail / (seamFrames - 1f) else 1f

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
            timeline = job.timeline,
            spec =
                OffscreenRenderSpec(
                    width = job.aspect.width,
                    height = job.aspect.height,
                    fps = job.choice.fps,
                    totalFrames = sourceFrames,
                    rangeStartMs = job.loopStartMs,
                    baseParams = stop.applyTo(job.sceneParams),
                    lfoConfigs = job.lfoConfigs,
                    adsrConfigs = job.adsrConfigs,
                    reducedMotion = job.reducedMotion,
                    paramsAt = driftedParamsAt,
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
    ): EncoderChoice? {
        val requested = EncoderChoice(fps.coerceIn(LoopSpec.MIN_FPS, LoopSpec.MAX_FPS), aspect.bitRate)
        val fallback = EncoderChoice(FALLBACK_FPS, aspect.bitRate * 2 / 3)
        return listOf(requested, fallback).firstOrNull { accepts(aspect, it) }
    }

    private fun accepts(
        aspect: ExportAspect,
        choice: EncoderChoice,
    ): Boolean {
        var codec: MediaCodec? = null
        try {
            return runCatching {
                codec =
                    MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC).also {
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
     * Shortens the seam to what the GPU can be expected to hold.
     *
     * The stash keeps one full-resolution frame per crossfade frame, so the affordable seam
     * length falls as the render size rises: a second of 4K costs four times what a second of
     * 1080p does.
     */
    private fun budgetedSpec(
        spec: LoopSpec,
        aspect: ExportAspect,
    ): LoopSpec {
        val perFrame = aspect.width.toLong() * aspect.height * BYTES_PER_PIXEL
        val affordable = (SEAM_STASH_BUDGET_BYTES / perFrame).toInt().coerceAtLeast(1)
        if (spec.crossfadeFrames <= affordable) return spec
        val shortened = (affordable * 1000L / spec.fps).coerceAtLeast(1L)
        return spec.copy(crossfadeMs = shortened)
    }

    private fun clampLoopStart(
        loopStartMs: Long,
        trackDurationMs: Long,
        loopMs: Long,
    ): Long {
        val latest = (trackDurationMs - loopMs).coerceAtLeast(0L)
        return loopStartMs.coerceIn(0L, latest)
    }

    private fun videoFormat(
        aspect: ExportAspect,
        choice: EncoderChoice,
    ): MediaFormat =
        MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, aspect.width, aspect.height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, choice.bitRate)
            setInteger(MediaFormat.KEY_FRAME_RATE, choice.fps)
            // A keyframe every second keeps a seam preview cheap to scrub, and guarantees the
            // sample copy in LoopExtend can start every repeat on an IDR.
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
        }

    private fun codecMessage(e: MediaCodec.CodecException): String =
        if (e.isRecoverable || e.isTransient) {
            "The video encoder was busy — close other apps that are recording or playing video and try again."
        } else {
            "This device's video encoder failed while rendering the loop: ${e.message}"
        }

    /**
     * The encoder and muxer for one loop file.
     *
     * The loop is video only. Its soundtrack is laid over the repeats later, once, at full
     * length — encoding audio into a loop that is about to be repeated hundreds of times would
     * mean encoding the same seconds hundreds of times.
     */
    private class StopWriter private constructor(
        private val encoder: MediaCodec,
        private val muxer: MediaMuxer,
    ) : Closeable {
        val surface: Surface = encoder.createInputSurface()

        private val info = MediaCodec.BufferInfo()
        private var track = -1
        private var muxing = false
        private var muxed = false
        private var sawEndOfStream = false

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
            check(muxing) { "The video encoder produced no output for this loop." }
            check(sawEndOfStream) { "The video encoder stalled while finishing the loop." }
            muxer.stop()
            muxed = true
        }

        override fun close() {
            if (muxing && !muxed) bestEffort(TAG, "muxer.stop()") { muxer.stop() }
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
                val buffer = checkNotNull(encoder.getOutputBuffer(index)) { "video encoder buffer null (codec error state)" }
                buffer.position(info.offset)
                buffer.limit(info.offset + info.size)
                muxer.writeSampleData(track, buffer, info)
            }
            encoder.releaseOutputBuffer(index, false)
            if (endOfStream) sawEndOfStream = true
            return endOfStream
        }

        companion object {
            fun open(
                file: File,
                format: MediaFormat,
            ): StopWriter {
                var encoder: MediaCodec? = null
                var muxer: MediaMuxer? = null
                val opened =
                    runCatching {
                        val codec =
                            MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC).also {
                                encoder = it
                                it.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                            }
                        val writer =
                            MediaMuxer(file.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4).also { muxer = it }
                        StopWriter(codec, writer).also { it.start() }
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
     * The frames are copied straight off the encoder surface into textures and blended back with
     * a constant alpha, which keeps the whole crossfade on the GPU: no pixel ever crosses to the
     * CPU, and nothing is decoded or re-encoded. The cost is one full-resolution texture per
     * crossfade frame, so the stash takes what it can get and reports how much that was.
     */
    private class SeamStash(
        private val width: Int,
        private val height: Int,
    ) : Closeable {
        private val triangle = GlUtil.FullscreenTriangle()
        private var program = 0
        private var textureLocation = -1
        private var alphaLocation = -1
        private var textures = IntArray(0)

        fun allocate(requested: Int): Int {
            program = GlUtil.buildProgram(VERTEX_SHADER, FRAGMENT_SHADER)
            triangle.create()
            textureLocation = GLES30.glGetUniformLocation(program, "uTex")
            alphaLocation = GLES30.glGetUniformLocation(program, "uAlpha")
            drainErrors()
            val ids = IntArray(requested)
            GLES30.glGenTextures(requested, ids, 0)
            var allocated = 0
            while (allocated < requested && allocateOne(ids[allocated])) {
                allocated++
            }
            if (allocated < requested) GLES30.glDeleteTextures(requested - allocated, ids, allocated)
            textures = ids.copyOf(allocated)
            return allocated
        }

        fun capture(index: Int) {
            GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, 0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, textures[index])
            GLES30.glCopyTexSubImage2D(GLES30.GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        }

        /**
         * Mixes stashed frame [index] over what the scene just drew, at weight [alpha].
         *
         * Source-alpha blending of a constant alpha is exactly a cross-dissolve, so the closing
         * frames walk from the natural continuation of the loop to its opening frames.
         */
        fun blendOver(
            index: Int,
            alpha: Float,
        ) {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            GLES30.glViewport(0, 0, width, height)
            GLES30.glUseProgram(program)
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, textures[index])
            GLES30.glUniform1i(textureLocation, 0)
            GLES30.glUniform1f(alphaLocation, alpha.coerceIn(0f, 1f))
            GLES30.glEnable(GLES30.GL_BLEND)
            GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA)
            triangle.draw()
            GLES30.glDisable(GLES30.GL_BLEND)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        }

        override fun close() {
            if (textures.isNotEmpty()) GLES30.glDeleteTextures(textures.size, textures, 0)
            if (program != 0) GLES30.glDeleteProgram(program)
            triangle.release()
            textures = IntArray(0)
            program = 0
        }

        private fun allocateOne(id: Int): Boolean {
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, id)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
            GLES30.glTexStorage2D(GLES30.GL_TEXTURE_2D, 1, GLES30.GL_RGBA8, width, height)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
            // A driver that cannot find room says so here rather than by crashing later, so the
            // seam is shortened to whatever did fit.
            return GLES30.glGetError() == GLES30.GL_NO_ERROR
        }

        private fun drainErrors() {
            var guard = 0
            while (GLES30.glGetError() != GLES30.GL_NO_ERROR && guard < ERROR_DRAIN_LIMIT) {
                guard++
            }
        }

        private companion object {
            const val ERROR_DRAIN_LIMIT = 32

            val VERTEX_SHADER =
                """
                #version 300 es
                layout(location = 0) in vec2 aPos;
                out vec2 vUv;
                void main() {
                    vUv = aPos * 0.5 + 0.5;
                    gl_Position = vec4(aPos, 0.0, 1.0);
                }
                """.trimIndent()

            val FRAGMENT_SHADER =
                """
                #version 300 es
                precision mediump float;
                uniform sampler2D uTex;
                uniform float uAlpha;
                in vec2 vUv;
                out vec4 fragColor;
                void main() {
                    fragColor = vec4(texture(uTex, vUv).rgb, uAlpha);
                }
                """.trimIndent()
        }
    }

    private companion object {
        const val FALLBACK_FPS = 30
        const val TIMEOUT_US = 10_000L
        const val FLUSH_ATTEMPT_LIMIT = 1_000
        const val BYTES_PER_PIXEL = 4L

        /**
         * How much graphics memory the seam stash may claim. Generous enough for a second of
         * 1080p, small enough to leave the scene's own targets room on a mid-range phone.
         */
        const val SEAM_STASH_BUDGET_BYTES = 192L * 1024 * 1024
    }
}

private const val TAG = "LoopRender"
