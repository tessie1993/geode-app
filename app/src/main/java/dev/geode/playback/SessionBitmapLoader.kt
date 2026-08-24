package dev.geode.playback

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import androidx.media3.common.MediaMetadata
import androidx.media3.common.util.BitmapLoader
import androidx.media3.common.util.UnstableApi
import com.google.common.util.concurrent.ListenableFuture
import com.google.common.util.concurrent.MoreExecutors
import java.util.concurrent.Executors
import androidx.core.net.toUri

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
                ?: throw IllegalArgumentException("no embedded artwork in $uri")
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
     */
    @Synchronized
    private fun loadEmbedded(uri: String): ListenableFuture<Bitmap> {
        lastFuture?.let { if (uri == lastUri) return it }
        return loadBitmap(uri.toUri()).also {
            lastUri = uri
            lastFuture = it
        }
    }

    @Synchronized
    fun release() {
        lastUri = null
        lastFuture = null
        io.shutdown()
    }
}
