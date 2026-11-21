package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import dev.geode.R
import dev.geode.data.pendingRows
import dev.geode.data.publishPending
import dev.geode.engine.bridge.ExportNative
import dev.geode.engine.bridge.LoopMuxNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.IOException

/** One audio clip of a long-form soundtrack, in the order the user dropped it; [title] names it in a refusal. */
data class MixClip(
    val uri: Uri,
    val title: String,
)

/** The soundtrack a long-form video is built around. Its length is the video's length. */
sealed interface LongFormAudio {
    val clips: List<MixClip>

    data class SingleTrack(
        val clip: MixClip,
    ) : LongFormAudio {
        override val clips: List<MixClip> get() = listOf(clip)
    }

    /** Several clips, played back to back in the order the user dropped them. */
    data class Mix(
        override val clips: List<MixClip>,
    ) : LongFormAudio {
        init {
            require(clips.isNotEmpty()) { "a mix needs at least one clip" }
        }
    }
}

/** One repeat of one loop, and where its first frame lands in the finished file. */
data class LoopWrite(
    val loopIndex: Int,
    val offsetUs: Long,
)

/**
 * How the rendered loops fill the running time.
 *
 * Nothing here re-renders or re-encodes: the plan is a list of offsets, and each offset is
 * applied to the already-encoded samples as they are copied into the long file. The plan itself is
 * native (`core/output/LoopPlan`, tested by `core/tests/output_test.cpp`): ceil(total / loop)
 * repeats, spread over the drift stops as evenly as whole repeats allow.
 */
class LoopExtendPlan private constructor(
    val loopDurationUs: Long,
    val totalDurationUs: Long,
    val writes: List<LoopWrite>,
) {
    val repeats: Int get() = writes.size

    /**
     * What the finished file will weigh: every repeat writes the loop's samples again, plus the
     * sample tables, which for a multi-hour file are megabytes of index in their own right.
     */
    fun estimatedBytes(
        loopBytes: Long,
        soundtrackBytes: Long,
    ): Long = ExportNative.loopEstimatedBytes(loopDurationUs, totalDurationUs, loopBytes, soundtrackBytes)

    /** The video bit rate a render would need to keep a file of this length under [limitBytes]. */
    fun fittingBitRate(
        limitBytes: Long,
        soundtrackBytes: Long,
    ): Int = ExportNative.loopFittingBitRate(totalDurationUs, limitBytes, soundtrackBytes)

    companion object {
        fun of(
            loopDurationUs: Long,
            totalDurationUs: Long,
            loops: Int,
        ): LoopExtendPlan {
            require(loopDurationUs > 0) { "loopDurationUs must be positive, was $loopDurationUs" }
            require(loops >= 1) { "a plan needs at least one loop, was $loops" }
            val total = totalDurationUs.coerceAtLeast(1L)
            val pairs = ExportNative.loopExtendPlan(loopDurationUs, total, loops)
            val writes = List(pairs.size / 2) { LoopWrite(loopIndex = pairs[it * 2 + 1].toInt(), offsetUs = pairs[it * 2]) }
            return LoopExtendPlan(loopDurationUs, total, writes)
        }
    }
}

/**
 * Builds the long-form video by repeating an already-encoded loop.
 *
 * The copy is native (`core/output/LoopMux`, tested by `core/tests/loop_mux_test.cpp`): each loop's video samples
 * are read back with AMediaExtractor and written straight into the output with AMediaMuxer, once per repeat, with
 * the presentation timestamps shifted by whole loop lengths and the soundtrack interleaved. No frame is decoded,
 * blended or re-encoded — a ten-hour file costs the same render as a one-minute one, and only the muxing grows.
 * This owns the descriptors, the MediaStore row, the cancel and progress between slices, and the words a failure
 * is shown with.
 */
