package dev.geode.data

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import androidx.annotation.WorkerThread
import androidx.core.net.toUri
import dev.geode.R
import dev.geode.RingLog
import dev.geode.editor.TrackRef
import dev.geode.util.bestEffort
import org.json.JSONObject
import java.io.File
import java.io.FileDescriptor

/**
 * Where a recording's MP4 is written while it records, and where it ends up.
 *
 * Once the user has said where recordings go ([RecordSaveStore]), the muxer writes straight into
 * the final file: a Videos library row inserted with `IS_PENDING` (published when it finishes,
 * deleted when it does not) or a new document in the chosen folder. Before that answer, the first
 * recording goes to a `studio-` scratch file in the cache (swept by `GeodeContainer` if the process
 * dies while it records); once finished it is [park]ed out of the cache, where it waits for the save
 * sheet, and [place] copies it once the sheet is answered. A parked recording the process did not
 * outlive is still there at the next start ([waiting]), so the sheet asks again instead of losing it.
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

        /** The same file as a raw descriptor for the native muxer, which keeps its own duplicate; open until [finish]. */
        val rawFd: Int get() = pfd.fd

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

    /** A finished recording waiting for the save sheet's answer: its file, title, length and song. */
    class Waiting(
        val file: File,
        val title: String,
        val durationMs: Long,
        val track: TrackRef?,
    )

    /**
     * Moves the finished scratch [file] out of the cache into `no_backup/record-waiting` (which neither the
     * scratch sweep, the system's cache trim nor a cloud backup touches) with a note of its title, length and
     * song. When the move fails it waits where it is, as before: for this process only.
     */
    @WorkerThread
    fun park(
        file: File,
        title: String,
        durationMs: Long,
        track: TrackRef?,
    ): Waiting {
        val parked = File(waitingDir().apply { mkdirs() }, file.name.removePrefix(SCRATCH_PREFIX))
        if (!file.renameTo(parked)) return Waiting(file, title, durationMs, track)
        val note = JSONObject().put("title", title).put("durationMs", durationMs)
        track?.let { note.put("track", it.uri).put("trackTitle", it.title).put("trackMs", it.durationMs) }
        AtomicWrite.text(noteOf(parked), note.toString())
        return Waiting(parked, title, durationMs, track)
    }

    /** The oldest recording an earlier process parked and never placed, or null. */
    @WorkerThread
    fun waiting(): Waiting? {
        val file =
            waitingDir()
                .listFiles { f -> f.isFile && f.extension == "mp4" }
                .orEmpty()
                .minByOrNull { it.lastModified() } ?: return null
        val note = runCatching { JSONObject(noteOf(file).readText()) }.getOrNull()
        val track =
            note?.optString("track")?.takeIf { it.isNotEmpty() }?.let {
                TrackRef(it, note.optString("trackTitle"), note.optLong("trackMs"))
            }
        val title = note?.optString("title")?.takeIf { it.isNotBlank() } ?: appContext.getString(R.string.record_default_name)
        return Waiting(file, title, note?.optLong("durationMs") ?: 0L, track)
    }

    private fun waitingDir(): File = File(appContext.noBackupFilesDir, WAITING_DIR)

    private fun noteOf(file: File): File = File(file.path.removeSuffix(".mp4") + ".json")

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
        noteOf(file).delete()
        // A folder answered for this recording only took a grant nothing keeps.
        if (target is SaveTarget.Folder) saveTargets.releaseIfUnused(target.treeUri)
        return out?.finish(copied)
    }

    private fun scratch(nowMs: Long): Output? {
        val file = File(appContext.cacheDir, "${SCRATCH_PREFIX}record-$nowMs.mp4")
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

        /** `GeodeContainer`'s scratch sweep deletes every cache file with this prefix at start. */
        const val SCRATCH_PREFIX = "studio-"
        const val WAITING_DIR = "record-waiting"
    }
}
