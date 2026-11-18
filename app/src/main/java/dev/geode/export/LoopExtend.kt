package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import dev.geode.R
import dev.geode.data.pendingRows
import dev.geode.data.publishPending
import dev.geode.engine.bridge.ExportNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.Closeable
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.ByteBuffer

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
 * The loop's video samples are read back with [MediaExtractor] and written straight into the
 * output with [MediaMuxer], once per repeat, with the presentation timestamps shifted by whole
 * loop lengths. No frame is decoded, blended or re-encoded here — a ten-hour file costs the same
 * render as a one-minute one, and only the muxing grows.
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
            val soundtrack =
                when (val built = transcode(audio, onProgress, isCancelled)) {
                    is AudioBuild.Ready -> built.soundtrack
                    is AudioBuild.Failed -> return@withContext Result.Failed(built.message)
                }
            try {
                write(reel, soundtrack, fileName, destination, sizeLimitBytes, onProgress, isCancelled)
            } finally {
                soundtrack.release()
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
    private fun write(
        reel: LoopReel,
        soundtrack: Soundtrack,
        fileName: String,
        destination: Uri?,
        sizeLimitBytes: Long,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result {
        if (soundtrack.totalUs <= 0L) return Result.Failed(context.getString(R.string.export_error_extend_empty_soundtrack))
        val plan = LoopExtendPlan.of(reel.loopDurationUs, soundtrack.totalUs, reel.loops.size)
        val estimate = plan.estimatedBytes(reel.bytes / reel.loops.size, soundtrack.bytes)
        if (estimate > sizeLimitBytes) return Result.Failed(oversizeMessage(plan, estimate, sizeLimitBytes, soundtrack.bytes))
        val target =
            when (val opened = openTarget(fileName, destination)) {
                is Target.Opened -> opened
                is Target.Failed -> return Result.Failed(opened.message)
            }
        return try {
            val completed = target.descriptor.use { mux(it, reel, plan, soundtrack, onProgress, isCancelled) }
            finish(target, completed, plan)
        } catch (e: CancellationException) {
            discard(target)
            throw e
        } catch (e: Exception) {
            discard(target)
            Result.Failed(
                when (e) {
                    is IllegalArgumentException -> context.getString(R.string.export_error_extend_copy, e.message)
                    is IllegalStateException -> e.message ?: context.getString(R.string.export_error_extend_assemble)
                    else -> context.getString(R.string.export_error_extend_write, e.message)
                },
            )
        }
    }

    private fun finish(
        target: Target.Opened,
        completed: Boolean,
        plan: LoopExtendPlan,
    ): Result =
        if (completed && !publish(target)) {
            Result.Failed(context.getString(R.string.export_error_library_publish))
        } else if (completed) {
            Result.Saved(uri = target.uri, durationMs = plan.totalDurationUs / 1000, plan = plan)
        } else {
            discard(target)
            Result.Cancelled
        }

    /**
     * Copies the loops into the output, repeat by repeat, with the soundtrack interleaved.
     *
     * Returns false when the caller cancelled part way through.
     */
    private fun mux(
        pfd: ParcelFileDescriptor,
        reel: LoopReel,
        plan: LoopExtendPlan,
        soundtrack: Soundtrack,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Boolean {
        val readers = mutableListOf<LoopReader>()
        var muxer: MediaMuxer? = null
        var feed: AudioReel? = null
        var started = false
        var stopped = false
        try {
            reel.loops.forEach { readers += LoopReader.open(it.file, readers.firstOrNull()?.buffer) }
            checkOneEncoding(readers)
            val writer = MediaMuxer(pfd.fileDescriptor, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4).also { muxer = it }
            val videoTrack = writer.addTrack(readers.first().format)
            val audioTrack = writer.addTrack(soundtrack.format)
            writer.start()
            started = true
            val audio = AudioReel(writer, audioTrack, soundtrack.clips, soundtrack.starts).also { feed = it }
            for ((done, loopWrite) in plan.writes.withIndex()) {
                if (isCancelled()) return false
                readers[loopWrite.loopIndex].copyInto(writer, videoTrack, loopWrite.offsetUs, plan.totalDurationUs)
                audio.writeUpTo(loopWrite.offsetUs + plan.loopDurationUs)
                onProgress(ExportNative.loopCopyProgress(done + 1, plan.repeats))
            }
            audio.writeUpTo(Long.MAX_VALUE)
            writer.stop()
            stopped = true
            onProgress(1f)
            return true
        } finally {
            bestEffort(TAG, "feed?.close()") { feed?.close() }
            if (started && !stopped) runCatching { muxer?.stop() }
            bestEffort(TAG, "muxer?.release()") { muxer?.release() }
            readers.forEach { it.close() }
        }
    }

    /**
     * Every loop's samples land in one track, which carries one codec configuration, so all the
     * loops must have come out of the encoder identically configured. They do when they were
     * rendered by one [LoopRender] pass; this catches a reel assembled from anything else before
     * it becomes a file that plays as garbage after the first palette step.
     */
    private fun checkOneEncoding(readers: List<LoopReader>) {
        val first = readers.first().signature
        check(readers.all { it.signature == first }) {
            "These loops were not encoded the same way, so they cannot share one video track. " +
                "Render the reel again in one pass."
        }
    }

    private fun transcode(
        audio: LongFormAudio,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): AudioBuild {
        val clips = audio.clips
        val transcoder = AudioTranscoder(context)
        val done = mutableListOf<TranscodedClip>()
        val transcoded =
            runCatching {
                clips.forEachIndexed { index, clip ->
                    val result =
                        transcoder.transcode(
                            uri = clip.uri,
                            maxDurationMs = 0L,
                            startMs = 0L,
                            // One AAC track carries one rate, so every clip is brought to the same one.
                            outputRateHz = ExportNative.mixRateHz(),
                            isCancelled = isCancelled,
                        ) { progress ->
                            onProgress(ExportNative.loopTranscodeProgress(index, progress, clips.size))
                        }
                    done += TranscodedClip(clip, result)
                }
            }
        transcoded.exceptionOrNull()?.let { failure ->
            done.forEach { it.audio.release() }
            if (failure is CancellationException) throw failure
            return AudioBuild.Failed(audioMessage(failure))
        }
        return joinable(done, ExportNative.loopClipStarts(LongArray(done.size) { done[it].audio.durationUs }))
    }

    /**
     * The clips become one AAC track, so they must agree on sample rate and channel count — a
     * track carries one codec configuration. [transcode] brings every clip to
     * [ExportNative.mixRateHz], so in practice this now catches a mono clip among stereo ones.
     * Rejecting the mix up front beats writing an hour of audio that plays at the wrong speed.
     */
    private fun joinable(
        clips: List<TranscodedClip>,
        starts: LongArray,
    ): AudioBuild {
        val first = clips.first().audio.format
        val odd = clips.firstOrNull { !sameAudioShape(first, it.audio.format) }
        if (odd != null) {
            clips.forEach { it.audio.release() }
            return AudioBuild.Failed(
                "\"${odd.clip.title}\" is recorded at a different sample rate or channel count from the first " +
                    "track in this mix. Convert the tracks to match, or export them as separate videos.",
            )
        }
        return AudioBuild.Ready(Soundtrack(clips, first, starts))
    }

    private fun sameAudioShape(
        a: MediaFormat,
        b: MediaFormat,
    ): Boolean = audioShape(a) == audioShape(b)

    private fun audioShape(format: MediaFormat): List<Any?> =
        listOf(
            format.getString(MediaFormat.KEY_MIME),
            format.getInteger(MediaFormat.KEY_SAMPLE_RATE),
            format.getInteger(MediaFormat.KEY_CHANNEL_COUNT),
        )

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
        val clip: MixClip,
        val audio: AudioTranscoder.Result,
    )

    private class Soundtrack(
        val clips: List<TranscodedClip>,
        val format: MediaFormat,
        val starts: LongArray,
    ) {
        val totalUs: Long get() = starts.last()
        val bytes: Long get() = clips.sumOf { it.audio.file.length() }

        fun release() {
            clips.forEach { it.audio.release() }
        }
    }

    /**
     * Reads one rendered loop's encoded samples back, as many times as the plan asks for.
     *
     * The loop was written with a keyframe interval of one second and a keyframe on its first
     * frame, and Android's surface encoders emit no B-frames, so each copy starts on an IDR and
     * every sample's timestamp needs only a constant offset added to it. That is the whole trick
     * behind extending without re-rendering.
     */
    private class LoopReader private constructor(
        private val extractor: MediaExtractor,
        val format: MediaFormat,
        val buffer: ByteBuffer,
    ) : Closeable {
        private val info = MediaCodec.BufferInfo()

        /** Identifies the encoder configuration this loop carries: width, height, mime and csd. */
        val signature: List<Any?> =
            listOf(
                format.getString(MediaFormat.KEY_MIME),
                format.getInteger(MediaFormat.KEY_WIDTH),
                format.getInteger(MediaFormat.KEY_HEIGHT),
                codecConfig(format, 0),
                codecConfig(format, 1),
            )

        // Every sample's time and extractor flags, read once; which each repeat writes, and when, is native.
        private val times: LongArray
        private val flags: IntArray
        init {
            val scanned = ArrayList<Pair<Long, Int>>()
            while (extractor.sampleTime >= 0) {
                scanned += extractor.sampleTime to extractor.sampleFlags
                extractor.advance()
            }
            times = LongArray(scanned.size) { scanned[it].first }
            flags = IntArray(scanned.size) { scanned[it].second }
        }

        fun copyInto(
            muxer: MediaMuxer,
            track: Int,
            offsetUs: Long,
            limitUs: Long,
        ) {
            val outPts = LongArray(times.size)
            val outFlags = IntArray(times.size)
            val count = ExportNative.loopRepeatSamples(times, flags, offsetUs, limitUs, outPts, outFlags)
            extractor.seekTo(0L, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            for (i in 0 until count) {
                val size = readNext()
                if (size < 0) break
                info.set(0, size, outPts[i], outFlags[i])
                buffer.position(0)
                buffer.limit(size)
                muxer.writeSampleData(track, buffer, info)
                extractor.advance()
            }
        }

        override fun close() {
            bestEffort(TAG, "extractor.release()") { extractor.release() }
        }

        private fun readNext(): Int {
            buffer.clear()
            return extractor.readSampleData(buffer, 0)
        }

        companion object {
            fun open(
                file: File,
                shared: ByteBuffer?,
            ): LoopReader {
                val extractor = MediaExtractor()
                val opened =
                    runCatching {
                        extractor.setDataSource(file.absolutePath)
                        val track =
                            (0 until extractor.trackCount).firstOrNull {
                                extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true
                            } ?: error("The rendered loop has no video track.")
                        extractor.selectTrack(track)
                        val format = extractor.getTrackFormat(track)
                        LoopReader(extractor, format, shared ?: ByteBuffer.allocateDirect(sampleBufferBytes(format)))
                    }
                return opened.getOrElse { failure ->
                    bestEffort(TAG, "extractor.release()") { extractor.release() }
                    throw failure
                }
            }

            /** Big enough for the loop's opening IDR, which is by far its largest sample (native). */
            private fun sampleBufferBytes(format: MediaFormat): Int =
                ExportNative.loopSampleBufferBytes(
                    if (format.containsKey(MediaFormat.KEY_MAX_INPUT_SIZE)) format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE) else 0,
                    format.getInteger(MediaFormat.KEY_WIDTH),
                    format.getInteger(MediaFormat.KEY_HEIGHT),
                )

            private fun codecConfig(
                format: MediaFormat,
                index: Int,
            ): List<Byte> {
                val key = "csd-$index"
                val source = if (format.containsKey(key)) format.getByteBuffer(key) else null
                val bytes = ByteArray(source?.remaining() ?: 0)
                source?.duplicate()?.get(bytes)
                return bytes.toList()
            }
        }
    }

    /**
     * Feeds the soundtrack in, clip after clip, keeping pace with the video repeats so the muxer
     * never has to hold hours of one track while it waits for the other.
     *
     * Each clip was encoded on its own, so the joins carry the usual AAC encoder priming: a
     * single frame of overlap at each boundary. Removing it would mean decoding and re-encoding
     * the whole soundtrack as one stream, which is the cost this mode exists to avoid.
     */
    private class AudioReel(
        private val muxer: MediaMuxer,
        private val track: Int,
        private val clips: List<TranscodedClip>,
        private val starts: LongArray,
    ) : Closeable {
        private val info = MediaCodec.BufferInfo()
        private var scratch: ByteBuffer = ByteBuffer.allocate(SCRATCH_BYTES)
        private var reader: RandomAccessFile? = null
        private var readerClip = -1
        private val sampleCounts = IntArray(clips.size) { clips[it].audio.sampleInfos.size }
        private val samplePts = clips.flatMap { it.audio.sampleInfos }.let { all -> LongArray(all.size) { all[it].presentationTimeUs } }
        private val cursor = IntArray(2)
        private val dueClip = IntArray(DUE_BATCH)
        private val dueSample = IntArray(DUE_BATCH)

        fun writeUpTo(untilUs: Long) {
            do {
                val due = ExportNative.loopAudioDue(starts, sampleCounts, samplePts, cursor, untilUs, dueClip, dueSample, duePts)
                for (i in 0 until due) {
                    if (dueClip[i] != readerClip) closeReader().also { readerClip = dueClip[i] }
                    val clip = clips[dueClip[i]]
                    write(clip, clip.audio.sampleInfos[dueSample[i]], duePts[i])
                }
            } while (due == DUE_BATCH)
        }

        override fun close() {
            closeReader()
        }

        private fun write(
            clip: TranscodedClip,
            sample: AudioTranscoder.SampleInfo,
        ) {
            val channel = openReader(clip).channel
            if (scratch.capacity() < sample.size) scratch = ByteBuffer.allocate(sample.size)
            scratch.clear()
            scratch.limit(sample.size)
            var read = 0
            while (read < sample.size) {
                val count = channel.read(scratch, sample.offset + read)
                if (count <= 0) break
                read += count
            }
            scratch.flip()
            info.set(0, read, ptsUs, sample.flags)
            muxer.writeSampleData(track, scratch, info)
        }

        private fun openReader(clip: TranscodedClip): RandomAccessFile =
            reader ?: RandomAccessFile(clip.audio.file, "r").also { reader = it }

        private fun closeReader() {
            bestEffort(TAG, "reader?.close()") { reader?.close() }
            reader = null
            readerClip = -1
        }
    }

    companion object {
        /**
         * The ceiling an MP4 written by [MediaMuxer] can address: it records 32-bit chunk
         * offsets, so past four gigabytes the index points at the wrong bytes and the file will
         * not play. A three-hour render therefore has to be rendered at a bit rate that fits,
         * which is what the failure message says. Native (`core/output/EncodePlan`), beside the
         * lower target [MuxerBudget] aims renders at.
         */
        val MUXER_SAFE_BYTES: Long get() = ExportNative.exportMuxerHardLimitBytes()

        private const val SCRATCH_BYTES = 64 * 1024
        private const val DUE_BATCH = 512
    }
}

private const val TAG = "LoopExtend"
