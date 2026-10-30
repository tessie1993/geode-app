package dev.geode.playback

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import androidx.core.net.toUri
import androidx.media3.common.MediaMetadata
import androidx.media3.common.util.BitmapLoader
import androidx.media3.common.util.UnstableApi
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import com.google.common.util.concurrent.MoreExecutors
import java.util.concurrent.ExecutionException
import java.util.concurrent.Executors

@UnstableApi
class SessionBitmapLoader(
    context: Context,
) : BitmapLoader {
    private val appContext = context.applicationContext

    private val io = MoreExecutors.listeningDecorator(Executors.newSingleThreadExecutor { r -> Thread(r, "geode-art") })

    private var lastUri: String? = null
    private var lastFuture: ListenableFuture<Bitmap>? = null

    override fun supportsMimeType(mimeType: String): Boolean = mimeType.startsWith("image/")

    override fun decodeBitmap(data: ByteArray): ListenableFuture<Bitmap> =
        io.submit<Bitmap> {
            MediaArtwork.decodeBytes(data) ?: throw IllegalArgumentException("could not decode artwork bytes")
        }

    override fun loadBitmap(uri: Uri): ListenableFuture<Bitmap> =
        io.submit<Bitmap> {
            MediaArtwork.decodeEmbedded(appContext, uri.toString())
                ?: throw NoArtworkException(uri)
        }

    @Suppress("ReturnCount")
    override fun loadBitmapFromMetadata(metadata: MediaMetadata): ListenableFuture<Bitmap>? {
        metadata.artworkData?.let { return decodeBitmap(it) }
        // The track URI travels in extras rather than in artworkUri, because artworkUri is
        // republished to the platform session and out-of-process consumers try to image-decode
        // it. See MediaArtwork.EXTRA_EMBEDDED_ART_URI.
        metadata.extras?.getString(MediaArtwork.EXTRA_EMBEDDED_ART_URI)?.let { return loadEmbedded(it) }
        metadata.artworkUri?.let { return loadBitmap(it) }
        return null
    }

    /**
     * Memoises the most recent embedded-artwork load.
     *
     * media3 wraps this loader in a [androidx.media3.session.CacheBitmapLoader], but that cache
     * keys on `artworkUri`/`artworkData` alone — both null for our items, since the track URI
     * travels in extras. Every notification refresh (each play/pause tap) would therefore miss the
     * cache and re-run a full MediaMetadataRetriever open plus decode on this single thread, with
     * the artwork blinking out while the future is pending. One slot is enough: refreshes repeat
     * the *current* item.
     *
     * A memoised load that failed because the file has no picture makes the answer
     * null ("this item has no artwork") rather than the failed future again: media3's
     * notification provider logs every failed bitmap future, so handing back the same failure
     * on each refresh wrote one warning with a stack per play/pause for every track without art.
     * The first refresh of such a track still sees the failure once, because whether a file
     * carries a picture is only known after the retriever has opened it off the main thread.
     */
    @Synchronized
    private fun loadEmbedded(uri: String): ListenableFuture<Bitmap>? {
        val cached = lastFuture?.takeIf { uri == lastUri }
        // Only a file that was opened and has no picture is remembered as "no artwork"; a
        // cancelled or otherwise failed load (IO, a decode hiccup) is tried again next refresh.
        val failure = cached?.takeIf { it.isDone }?.let { runCatching { Futures.getDone(it) }.exceptionOrNull() }
        return when {
            cached != null && failure == null -> cached
            (failure as? ExecutionException)?.cause is NoArtworkException -> null
            else ->
                loadBitmap(uri.toUri()).also {
                    lastUri = uri
                    lastFuture = it
                }
        }
    }

    @Synchronized
    fun release() {
        lastUri = null
        lastFuture = null
        io.shutdown()
    }
}

/** The file opened fine and carries no picture: the one failure worth remembering per track. */
private class NoArtworkException(
    uri: Uri,
) : IllegalArgumentException("no embedded artwork in $uri")
