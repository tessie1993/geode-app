package dev.geode.data

import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.content.IntentSender
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.MediaStore
import android.util.Size
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import java.io.File
import java.io.InputStream

/**
 * The `ContentResolver` calls the screens and the session's controllers used to make themselves: the
 * system consent requests for a write or a delete on tracks the app does not own, the persisted
 * grants for a document or a folder the user picked, and the reads, names and folder copies of those
 * documents (a background image, a preset, a template, a .milk or a folder of them, a queued file).
 *
 * One class for the one source, with every function main-safe: each hops to [io] itself, so a
 * composable can call it from its own scope without knowing which thread a binder call wants. What
 * comes back is a plain value ([IntentSender], [Boolean], a name, what a reader made of the bytes),
 * never the resolver; [opener] alone hands back a lazy open for a reader that opens on its own IO
 * thread. `GeodeContainer` builds the one instance, with the same IO dispatcher `DataModule`
 * provides as `@IoDispatcher`.
 */
class MediaStoreSource(
    private val context: Context,
    private val io: CoroutineDispatcher,
) {
    private val resolver: ContentResolver = context.contentResolver

    /** The system's "allow this app to change these files" request (API 30+) for [uris]. */
    suspend fun writeRequest(uris: List<Uri>): IntentSender = withContext(io) { MediaStore.createWriteRequest(resolver, uris).intentSender }

    /** The system's "delete these files" request (API 30+) for [uris]. */
    suspend fun deleteRequest(uris: List<Uri>): IntentSender =
        withContext(io) { MediaStore.createDeleteRequest(resolver, uris).intentSender }

    /**
     * Keeps read access to each of [uris] across restarts. It has to run while the picker's own
     * grant is still active; a URI the provider refuses is skipped, as the others still count.
     */
    suspend fun keepReadAccess(uris: List<Uri>) {
        withContext(io) {
            uris.forEach { uri ->
                resolver.persistGrant(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
        }
    }

    /** Keeps read and write access to the picked folder [tree]; false when the provider refuses. */
    suspend fun keepFolderAccess(tree: Uri): Boolean =
        withContext(io) {
            resolver.persistGrant(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        }

    /**
     * A picture of the video at [uri], about [width] × [height]: the media row's cached thumbnail,
     * else one decoded frame (a document-tree URI or an unindexed row has no cached thumbnail).
     * Null when both fail.
     */
    suspend fun videoFrame(
        uri: Uri,
        width: Int,
        height: Int,
    ): Bitmap? =
        withContext(io) {
            runCatching { resolver.loadThumbnail(uri, Size(width, height), null) }.getOrNull()
                ?: runCatching {
                    resolver.openFileDescriptor(uri, "r")?.use { file ->
                        val retriever = MediaMetadataRetriever()
                        try {
                            retriever.setDataSource(file.fileDescriptor)
                            retriever.getScaledFrameAtTime(-1, MediaMetadataRetriever.OPTION_CLOSEST_SYNC, width, height)
                        } finally {
                            retriever.release()
                        }
                    }
                }.getOrNull()
        }

    /** Gives back the read grant [keepReadAccess] kept for [uri]; one never released is a permission leak. */
    suspend fun releaseReadAccess(uri: Uri) {
        withContext(io) {
            runCatching { resolver.releasePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        }
    }

    /** False when the document at [uri] no longer opens (deleted, or its grant is gone). */
    suspend fun isReadable(uri: Uri): Boolean =
        withContext(io) {
            runCatching {
                resolver.openInputStream(uri)?.use { }
                true
            }.getOrDefault(false)
        }

    /**
     * What [block] makes of the document at [uri], read on [io]; null when the provider gives no
     * stream. A refusal or a failed read throws, so each caller keeps its own fallback.
     */
    suspend fun <T> read(
        uri: Uri,
        block: (InputStream) -> T,
    ): T? = withContext(io) { resolver.openInputStream(uri)?.use(block) }

    /** A lazy open of [uri] for a reader that opens it on its own IO thread; null when refused. */
    fun opener(uri: Uri): () -> InputStream? = { runCatching { resolver.openInputStream(uri) }.getOrNull() }

    /** The name the provider shows for [uri], or null; each caller keeps its own fallback. */
    suspend fun displayName(uri: Uri): String? = withContext(io) { resolver.displayNameOf(uri) }

    /**
     * Whether [uri], handed over by another app, is one the app may read: a content uri, or a file
     * uri outside this app's own data (another app must never make us read our own files).
     */
    suspend fun isForeignDocument(uri: Uri): Boolean =
        withContext(io) {
            when (uri.scheme) {
                ContentResolver.SCHEME_CONTENT -> true
                ContentResolver.SCHEME_FILE -> {
                    val path = uri.path
                    val own = File(context.applicationInfo.dataDir).canonicalPath
                    val target = path?.let { runCatching { File(it).canonicalPath }.getOrNull() }
                    target != null && target != own && !target.startsWith("$own/")
                }
                else -> false
            }
        }

    /**
     * Each file under the picked folder [tree], [maxDepth] folders deep at most, as its name and
     * uri. A walk the provider breaks off keeps what it found so far.
     */
    suspend fun filesUnder(
        tree: Uri,
        maxDepth: Int,
    ): List<Pair<String, Uri>> =
        withContext(io) {
            val out = mutableListOf<Pair<String, Uri>>()

            fun walk(
                dir: DocumentFile,
                depth: Int,
            ) {
                if (depth > maxDepth) return
                for (child in dir.listFiles()) {
                    when {
                        child.isDirectory -> walk(child, depth + 1)
                        child.isFile -> child.name?.let { out.add(it to child.uri) }
                    }
                }
            }
            runCatching { DocumentFile.fromTreeUri(context, tree)?.let { walk(it, 0) } }
            out
        }

    /**
     * Copies each of [files] (a file and its MIME type) into the picked folder [tree], replacing a
     * file of the same name. Best effort: a provider that refuses ends the copy quietly.
     */
    suspend fun copyIntoFolder(
        tree: Uri,
        files: List<Pair<File, String>>,
    ) {
        withContext(io) {
            runCatching {
                val folder = DocumentFile.fromTreeUri(context, tree) ?: return@runCatching
                for ((src, mime) in files) {
                    if (!src.exists()) continue
                    folder.findFile(src.name)?.delete()
                    val dest = folder.createFile(mime, src.name) ?: continue
                    resolver.openOutputStream(dest.uri)?.use { out -> src.inputStream().use { it.copyTo(out) } }
                }
            }
        }
    }

    /** Deletes the files called [names] from the picked folder [tree]. Best effort, as [copyIntoFolder]. */
    suspend fun deleteFromFolder(
        tree: Uri,
        names: List<String>,
    ) {
        withContext(io) {
            runCatching {
                val folder = DocumentFile.fromTreeUri(context, tree) ?: return@runCatching
                names.forEach { folder.findFile(it)?.delete() }
            }
        }
    }
}
