package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import android.net.Uri
import android.os.Build
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import dev.geode.util.bestEffort
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.Closeable
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.util.Locale

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

/** One palette stop's share of the running time: which loop fills it, and for how many repeats. */
data class LoopSegment(
    val loopIndex: Int,
    val startUs: Long,
    val repeats: Int,
)

/** One repeat of one loop, and where its first frame lands in the finished file. */
data class LoopWrite(
    val loopIndex: Int,
    val offsetUs: Long,
)

/**
 * How the rendered loops fill the running time.
 *
 * Nothing here re-renders or re-encodes: the plan is a list of offsets, and each offset is
 * applied to the already-encoded samples as they are copied into the long file.
 */
data class LoopExtendPlan(
    val loopDurationUs: Long,
    val totalDurationUs: Long,
    val segments: List<LoopSegment>,
) {
    init {
        require(loopDurationUs > 0) { "loopDurationUs must be positive, was $loopDurationUs" }
        require(totalDurationUs > 0) { "totalDurationUs must be positive, was $totalDurationUs" }
    }

    val writes: List<LoopWrite> =
        segments.flatMap { segment ->
            List(segment.repeats) { repeat -> LoopWrite(segment.loopIndex, segment.startUs + repeat * loopDurationUs) }
        }

    val repeats: Int get() = writes.size

    /** Every point where the loop wraps — what a seam preview scrubs to. */
    val seamsMs: List<Long> get() = writes.drop(1).map { it.offsetUs / 1000 }

    /** Every point where the palette steps to its next drift stop. */
    val driftStepsMs: List<Long> get() = segments.drop(1).map { it.startUs / 1000 }

    fun nearestSeamMs(positionMs: Long): Long? = seamsMs.minByOrNull { kotlin.math.abs(it - positionMs) }

    fun seamPreviewStartMs(
        seamMs: Long,
        leadMs: Long = LoopSpec.SEAM_PREVIEW_LEAD_MS,
    ): Long = (seamMs - leadMs).coerceIn(0L, totalDurationUs / 1000)

    /**
     * What the finished file will weigh.
     *
     * Every repeat writes the loop's samples again, so the video grows linearly with the running
     * time even though nothing is re-encoded. The headroom covers the sample tables, which for a
     * multi-hour file are megabytes of index in their own right.
     */
    fun estimatedBytes(
        loopBytes: Long,
        soundtrackBytes: Long,
    ): Long {
        val body = repeats * loopBytes + soundtrackBytes
        return body + body / SAMPLE_TABLE_HEADROOM_DIVISOR
    }

    /** The video bit rate a render would need to keep a file of this length under [limitBytes]. */
    fun fittingBitRate(
        limitBytes: Long,
        soundtrackBytes: Long,
    ): Int {
        val seconds = (totalDurationUs / 1_000_000L).coerceAtLeast(1L)
        val room = (limitBytes - soundtrackBytes).coerceAtLeast(0L)
        return (room * 8 / seconds).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
    }

    companion object {
        const val SAMPLE_TABLE_HEADROOM_DIVISOR: Long = 50

        fun of(
            loopDurationUs: Long,
            totalDurationUs: Long,
            loops: Int,
        ): LoopExtendPlan {
            require(loops >= 1) { "a plan needs at least one loop, was $loops" }
            val total = totalDurationUs.coerceAtLeast(1L)
            val repeats = ((total + loopDurationUs - 1) / loopDurationUs).toInt().coerceAtLeast(1)
            // A drift stop that would get no repeats is dropped rather than left empty: better a
            // few palette steps that actually come round than a schedule the video never reaches.
            val stops = minOf(loops, repeats)
            val each = repeats / stops
            val remainder = repeats % stops
            var startUs = 0L
            val segments =
                List(stops) { index ->
                    val count = each + if (index < remainder) 1 else 0
                    LoopSegment(index, startUs, count).also { startUs += count * loopDurationUs }
                }
            return LoopExtendPlan(loopDurationUs, total, segments)
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
            val chapters: ChapterMarkers,
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
            if (reel.loops.isEmpty()) return@withContext Result.Failed("There is no rendered loop to extend.")
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

    @Suppress("ReturnCount")
    private fun write(
        reel: LoopReel,
        soundtrack: Soundtrack,
        fileName: String,
        destination: Uri?,
        sizeLimitBytes: Long,
        onProgress: (Float) -> Unit,
        isCancelled: () -> Boolean,
    ): Result {
        if (soundtrack.totalUs <= 0L) return Result.Failed("That soundtrack decoded to nothing — the file may be empty or corrupt.")
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
            finish(target, completed, plan, soundtrack)
        } catch (e: IllegalStateException) {
            discard(target)
            Result.Failed(e.message ?: "The long-form file could not be assembled.")
        } catch (e: IllegalArgumentException) {
            discard(target)
            Result.Failed("The rendered loop could not be copied into the long file: ${e.message}")
        } catch (e: IOException) {
            discard(target)
            Result.Failed("Writing the long-form file failed — check there is room on the device. (${e.message})")
        }
    }

    private fun finish(
        target: Target.Opened,
        completed: Boolean,
        plan: LoopExtendPlan,
        soundtrack: Soundtrack,
    ): Result =
        if (completed) {
            publish(target)
            Result.Saved(
                uri = target.uri,
                durationMs = plan.totalDurationUs / 1000,
                chapters = ChapterMarkers.of(soundtrack.spans),
                plan = plan,
            )
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
            reel.loops.forEach { readers += LoopReader.open(it.file) }
            checkOneEncoding(readers)
            val writer = MediaMuxer(pfd.fileDescriptor, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4).also { muxer = it }
            val videoTrack = writer.addTrack(readers.first().format)
            val audioTrack = writer.addTrack(soundtrack.format)
            writer.start()
            started = true
            val audio = AudioReel(writer, audioTrack, soundtrack.clips).also { feed = it }
            var done = 0
            for (loopWrite in plan.writes) {
                if (isCancelled()) return false
                readers[loopWrite.loopIndex].copyInto(writer, videoTrack, loopWrite.offsetUs, plan.totalDurationUs)
                audio.writeUpTo(loopWrite.offsetUs + plan.loopDurationUs)
                done++
                onProgress(AUDIO_PROGRESS_SPAN + (1f - AUDIO_PROGRESS_SPAN) * done / plan.repeats)
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
        var startUs = 0L
        val transcoded =
            runCatching {
                clips.forEachIndexed { index, clip ->
                    val result =
                        transcoder.transcode(uri = clip.uri, maxDurationMs = 0L, startMs = 0L, isCancelled = isCancelled) { progress ->
                            onProgress(AUDIO_PROGRESS_SPAN * (index + progress) / clips.size)
                        }
                    done += TranscodedClip(clip, result, startUs)
                    startUs += result.durationUs
                }
            }
        transcoded.exceptionOrNull()?.let { failure ->
            done.forEach { it.audio.release() }
            if (failure is CancellationException) throw failure
            return AudioBuild.Failed(audioMessage(failure))
        }
        return joinable(done)
    }

    /**
     * The clips become one AAC track, so they must already agree on sample rate and channel
     * count — a track carries one codec configuration and nothing here resamples. Rejecting the
     * mix up front beats writing an hour of audio that plays at the wrong speed.
     */
    private fun joinable(clips: List<TranscodedClip>): AudioBuild {
        val first = clips.first().audio.format
        val odd = clips.firstOrNull { !sameAudioShape(first, it.audio.format) }
        if (odd != null) {
            clips.forEach { it.audio.release() }
            return AudioBuild.Failed(
                "\"${odd.clip.title}\" is recorded at a different sample rate or channel count from the first " +
                    "track in this mix. Convert the tracks to match, or export them as separate videos.",
            )
        }
        return AudioBuild.Ready(Soundtrack(clips, first))
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
            is IllegalArgumentException -> "One of the tracks in this mix has no audio Geode can read: ${failure.message}"
            is IOException -> "One of the tracks in this mix could not be read — it may have moved or been deleted."
            else -> failure.message ?: "The soundtrack could not be prepared."
        }

    private fun oversizeMessage(
        plan: LoopExtendPlan,
        estimate: Long,
        limit: Long,
        soundtrackBytes: Long,
    ): String {
        val hours = plan.totalDurationUs / 3_600_000_000.0
        val fitting = plan.fittingBitRate(limit, soundtrackBytes) / 1_000_000.0
        return String.format(
            Locale.ROOT,
            "%.1f h at this quality would come to about %.1f GB, past the %.0f GB an MP4 written on Android can " +
                "address. Render the loop at about %.1f Mbps (a smaller size or lower quality) or make the video shorter.",
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
                        "The folder you chose would not let the file be written. Some cloud providers refuse " +
                            "this; try your Videos library or a folder on the device.",
                    )
            return Target.Opened(destination, descriptor, viaMediaStore = false)
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            // Pre-scoped-storage, writing into the Videos library needs
            // WRITE_EXTERNAL_STORAGE. Saying so beats letting the insert throw.
            return Target.Failed(
                "On this version of Android, Geode cannot add to your Videos library directly. " +
                    "Render to a folder you choose instead.",
            )
        }
        val values =
            ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, fileName)
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/Geode")
                    put(MediaStore.Video.Media.IS_PENDING, 1)
                }
            }
        val uri =
            resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)
                ?: return Target.Failed(
                    "Your Videos library would not accept a new file. Check that storage is not full, " +
                        "or render to a folder you choose instead.",
                )
        val descriptor = resolver.openFileDescriptor(uri, "w")
        if (descriptor == null) {
            bestEffort(TAG, "resolver.delete(uri, null, null)") { resolver.delete(uri, null, null) }
            return Target.Failed("The new file in your Videos library could not be opened for writing.")
        }
        return Target.Opened(uri, descriptor, viaMediaStore = true)
    }

    private fun publish(target: Target.Opened) {
        if (!target.viaMediaStore || Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        val done = ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }
        bestEffort(TAG, "resolver.update(target.uri, done, null, null)") {
            context.contentResolver.update(target.uri, done, null, null)
        }
    }

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
        val startUs: Long,
    ) {
        val durationUs: Long get() = audio.durationUs
    }

    private class Soundtrack(
        val clips: List<TranscodedClip>,
        val format: MediaFormat,
    ) {
        val totalUs: Long get() = clips.lastOrNull()?.let { it.startUs + it.durationUs } ?: 0L

        val bytes: Long get() = clips.sumOf { it.audio.file.length() }

        /** Where each clip landed — the only place chapter boundaries ever come from. */
        val spans: List<MixClipSpan> get() = clips.map { MixClipSpan(it.clip, it.startUs / 1000, it.durationUs / 1000) }

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
    ) : Closeable {
        private val buffer: ByteBuffer = ByteBuffer.allocateDirect(sampleBufferBytes(format))
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

        fun copyInto(
            muxer: MediaMuxer,
            track: Int,
            offsetUs: Long,
            limitUs: Long,
        ): Int {
            extractor.seekTo(0L, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            var written = 0
            var size = readNext()
            while (size >= 0 && offsetUs + extractor.sampleTime < limitUs) {
                info.set(0, size, offsetUs + extractor.sampleTime, muxerFlags(extractor.sampleFlags))
                buffer.position(0)
                buffer.limit(size)
                muxer.writeSampleData(track, buffer, info)
                written++
                extractor.advance()
                size = readNext()
            }
            return written
        }

        override fun close() {
            bestEffort(TAG, "extractor.release()") { extractor.release() }
        }

        private fun readNext(): Int {
            buffer.clear()
            return extractor.readSampleData(buffer, 0)
        }

        companion object {
            fun open(file: File): LoopReader {
                val extractor = MediaExtractor()
                val opened =
                    runCatching {
                        extractor.setDataSource(file.absolutePath)
                        val track =
                            (0 until extractor.trackCount).firstOrNull {
                                extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true
                            } ?: error("The rendered loop has no video track.")
                        extractor.selectTrack(track)
                        LoopReader(extractor, extractor.getTrackFormat(track))
                    }
                return opened.getOrElse { failure ->
                    bestEffort(TAG, "extractor.release()") { extractor.release() }
                    throw failure
                }
            }

            private fun muxerFlags(extractorFlags: Int): Int =
                if (extractorFlags and MediaExtractor.SAMPLE_FLAG_SYNC != 0) MediaCodec.BUFFER_FLAG_KEY_FRAME else 0

            /** Big enough for the loop's opening IDR, which is by far its largest sample. */
            private fun sampleBufferBytes(format: MediaFormat): Int {
                val declared =
                    if (format.containsKey(MediaFormat.KEY_MAX_INPUT_SIZE)) format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE) else 0
                val pixels = format.getInteger(MediaFormat.KEY_WIDTH) * format.getInteger(MediaFormat.KEY_HEIGHT)
                return maxOf(declared, pixels, MIN_SAMPLE_BUFFER_BYTES)
            }

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
    ) : Closeable {
        private val info = MediaCodec.BufferInfo()
        private var scratch: ByteBuffer = ByteBuffer.allocate(SCRATCH_BYTES)
        private var reader: RandomAccessFile? = null
        private var clipIndex = 0
        private var sampleIndex = 0

        fun writeUpTo(untilUs: Long) {
            var done = false
            while (!done) {
                val clip = clips.getOrNull(clipIndex)
                val sample = clip?.audio?.sampleInfos?.getOrNull(sampleIndex)
                when {
                    clip == null -> done = true
                    sample == null -> nextClip()
                    clip.startUs + sample.presentationTimeUs >= untilUs -> done = true
                    else -> {
                        write(clip, sample)
                        sampleIndex++
                    }
                }
            }
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
            info.set(0, read, clip.startUs + sample.presentationTimeUs, sample.flags)
            muxer.writeSampleData(track, scratch, info)
        }

        private fun openReader(clip: TranscodedClip): RandomAccessFile =
            reader ?: RandomAccessFile(clip.audio.file, "r").also { reader = it }

        private fun nextClip() {
            closeReader()
            clipIndex++
            sampleIndex = 0
        }

        private fun closeReader() {
            bestEffort(TAG, "reader?.close()") { reader?.close() }
            reader = null
        }
    }

    companion object {
        /**
         * The ceiling an MP4 written by [MediaMuxer] can address: it records 32-bit chunk
         * offsets, so past four gigabytes the index points at the wrong bytes and the file will
         * not play. A three-hour render therefore has to be rendered at a bit rate that fits,
         * which is what the failure message says.
         */
        const val MUXER_SAFE_BYTES: Long = 4L * 1024 * 1024 * 1024 - 64L * 1024 * 1024

        private const val AUDIO_PROGRESS_SPAN = 0.35f
        private const val SCRATCH_BYTES = 64 * 1024
        private const val MIN_SAMPLE_BUFFER_BYTES = 1024 * 1024
    }
}

private const val TAG = "LoopExtend"
