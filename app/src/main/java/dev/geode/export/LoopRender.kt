package dev.geode.export

import android.content.Context
import android.content.res.AssetFileDescriptor
import android.net.Uri
import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.RingLog
import dev.geode.billing.ProSnapshot
import dev.geode.data.LookSnapshot
import dev.geode.engine.bridge.GeodeNative
import dev.geode.model.WatermarkChoice
import dev.geode.pro.ProFeature
import dev.geode.util.bestEffort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileNotFoundException

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
    /** Native (`core/output/LoopPlan`, tested by `core/tests/output_test.cpp`). */
    val loopDurationUs: Long by lazy {
        val out = LongArray(GeodeNative.LOOP_SPEC_LONGS)
        GeodeNative.loopSpecDerive(loopMs, crossfadeMs, fps, out)
        out[GeodeNative.LOOP_SPEC_LOOP_DURATION_US]
    }

    companion object {
        // The ranges the controls offer are native's (`core/output/LoopPlan`).
        private val limits: LongArray by lazy { GeodeNative.loopLimits() }
        val minLoopMs: Long get() = limits[GeodeNative.LOOP_LIMIT_MIN_LOOP_MS]
        val maxLoopMs: Long get() = limits[GeodeNative.LOOP_LIMIT_MAX_LOOP_MS]
        val minCrossfadeMs: Long get() = limits[GeodeNative.LOOP_LIMIT_MIN_CROSSFADE_MS]
        val maxCrossfadeMs: Long get() = limits[GeodeNative.LOOP_LIMIT_MAX_CROSSFADE_MS]
        val defaultCrossfadeMs: Long get() = limits[GeodeNative.LOOP_LIMIT_DEFAULT_CROSSFADE_MS]
        val maxDriftStops: Int get() = limits[GeodeNative.LOOP_LIMIT_MAX_DRIFT_STOPS].toInt()

        /** Parses loose UI numbers into a spec, clamping instead of failing (natively, `core/output/LoopPlan`). */
        fun of(
            loopMs: Long,
            crossfadeMs: Long,
            fps: Int,
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
    companion object {
        val None: TimeOfDayDrift = TimeOfDayDrift(0f, 0f, 1)
    }
}

/** One rendered loop: a self-contained, video-only MP4 that starts on a keyframe. */
class RenderedLoop(
    val file: File,
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
 * The renderer draws the loop's source frames — one loop plus a tail — and dissolves the
 * tail into the stashed opening frames, so the last frame of the encoded loop flows into the
 * first exactly as two consecutive frames would. Repeating that loop is then seamless without
 * anything downstream having to touch pixels again.
 *
 * The render itself is one native job (`core/output/LoopRender`): the encoder it settles on, each
 * drift stop's frames, seam and presentation times, and the scratch file each stop is written to.
 * This hands the job its values, polls it, passes a cancel on and words its outcome.
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
        codec: ExportCodec = ExportCodec.H264,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
        // The wordmark, as the render pinned it at its start; see WatermarkOverlay.
        watermark: WatermarkChoice = WatermarkChoice.NONE,
        // The track the loop is cut from: the export's sound engine hears it.
        audioUri: Uri,
        // How long the long-form file LoopExtend will repeat this reel into, or 0 when unknown.
        longFormMs: Long = 0L,
    ): Result =
        withContext(Dispatchers.Default) {
            val job = GeodeNative.loopJobCreate()
            if (job == 0L) return@withContext failed(R.string.export_error_loop_stopped)
            try {
                setOutput(job, aspect, spec, drift, codec, longFormMs)
                setScene(job, sceneFactory, look)
                if (!setTrack(job, audioUri)) return@withContext Result.Failed("the track cannot be opened")
                if (!setMark(job, watermark, aspect)) return@withContext Result.Failed("watermark pass failed: out of memory")
                coroutineScope {
                    val poll =
                        launch {
                            try {
                                while (isActive) {
                                    if (isCancelled()) GeodeNative.loopJobCancel(job)
                                    onProgress(GeodeNative.loopJobProgress(job))
                                    delay(POLL_MS)
                                }
                            } finally {
                                // A cancel of the caller ends this poll while the run below still blocks its
                                // thread: only this stops the job. After the run has ended it changes nothing.
                                GeodeNative.loopJobCancel(job)
                            }
                        }
                    // Blocks this worker thread for the whole render; the poll above runs on another.
                    GeodeNative.loopJobRun(job)
                    poll.cancelAndJoin()
                }
                resultOf(job, aspect, spec).also { if (it is Result.Rendered) onProgress(1f) }
            } finally {
                GeodeNative.loopJobDestroy(job)
            }
        }

    private fun setOutput(
        job: Long,
        aspect: ExportAspect,
        spec: LoopSpec,
        drift: TimeOfDayDrift,
        codec: ExportCodec,
        longFormMs: Long,
    ) {
        GeodeNative.loopJobSetOutput(
            job,
            context.cacheDir.absolutePath,
            aspect.width,
            aspect.height,
            aspect.bitRate,
            spec.loopMs,
            spec.crossfadeMs,
            spec.fps,
            longFormMs,
        )
        GeodeNative.loopJobSetDrift(job, drift.hueTurns, drift.warmth, drift.stops)
        codec.candidates().forEach { GeodeNative.loopJobAddCodec(job, it.mimeType, it.topFps(aspect)) }
    }

    /** The renderer's inputs and the pinned look, each part in the frame the live renderer takes it in. */
    private fun setScene(
        job: Long,
        sceneFactory: SceneFactory,
        look: LookSnapshot,
    ) {
        GeodeNative.loopJobSetScene(
            job,
            context.assets,
            File(context.filesDir, "milk/textures").absolutePath,
            sceneFactory.sceneId,
            sceneFactory.milkPresetPath,
            ProSnapshot.gate(context).allows(ProFeature.AUTO_DIRECTOR),
        )
        GeodeNative.loopJobSetParams(job, look.params.frame())
        GeodeNative.loopJobSetLfos(job, look.lfos)
        GeodeNative.loopJobSetAdsrs(job, look.adsrs)
        GeodeNative.loopJobSetSoundTuning(job, look.soundTuning?.toFloatArray())
        GeodeNative.loopJobSetReactions(job, look.reactions)
        GeodeNative.loopJobSetMotion(job, look.reducedMotion, look.transitionId, look.transitionMs)
        val u = look.underlay
        GeodeNative.loopJobSetUnderlay(job, u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
    }

    /** Hands the track's descriptor to the job, which closes it; false when the track cannot be opened. */
    private fun setTrack(
        job: Long,
        audioUri: Uri,
    ): Boolean {
        val afd =
            try {
                context.contentResolver.openAssetFileDescriptor(audioUri, "r")
            } catch (e: FileNotFoundException) {
                RingLog.note(TAG, "the loop's track is gone", e)
                null
            }
        if (afd == null) return false
        val length = afd.declaredLength
        val offset = if (length == AssetFileDescriptor.UNKNOWN_LENGTH) 0L else afd.startOffset
        GeodeNative.loopJobSetTrack(job, afd.parcelFileDescriptor.detachFd(), offset, length)
        return true
    }

    /** The wordmark's tile, when these frames carry one; false when it could not be handed over, so a free render never ships unmarked. */
    private fun setMark(
        job: Long,
        watermark: WatermarkChoice,
        aspect: ExportAspect,
    ): Boolean {
        val tile = WatermarkOverlay(watermark, context, WatermarkChoice.USE_LOOP).tile(aspect.width, aspect.height) ?: return true
        return GeodeNative.loopJobSetMark(job, tile.pixels, tile.width, tile.height, tile.placement, tile.corner)
    }

    /** What the finished job says, in the shell's words. */
    private fun resultOf(
        job: Long,
        aspect: ExportAspect,
        spec: LoopSpec,
    ): Result {
        val detail = GeodeNative.loopJobDetail(job).orEmpty()
        val state = GeodeNative.loopJobState(job)
        if (state != GeodeNative.LOOP_JOB_RENDERED && state != GeodeNative.LOOP_JOB_CANCELLED) {
            RingLog.note(TAG, "loop render failed (${GeodeNative.loopJobFailure(job)}): $detail")
        }
        return when (state) {
            GeodeNative.LOOP_JOB_RENDERED -> {
                val files = List(GeodeNative.loopJobFileCount(job)) { RenderedLoop(File(GeodeNative.loopJobFile(job, it).orEmpty())) }
                val kept = spec.copy(crossfadeMs = GeodeNative.loopJobCrossfadeMs(job), fps = GeodeNative.loopJobFps(job))
                Result.Rendered(LoopReel(loops = files, spec = kept, width = aspect.width, height = aspect.height))
            }
            GeodeNative.LOOP_JOB_CANCELLED -> Result.Cancelled
            else ->
                when (GeodeNative.loopJobFailure(job)) {
                    GeodeNative.LOOP_FAILURE_ENCODER_REJECTED ->
                        failed(R.string.export_error_loop_encoder_rejected, aspect.width, aspect.height)
                    GeodeNative.LOOP_FAILURE_SHADER_COMPILE -> failed(R.string.export_error_loop_shader_compile, detail)
                    GeodeNative.LOOP_FAILURE_LOW_GPU_MEMORY ->
                        failed(R.string.export_error_loop_low_gpu_memory, aspect.width, aspect.height)
                    GeodeNative.LOOP_FAILURE_NO_OUTPUT -> failed(R.string.export_error_loop_no_output)
                    GeodeNative.LOOP_FAILURE_STALLED -> failed(R.string.export_error_loop_stalled)
                    GeodeNative.LOOP_FAILURE_ENCODER_FAILED -> failed(R.string.export_error_loop_encoder_failed, detail)
                    GeodeNative.LOOP_FAILURE_CACHE_WRITE -> failed(R.string.export_error_loop_cache_write, detail)
                    else -> if (detail.isEmpty()) failed(R.string.export_error_loop_stopped) else Result.Failed(detail)
                }
        }
    }

    private companion object {
        /** How often the job is asked for its progress and told of a cancel. */
        const val POLL_MS = 50L
    }
}

private const val TAG = "LoopRender"
