package dev.geode.export

import android.content.Context
import android.content.res.AssetFileDescriptor
import android.net.Uri
import android.os.ParcelFileDescriptor
import dev.geode.engine.bridge.ExportAudioNative
import dev.geode.util.bestEffort
import java.io.File
import java.io.IOException

/**
 * An export's soundtrack encoded to AAC in a scratch file; the finished job's samples and format go straight to the
 * native muxer that copies the track (`handOver`), never through here.
 *
 * The job is native (`core/output/AacTranscode`, tested by `core/tests/aac_transcode_test.cpp`): the AIFF reader or
 * the platform decoder, the start trim, the fold to two channels, the resample, the encoder and its drain. This
 * hands it the descriptors, runs it in slices so a cancel and the progress get through, and words its outcome.
 */
class AudioTranscoder(
    private val context: Context,
) {
    /** The scratch file the AAC lives in, and how long it runs. */
    class Result(
        val file: File,
        /** One AAC-LC frame past the last access unit, at the encoder's rate. */
        val durationUs: Long,
    ) {
        fun release() {
            bestEffort(TAG, "file.delete()") { file.delete() }
        }
    }

    /**
     * [outputRateHz] forces the AAC rate (a mix needs every clip at one rate); null lets native pick the AAC rate
     * for the source. [handOver] is given the finished native job and its scratch file before the job is freed, to
     * pass the track to the native muxer. Blocking; a cancel throws [kotlinx.coroutines.CancellationException], a
     * track with no audio [IllegalArgumentException], a track that cannot be read or a scratch file that cannot be
     * written [IOException].
     */
    fun transcode(
        uri: Uri,
        maxDurationMs: Long,
        startMs: Long = 0L,
        outputRateHz: Int? = null,
        isCancelled: () -> Boolean = { false },
        handOver: (job: Long, file: File) -> Unit = { _, _ -> },
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
            return resultOf(job, file, handOver)
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
        handOver: (Long, File) -> Unit,
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
        handOver(job, file)
        return Result(file, track[ExportAudioNative.AAC_TRACK_DURATION_US])
    }

    private companion object {
        /** How long each native run lasts before a cancel and the progress are looked at. */
        const val SLICE_MS = 50
    }
}

private const val TAG = "AudioTranscoder"
