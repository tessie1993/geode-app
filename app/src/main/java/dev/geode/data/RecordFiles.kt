package dev.geode.data

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import androidx.annotation.WorkerThread
import androidx.core.net.toUri
import dev.geode.RingLog
import dev.geode.util.bestEffort
import java.io.File
import java.io.FileDescriptor

/**
 * Where a recording's MP4 is written while it records, and where it ends up.
 *
 * Once the user has said where recordings go ([RecordSaveStore]), the muxer writes straight into
 * the final file: a Videos library row inserted with `IS_PENDING` (published when it finishes,
 * deleted when it does not) or a new document in the chosen folder. Before that answer, the first
 * recording goes to a `studio-` scratch file in the cache (swept by `GeodeContainer` if the process
 * dies), and [place] copies it once the save sheet is answered.
 */
class RecordFiles(
    context: Context,
    val saveTargets: RecordSaveStore = RecordSaveStore(context),
) {
    private val appContext = context.applicationContext
    private val resolver get() = appContext.contentResolver

    /** One recording's file while the muxer writes it. */
    class Output internal constructor(
        private val pfd: ParcelFileDescriptor,
        /** The scratch file when no place was chosen yet, else null. */
        val scratch: File?,
        private val done: (keep: Boolean) -> Uri?,
    ) {
        val fd: FileDescriptor get() = pfd.fileDescriptor

        /** Closes the file; kept, the video's URI (the scratch file's for a scratch); else it is deleted and null comes back. */
        @WorkerThread
        fun finish(keep: Boolean): Uri? {
            bestEffort(TAG, "close the recording") { pfd.close() }
            return done(keep)
        }
    }

    /** A file for a recording starting now, in the chosen place, or scratch before the first answer. Null when none would open. */
    @WorkerThread
    fun open(nowMs: Long): Output? {
        val name = "beatrealms_record_$nowMs.mp4"
        return when (val target = saveTargets.target()) {
            null -> scratch(nowMs)
            is SaveTarget.Folder -> folder(target, name) ?: library(name)
            SaveTarget.VideosLibrary -> library(name)
        }
    }

    /** Copies the scratch [file] to [target] and deletes it; the video's URI, or null when it could not be written. */
    @WorkerThread
    fun place(
        file: File,
        target: SaveTarget,
    ): Uri? {
        val name = "beatrealms_record_${file.lastModified()}.mp4"
        val out = (if (target is SaveTarget.Folder) folder(target, name) else null) ?: library(name)
        val copied =
            out != null &&
                runCatching {
                    file.inputStream().use { input ->
                        ParcelFileDescriptor.AutoCloseOutputStream(ParcelFileDescriptor.dup(out.fd)).use { input.copyTo(it) }
                    }
                }.onFailure { RingLog.note(TAG, "recording copy failed", it) }.isSuccess
        file.delete()
        return out?.finish(copied)
    }

    private fun scratch(nowMs: Long): Output? {
        val file = File(appContext.cacheDir, "studio-record-$nowMs.mp4")
        val pfd =
            runCatching {
                ParcelFileDescriptor.open(
                    file,
                    ParcelFileDescriptor.MODE_READ_WRITE or ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_TRUNCATE,
                )
            }.getOrNull() ?: return null
        return Output(pfd, file) { keep ->
            if (keep) {
                Uri.fromFile(file)
            } else {
                file.delete()
                null
            }
        }
    }

    private fun library(name: String): Output? {
        val values =
            ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, name)
                put(MediaStore.Video.Media.MIME_TYPE, MIME)
                put(MediaStore.Video.Media.RELATIVE_PATH, LIBRARY_PATH)
                put(MediaStore.Video.Media.IS_PENDING, 1)
            }
        val row = runCatching { resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values) }.getOrNull() ?: return null
        val pfd = runCatching { resolver.openFileDescriptor(row, "rw") }.getOrNull()
        if (pfd == null) {
            bestEffort(TAG, "delete the recording row") { resolver.delete(row, null, null) }
            return null
        }
        return Output(pfd, null) { keep ->
            when {
                keep && publishPending(resolver.pendingRows(), row) -> row
                keep -> null
                else -> {
                    bestEffort(TAG, "delete the recording row") { resolver.delete(row, null, null) }
                    null
                }
            }
        }
    }

    private fun folder(
        target: SaveTarget.Folder,
        name: String,
    ): Output? {
        val tree = target.treeUri.toUri()
        val doc =
            runCatching {
                val parent = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
                DocumentsContract.createDocument(resolver, parent, MIME, name)
            }.onFailure { RingLog.note(TAG, "recording folder unavailable, saving to Videos", it) }.getOrNull() ?: return null
        val pfd = runCatching { resolver.openFileDescriptor(doc, "rw") }.getOrNull()
        if (pfd == null) {
            bestEffort(TAG, "delete the recording document") { DocumentsContract.deleteDocument(resolver, doc) }
            return null
        }
        return Output(pfd, null) { keep ->
            if (keep) {
                doc
            } else {
                bestEffort(TAG, "delete the recording document") { DocumentsContract.deleteDocument(resolver, doc) }
                null
            }
        }
    }

    private companion object {
        const val TAG = "RecordFiles"
        const val MIME = "video/mp4"
        const val LIBRARY_PATH = "Movies/BeatRealms"
    }
}
