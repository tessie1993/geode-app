package dev.geode.data

import android.content.ContentResolver
import android.content.Intent
import android.content.IntentSender
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.MediaStore
import android.util.Size
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

/**
 * The `ContentResolver` calls the screens used to make themselves: the system consent requests for
 * a write or a delete on tracks the app does not own, and the persisted grants for a document or a
 * folder the user picked.
 *
 * One class for the one source, with every function main-safe: each hops to [io] itself, so a
 * composable can call it from its own scope without knowing which thread a binder call wants. What
 * comes back is a plain value ([IntentSender], [Boolean]), never the resolver. `GeodeContainer`
 * builds the one instance, with the same IO dispatcher `DataModule` provides as `@IoDispatcher`.
 */
class MediaStoreSource(
    private val resolver: ContentResolver,
    private val io: CoroutineDispatcher,
) {
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
}
