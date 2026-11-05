package dev.geode.data

import android.content.ContentValues
import android.content.Context
import android.hardware.display.DisplayManager
import android.net.Uri
import android.os.Build
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.MediaStore
import android.view.Display
import androidx.annotation.WorkerThread
import androidx.core.net.toUri
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.io.File

/** A stopped capture take's MP4, still in app scratch until [TakeCapture.publish] puts it where takes go. */
data class TakeVideo(
    val file: File,
    val durationMs: Long,
)

/**
 * The storage and lifecycle glue of a capture take (ADR 0008). The take itself, encoders, muxer, timing and
 * the stage's frames, is native (`core/output/TakeSession`); this opens the file it writes, starts and ends
 * it, and copies the finished MP4 to the Videos library or the folder the user picked.
 *
 * The file is render scratch in the cache (`studio-take-…`), so a take cut short by a crash is swept by
 * `GeodeContainer` at the next start rather than left behind.
 *
 * Every call blocks on I/O or the encoders' drain: call it from the session's store dispatcher, one thread,
 * which is also what keeps [begin] and [end] in order.
 */
class TakeCapture(
    context: Context,
) {
    private val appContext = context.applicationContext
    private var fd: ParcelFileDescriptor? = null
    private var file: File? = null

    /** Starts capturing the stage. False when it could not: no stage drawn yet, or no encoder would start. */
    @WorkerThread
    fun begin(): Boolean {
        if (fd != null) return false
        val out = File(appContext.cacheDir, "studio-take-${System.currentTimeMillis()}.mp4")
        val opened =
            runCatching {
                ParcelFileDescriptor.open(
                    out,
                    ParcelFileDescriptor.MODE_READ_WRITE or ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_TRUNCATE,
                )
            }.getOrNull() ?: return false
        if (!GeodeNative.takeBegin(opened.fd, displayFps())) {
            bestEffort(TAG, "close refused take") { opened.close() }
            out.delete()
            return false
        }
        fd = opened
        file = out
        return true
    }

    /** Ends the capture and finishes its MP4; null when there was none, or it holds no picture. */
    @WorkerThread
    fun end(): TakeVideo? {
        val opened = fd ?: return null
        val out = file ?: return null
        fd = null
        file = null
        val durationMs = GeodeNative.takeEnd()
        bestEffort(TAG, "close take file") { opened.close() }
        if (durationMs < 0L) {
            out.delete()
            return null
        }
        return TakeVideo(out, durationMs)
    }

    /**
     * Copies [video] to [target] and deletes the scratch file once it is safe there. The
     * library row is inserted pending, like every render's, so the gallery never lists a half-copied video.
     * Null when the copy failed; the scratch file is then kept for the caller to retry or discard.
     */
    @WorkerThread
    fun publish(
        video: TakeVideo,
        target: SaveTarget,
    ): Uri? {
        val display = "muzviz_take_${System.currentTimeMillis()}.mp4"
        val uri =
            when (target) {
                is SaveTarget.Folder -> intoFolder(video.file, target.treeUri, display)
                SaveTarget.VideosLibrary -> intoLibrary(video.file, display)
            }
        if (uri != null) video.file.delete() else dev.geode.RingLog.note(TAG, "take could not be saved to ${target.encode()}")
        return uri
    }

    @WorkerThread
    fun discard(video: TakeVideo) {
        video.file.delete()
    }

    private fun intoLibrary(
        source: File,
        display: String,
    ): Uri? {
        val resolver = appContext.contentResolver
        val values =
            ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, display)
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/MuzViz")
                    put(MediaStore.Video.Media.IS_PENDING, 1)
                }
            }
        val uri = runCatching { resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values) }.getOrNull() ?: return null
        val copied = runCatching { resolver.openOutputStream(uri)?.use { out -> source.inputStream().use { it.copyTo(out) } } != null }
        if (copied.getOrDefault(false)) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                bestEffort(TAG, "publish take row") {
                    resolver.update(uri, ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }, null, null)
                }
            }
            return uri
        }
        bestEffort(TAG, "delete half-made take row") { resolver.delete(uri, null, null) }
        return null
    }

    private fun intoFolder(
        source: File,
        treeUri: String,
        display: String,
    ): Uri? {
        val resolver = appContext.contentResolver
        val tree = treeUri.toUri()
        val doc =
            runCatching {
                val parent = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
                DocumentsContract.createDocument(resolver, parent, "video/mp4", display)
            }.getOrNull() ?: return null
        val copied = runCatching { resolver.openOutputStream(doc)?.use { out -> source.inputStream().use { it.copyTo(out) } } != null }
        if (copied.getOrDefault(false)) return doc
        bestEffort(TAG, "delete half-made take document") { DocumentsContract.deleteDocument(resolver, doc) }
        return null
    }

    /** The display's refresh rate; native holds the take to 24..60 fps. */
    private fun displayFps(): Float =
        runCatching {
            appContext.getSystemService(DisplayManager::class.java).getDisplay(Display.DEFAULT_DISPLAY).refreshRate
        }.getOrDefault(DEFAULT_FPS)

    private companion object {
        const val TAG = "TakeCapture"
        const val DEFAULT_FPS = 60f
    }
}
