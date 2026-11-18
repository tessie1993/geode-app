package dev.geode.export

import android.content.Context
import android.content.res.AssetFileDescriptor
import android.media.MediaFormat
import android.net.Uri
import android.os.ParcelFileDescriptor
import dev.geode.engine.bridge.ExportAudioNative
import dev.geode.util.bestEffort
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer

/**
 * An export's soundtrack encoded to AAC in a scratch file, with the table its muxer copies the track from.
 *
 * The job is native (`core/output/AacTranscode`, tested by `core/tests/aac_transcode_test.cpp`): the AIFF reader or
 * the platform decoder, the start trim, the fold to two channels, the resample, the encoder and its drain. This
 * hands it the descriptors, runs it in slices so a cancel and the progress get through, and words its outcome.
 */
class AudioTranscoder(
    private val context: Context,
) {
    class Result(
        val format: MediaFormat,
        val file: File,
        private val table: LongArray,
        /** One AAC-LC frame past the last access unit, at the encoder's rate. */
        val durationUs: Long,
    ) {
        /** A view over the native table, a row per access unit in PTS order. */
        val sampleInfos: List<SampleInfo> =
            object : AbstractList<SampleInfo>() {
                override val size: Int get() = table.size / ExportAudioNative.AAC_SAMPLE_LONGS

                override fun get(index: Int): SampleInfo {
                    val at = index * ExportAudioNative.AAC_SAMPLE_LONGS
                    return SampleInfo(table[at], table[at + 1].toInt(), table[at + 2], table[at + 3].toInt())
                }
            }

        fun release() {
            bestEffort(TAG, "file.delete()") { file.delete() }
        }
    }

    class SampleInfo(
        val offset: Long,
        val size: Int,
        val presentationTimeUs: Long,
        val flags: Int,
    )

    /**
     * [outputRateHz] forces the AAC rate (a mix needs every clip at one rate); null lets native pick the AAC rate
     * for the source. Blocking; a cancel throws [kotlinx.coroutines.CancellationException], a track with no audio
     * [IllegalArgumentException], a track that cannot be read or a scratch file that cannot be written [IOException].
     */
    fun transcode(
        uri: Uri,
        maxDurationMs: Long,
        startMs: Long = 0L,
        outputRateHz: Int? = null,
        isCancelled: () -> Boolean = { false },
        onProgress: (Float) -> Unit = {},
    ): Result {
        val file = File.createTempFile("geode_aac_", ".bin", context.cacheDir)
        var job = 0L
        try {
            job = open(uri, file, startMs, maxDurationMs, outputRateHz ?: 0)
            while (ExportAudioNative.aacJobRun(job, SLICE_MS)) {
                if (isCancelled()) throw kotlinx.coroutines.CancellationException("Export cancelled")
                onProgress(ExportAudioNative.aacJobProgress(job))
            }
            return resultOf(job, file)
        } catch (t: Throwable) {
            bestEffort(TAG, "file.delete()") { file.delete() }
            throw t
        } finally {
            if (job != 0L) ExportAudioNative.aacJobDestroy(job)
        }
    }

    // Both descriptors are detached here and closed natively.
    private fun open(
        uri: Uri,
        file: File,
        startMs: Long,
        maxDurationMs: Long,
        outputRateHz: Int,
    ): Long {
        val afd = context.contentResolver.openAssetFileDescriptor(uri, "r") ?: throw IOException("The track could not be opened")
        val out =
            try {
                ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_WRITE_ONLY or ParcelFileDescriptor.MODE_TRUNCATE)
            } catch (e: IOException) {
                bestEffort(TAG, "afd.close()") { afd.close() }
                throw e
            }
        val length = afd.declaredLength
        val offset = if (length == AssetFileDescriptor.UNKNOWN_LENGTH) 0L else afd.startOffset
        val job =
            ExportAudioNative.aacJobCreate(
                afd.parcelFileDescriptor.detachFd(),
                offset,
                length,
                out.detachFd(),
                startMs,
                maxDurationMs,
                outputRateHz,
            )
        check(job != 0L) { "The audio transcode could not start" }
        return job
    }

    private fun resultOf(
        job: Long,
        file: File,
    ): Result {
        if (ExportAudioNative.aacJobState(job) != ExportAudioNative.AAC_JOB_DONE) {
            val detail = ExportAudioNative.aacJobDetail(job)
            throw when (ExportAudioNative.aacJobFailure(job)) {
                ExportAudioNative.AAC_FAILURE_NO_AUDIO -> IllegalArgumentException(detail)
                ExportAudioNative.AAC_FAILURE_WRITE, ExportAudioNative.AAC_FAILURE_UNREADABLE -> IOException(detail)
                else -> IllegalStateException(detail)
            }
        }
        val track = LongArray(ExportAudioNative.AAC_TRACK_LONGS).also { ExportAudioNative.aacJobTrack(job, it) }
        val format =
            MediaFormat
                .createAudioFormat(
                    MediaFormat.MIMETYPE_AUDIO_AAC,
                    track[ExportAudioNative.AAC_TRACK_RATE].toInt(),
                    track[ExportAudioNative.AAC_TRACK_CHANNELS].toInt(),
                ).apply {
                    setInteger(MediaFormat.KEY_BIT_RATE, track[ExportAudioNative.AAC_TRACK_BIT_RATE].toInt())
                    ExportAudioNative.aacJobCsd(job)?.let { setByteBuffer("csd-0", ByteBuffer.wrap(it)) }
                }
        val table = checkNotNull(ExportAudioNative.aacJobSamples(job)) { "The audio's sample table could not be read" }
        return Result(format, file, table, track[ExportAudioNative.AAC_TRACK_DURATION_US])
    }

    private companion object {
        /** How long each native run lasts before a cancel and the progress are looked at. */
        const val SLICE_MS = 50
    }
}

private const val TAG = "AudioTranscoder"