class LoopExtend(
    private val context: Context,
) {
    sealed interface Result {
        data class Saved(
            val uri: Uri,
            val durationMs: Long,
            val plan: LoopExtendPlan,
        ) : Result

        data class Failed(
            val message: String,
        ) : Result

        data object Cancelled : Result
    }

    /**
     * Lays [audio] over repeats of [reel] and saves the result.
     *
     * The caller keeps ownership of [reel]: the same loops can be extended again to a different
     * length or soundtrack without rendering anything a second time.
     */
    @Suppress("ReturnCount")
    suspend fun extend(
        reel: LoopReel,
        audio: LongFormAudio,
        fileName: String,
        destination: Uri? = null,
        sizeLimitBytes: Long = MUXER_SAFE_BYTES,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result =
        withContext(Dispatchers.Default) {
            if (reel.loops.isEmpty()) return@withContext Result.Failed(context.getString(R.string.export_error_extend_no_loop))
            val mux = LoopMuxNative.loopMuxCreate()
            if (mux == 0L) return@withContext Result.Failed(context.getString(R.string.export_error_extend_assemble))
            try {
                val soundtrack =
                    when (val built = transcode(audio, mux, onProgress, isCancelled)) {
                        is AudioBuild.Ready -> built.soundtrack
                        is AudioBuild.Failed -> return@withContext Result.Failed(built.message)
                    }
                try {
                    write(mux, reel, soundtrack, fileName, destination, sizeLimitBytes, onProgress, isCancelled)
                } finally {
                    soundtrack.release()
                }
            } finally {
                LoopMuxNative.loopMuxDestroy(mux)
            }
        }

    private sealed interface AudioBuild {
        data class Ready(
            val soundtrack: Soundtrack,
        ) : AudioBuild

        data class Failed(
            val message: String,
        ) : AudioBuild
    }

    private sealed interface Target {
        class Opened(
            val uri: Uri,
            val descriptor: ParcelFileDescriptor,
            val viaMediaStore: Boolean,
        ) : Target

        class Failed(
            val message: String,
        ) : Target
    }

    @Suppress("ReturnCount", "TooGenericExceptionCaught")
    private suspend fun write(
        mux: Long,
        reel: LoopReel,
        soundtrack: Soundtrack,
        fileName: String,
        destination: Uri?,
        sizeLimitBytes: Long,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result {
        val totalUs = LoopMuxNative.loopMuxTotalUs(mux)
        if (totalUs <= 0L) return Result.Failed(context.getString(R.string.export_error_extend_empty_soundtrack))
        val plan = LoopExtendPlan.of(reel.loopDurationUs, totalUs, reel.loops.size)
        val estimate = plan.estimatedBytes(reel.bytes / reel.loops.size, soundtrack.bytes)
        if (estimate > sizeLimitBytes) return Result.Failed(oversizeMessage(plan, estimate, sizeLimitBytes, soundtrack.bytes))
        val target =
            when (val opened = openTarget(fileName, destination)) {
                is Target.Opened -> opened
                is Target.Failed -> return Result.Failed(opened.message)
            }
        return try {
            reel.loops.forEach { LoopMuxNative.loopMuxAddLoop(mux, it.file.absolutePath) }
            val ended = target.descriptor.use { copy(it, mux, reel.loopDurationUs, onProgress, isCancelled) }
            if (ended == null) {
                finish(target, plan)
            } else {
                discard(target)
                ended
            }
        } catch (e: CancellationException) {
            discard(target)
            throw e
        } catch (e: Exception) {
            discard(target)
            Result.Failed(context.getString(R.string.export_error_extend_write, e.message))
        }
    }

    private fun finish(
        target: Target.Opened,
        plan: LoopExtendPlan,
    ): Result =
        if (publish(target)) {
            Result.Saved(uri = target.uri, durationMs = plan.totalDurationUs / 1000, plan = plan)
        } else {
            Result.Failed(context.getString(R.string.export_error_library_publish))
        }

    /**
     * Runs the native copy on this thread while a poll passes a cancel on and reports its progress. Null when the
     * file is finished, else how it ended: cancelled by the caller, or failed in the shell's words.
     */
    private suspend fun copy(
        pfd: ParcelFileDescriptor,
        mux: Long,
        loopDurationUs: Long,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result? {
        coroutineScope {
            val poll =
                launch {
                    try {
                        while (isActive) {
                            if (isCancelled()) LoopMuxNative.loopMuxCancel(mux)
                            onProgress(LoopMuxNative.loopMuxProgress(mux))
                            delay(POLL_MS)
                        }
                    } finally {
                        // A cancel of the caller ends this poll while the run below still blocks its thread:
                        // only this stops the copy. After the run has ended it changes nothing.
                        LoopMuxNative.loopMuxCancel(mux)
                    }
                }
            LoopMuxNative.loopMuxRun(mux, pfd.fd, loopDurationUs)
            poll.cancelAndJoin()
        }
        return when (LoopMuxNative.loopMuxState(mux)) {
            LoopMuxNative.MUX_DONE -> {
                onProgress(1f)
                null
            }
            LoopMuxNative.MUX_CANCELLED -> Result.Cancelled
            else -> muxFailure(mux)
        }
    }

    /** The loops' own words when they cannot make one file; else the read or the write that went wrong. */
    private fun muxFailure(mux: Long): Result.Failed {
        val detail = LoopMuxNative.loopMuxDetail(mux).orEmpty()
        return Result.Failed(
            when (LoopMuxNative.loopMuxFailure(mux)) {
                LoopMuxNative.FAILURE_UNUSABLE -> detail.ifEmpty { context.getString(R.string.export_error_extend_assemble) }
                LoopMuxNative.FAILURE_READ -> context.getString(R.string.export_error_extend_copy, detail)
                else -> context.getString(R.string.export_error_extend_write, detail)
            },
        )
    }

    /**
     * Encodes each clip to AAC (native) and hands it to the copy, which reads its samples and format straight out of
     * the finished job. The clips become one AAC track, so they must agree on sample rate and channel count — a
     * track carries one codec configuration. Every clip is brought to [ExportNative.mixRateHz], so in practice the
     * copy refuses a mono clip among stereo ones: rejecting the mix up front beats writing an hour of audio that
     * plays at the wrong speed.
     */
    private fun transcode(
        audio: LongFormAudio,
        mux: Long,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): AudioBuild {
        val clips = audio.clips
        val transcoder = AudioTranscoder(context)
        val done = mutableListOf<TranscodedClip>()
        var odd: MixClip? = null
        val transcoded =
            runCatching {
                for ((index, clip) in clips.withIndex()) {
                    var taken = LoopMuxNative.CLIP_UNREADABLE
                    val result =
                        transcoder.transcode(
                            uri = clip.uri,
                            maxDurationMs = 0L,
                            startMs = 0L,
                            // One AAC track carries one rate, so every clip is brought to the same one.
                            outputRateHz = ExportNative.mixRateHz(),
                            isCancelled = isCancelled,
                            handOver = { aacJob, file -> taken = LoopMuxNative.loopMuxAddClip(mux, aacJob, file.absolutePath) },
                        ) { progress ->
                            onProgress(ExportNative.loopTranscodeProgress(index, progress, clips.size))
                        }
                    done += TranscodedClip(result)
                    check(taken != LoopMuxNative.CLIP_UNREADABLE) { "the soundtrack's track cannot be read back" }
                    if (taken == LoopMuxNative.CLIP_DIFFERS) {
                        odd = clip
                        break
                    }
                }
            }
        val failure = transcoded.exceptionOrNull()
        val message =
            failure?.let { audioMessage(it) } ?: odd?.let {
                "\"${it.title}\" is recorded at a different sample rate or channel count from the first " +
                    "track in this mix. Convert the tracks to match, or export them as separate videos."
            }
        if (message == null) return AudioBuild.Ready(Soundtrack(done))
        done.forEach { it.audio.release() }
        if (failure is CancellationException) throw failure
        return AudioBuild.Failed(message)
    }

    private fun audioMessage(failure: Throwable): String =
        when (failure) {
            is IllegalArgumentException -> context.getString(R.string.export_error_extend_track_no_audio, failure.message)
            is IOException -> context.getString(R.string.export_error_extend_track_unreadable)
            else -> failure.message ?: context.getString(R.string.export_error_extend_soundtrack)
        }

    private fun oversizeMessage(
        plan: LoopExtendPlan,
        estimate: Long,
        limit: Long,
        soundtrackBytes: Long,
    ): String {
        val hours = plan.totalDurationUs / 3_600_000_000.0
        val fitting = plan.fittingBitRate(limit, soundtrackBytes) / 1_000_000.0
        return context.getString(
            R.string.export_error_extend_too_big,
            hours,
            estimate / 1_000_000_000.0,
            limit / 1_000_000_000.0,
            fitting,
        )
    }

    @Suppress("ReturnCount")
    private fun openTarget(
        fileName: String,
        destination: Uri?,
    ): Target {
        val resolver = context.contentResolver
        if (destination != null) {
            val descriptor =
                resolver.openFileDescriptor(destination, "w")
                    ?: return Target.Failed(
                        context.getString(R.string.export_error_destination_write),
                    )
            return Target.Opened(destination, descriptor, viaMediaStore = false)
        }
        val values =
            ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, fileName)
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/BeatRealms")
                put(MediaStore.Video.Media.IS_PENDING, 1)
            }
        val uri =
            resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)
                ?: return Target.Failed(
                    context.getString(R.string.export_error_library_insert),
                )
        val descriptor = resolver.openFileDescriptor(uri, "w")
        if (descriptor == null) {
            bestEffort(TAG, "resolver.delete(uri, null, null)") { resolver.delete(uri, null, null) }
            return Target.Failed(context.getString(R.string.export_error_library_open))
        }
        return Target.Opened(uri, descriptor, viaMediaStore = true)
    }

    private fun publish(target: Target.Opened): Boolean =
        !target.viaMediaStore || publishPending(context.contentResolver.pendingRows(), target.uri)

    private fun discard(target: Target.Opened) {
        val resolver = context.contentResolver
        if (target.viaMediaStore) {
            bestEffort(TAG, "resolver.delete(target.uri, null, null)") { resolver.delete(target.uri, null, null) }
        } else {
            bestEffort(TAG, "DocumentsContract.deleteDocument(resolve...") { DocumentsContract.deleteDocument(resolver, target.uri) }
        }
    }

    /** One clip of the soundtrack, encoded and waiting to be laid over the repeats. */
    private class TranscodedClip(
        val audio: AudioTranscoder.Result,
    )

    private class Soundtrack(
        val clips: List<TranscodedClip>,
    ) {
        val bytes: Long get() = clips.sumOf { it.audio.file.length() }

        fun release() {
            clips.forEach { it.audio.release() }
        }
    }

    companion object {
        /**
         * The ceiling an MP4 written by the muxer can address: it records 32-bit chunk
         * offsets, so past four gigabytes the index points at the wrong bytes and the file will
         * not play. A three-hour render therefore has to be rendered at a bit rate that fits,
         * which is what the failure message says. Native (`core/output/EncodePlan`), beside the
         * lower target [MuxerBudget] aims renders at.
         */
        val MUXER_SAFE_BYTES: Long get() = ExportNative.exportMuxerHardLimitBytes()

        /** How often the copy is asked for its progress and told of a cancel. */
        private const val POLL_MS = 50L
    }
}

private const val TAG = "LoopExtend"
