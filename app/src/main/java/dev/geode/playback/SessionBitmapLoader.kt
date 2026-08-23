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

@UnstableApi
class SessionBitmapLoader(
    context: Context,
) : BitmapLoader {
    private val appContext = context.applicationContext

    private val io = MoreExecutors.listeningDecorator(Executors.newSingleThreadExecutor { r -> Thread(r, "geode-art") })

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
        metadata.artworkUri?.let { return loadBitmap(it) }
        return null
    }

    fun release() {
        io.shutdown()
    }
}
